#include "Parser.h"
#include "IrGenerator.h"

ExprResult AstToIr::generateMemberFunctionCallIr(const MemberFunctionCallNode& memberFunctionCallNode, ExpressionContext context) {
	std::vector<IrOperand> irOperands;
	irOperands.reserve(5 + memberFunctionCallNode.arguments().size() * 4); // ret + name + this + ~4 per arg

	FLASH_LOG(Codegen, Debug, "=== generateMemberFunctionCallIr START ===");

	// Get the object expression
	ASTNode object_node = memberFunctionCallNode.object();
	std::optional<StringHandle> immediate_lambda_object_name;
	std::optional<TypeSpecifierNode> immediate_lambda_object_type;

	// Special case: Immediate lambda invocation [](){}()
	// Check if the object is a LambdaExpressionNode (either directly or wrapped in ExpressionNode)
	const LambdaExpressionNode* lambda_ptr = nullptr;

	if (object_node.is<LambdaExpressionNode>()) {
		// Lambda stored directly
		lambda_ptr = &object_node.as<LambdaExpressionNode>();
	} else if (object_node.is<ExpressionNode>()) {
		const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
		if (const auto* lambda_expression = std::get_if<LambdaExpressionNode>(&object_expr)) {
			// Lambda wrapped in ExpressionNode
			lambda_ptr = lambda_expression;
		}
	}

	if (lambda_ptr) {
		const LambdaExpressionNode& lambda = *lambda_ptr;

		// CRITICAL: First, collect the lambda for generation!
		// This ensures operator() and __invoke functions will be generated.
		// Without this, the lambda is never added to collected_lambdas_ and
		// its functions are never generated, causing linker errors.
		ExprResult lambda_result = generateLambdaExpressionIr(lambda);

		// Check if this is a generic lambda (has auto parameters)
		bool is_generic = false;
		std::vector<size_t> auto_param_indices;
		size_t param_idx = 0;
		for (const auto& param_node : lambda.parameters()) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				if (isPlaceholderAutoType(param_type.type())) {
					is_generic = true;
					auto_param_indices.push_back(param_idx);
				}
			}
			param_idx++;
		}

		// For non-capturing lambdas, we can optimize by calling __invoke directly
		// (a static function that doesn't need a 'this' pointer).
		// For capturing lambdas, we must call operator() with the closure object.
		if (lambda.captures().empty()) {
			// Non-capturing lambda: call __invoke directly
			StringHandle closure_type_name = lambda.generate_lambda_name();
			StringHandle invoke_name = StringTable::getOrInternStringHandle(StringBuilder().append(closure_type_name).append("_invoke"sv));

			// Generate a direct function call to __invoke
			TempVar ret_var = var_counter.next();

			// Create CallOp structure (matching the pattern in generateFunctionCallIr)
			CallOp call_op;
			call_op.result = ret_var;

			// Build TypeSpecifierNode for return type (needed for mangling)
			// Per C++20 §7.5.5.1, a lambda with no return statements deduces void
			TypeSpecifierNode return_type_node(TypeIndex{}.withCategory(TypeCategory::Void), 0, memberFunctionCallNode.called_from(), CVQualifier::None, ReferenceQualifier::None);
			if (lambda.return_type().has_value()) {
				const auto& ret_type = lambda.return_type()->as<TypeSpecifierNode>();
				return_type_node = ret_type;
				call_op.return_type_index = ret_type.type_index();
				call_op.return_size_in_bits = SizeInBits{static_cast<int>(ret_type.size_in_bits())};
			} else {
				// Per C++20 §7.5.5.1, a lambda with no return statements deduces void
				call_op.return_type_index = nativeTypeIndex(TypeCategory::Void);
				call_op.return_size_in_bits = SizeInBits{0};
			}

			// Build TypeSpecifierNodes for parameters (needed for mangling)
			// For generic lambdas, we need to deduce auto parameters from arguments
			std::vector<TypeSpecifierNode> param_types;
			std::vector<TypeSpecifierNode> deduced_param_types; // For generic lambdas

			if (is_generic) {
				// First, collect argument types
				std::vector<TypeSpecifierNode> arg_types;
				memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
					const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(arg_expr)) {
						const auto& identifier = std::get<IdentifierNode>(arg_expr);
						const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
						if (symbol.has_value()) {
							const DeclarationNode* decl = get_decl_from_symbol(*symbol);
							if (decl) {
								TypeSpecifierNode type_node = decl->type_node().as<TypeSpecifierNode>();
								// Resolve auto type from lambda initializer if available
								if (isPlaceholderAutoType(type_node.type())) {
									if (auto deduced = deduceLambdaClosureType(*symbol, decl->identifier_token())) {
										type_node = *deduced;
									}
								}
								arg_types.push_back(type_node);
							} else {
								// Default to int
								arg_types.push_back(TypeSpecifierNode(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None));
							}
						} else {
							arg_types.push_back(TypeSpecifierNode(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None));
						}
					} else if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
						arg_types.push_back(TypeSpecifierNode(TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None));
					} else if (const auto* literal = std::get_if<NumericLiteralNode>(&arg_expr)) {
						arg_types.push_back(TypeSpecifierNode(literal->type(), TypeQualifier::None,
															  static_cast<unsigned char>(literal->sizeInBits()), Token{}, CVQualifier::None));
					} else {
						// For complex expressions, evaluate and get type
						ExprResult operand_result = visitExpressionNode(arg_expr);
						arg_types.push_back(TypeSpecifierNode(
							operand_result.typeEnum(),
							TypeQualifier::None,
							static_cast<unsigned char>(operand_result.size_in_bits.value),
							Token{}, CVQualifier::None));
					}
				});

				// Now build param_types with deduced types for auto parameters
				size_t arg_idx = 0;
				for (const auto& param_node : lambda.parameters()) {
					if (param_node.is<DeclarationNode>()) {
						const auto& param_decl = param_node.as<DeclarationNode>();
						const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();

						if (isPlaceholderAutoType(param_type.type()) && arg_idx < arg_types.size()) {
							// Deduce type from argument, preserving reference flags from auto&& parameter
							TypeSpecifierNode deduced_type = arg_types[arg_idx];
							// Copy reference flags from auto parameter (e.g., auto&& -> T&&)
							deduced_type.set_reference_qualifier(param_type.reference_qualifier());
							deduced_param_types.push_back(deduced_type);
							param_types.push_back(deduced_type);
						} else {
							param_types.push_back(param_type);
						}
					}
					arg_idx++;
				}

				// Build instantiation key and request instantiation
				std::string instantiation_key = std::to_string(lambda.lambda_id());
				for (const auto& deduced : deduced_param_types) {
					instantiation_key += "_" + std::to_string(static_cast<int>(deduced.type())) +
										 "_" + std::to_string(deduced.size_in_bits());
				}

				// Check if we've already scheduled this instantiation
				if (generated_generic_lambda_instantiations_.find(instantiation_key) ==
					generated_generic_lambda_instantiations_.end()) {
					// Schedule this instantiation
					GenericLambdaInstantiation inst;
					inst.lambda_id = lambda.lambda_id();
					inst.instantiation_key = StringTable::getOrInternStringHandle(instantiation_key);
					for (size_t i = 0; i < auto_param_indices.size() && i < deduced_param_types.size(); ++i) {
						inst.deduced_types.push_back({auto_param_indices[i], deduced_param_types[i]});
					}
					pending_generic_lambda_instantiations_.push_back(std::move(inst));
					generated_generic_lambda_instantiations_.insert(instantiation_key);

					// Also store deduced types in the LambdaInfo for generation
					// Find the LambdaInfo for this lambda
					for (auto& lambda_info : collected_lambdas_) {
						if (lambda_info.lambda_id == lambda.lambda_id()) {
							// Store deduced types (full TypeSpecifierNode to preserve struct info and reference flags)
							for (size_t i = 0; i < auto_param_indices.size() && i < deduced_param_types.size(); ++i) {
								lambda_info.setDeducedType(
									auto_param_indices[i],
									deduced_param_types[i]);
							}
							break;
						}
					}
				}
			} else {
				// Non-generic: use parameter types directly
				for (const auto& param_node : lambda.parameters()) {
					if (param_node.is<DeclarationNode>()) {
						const auto& param_decl = param_node.as<DeclarationNode>();
						const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
						param_types.push_back(param_type);
					}
				}
			}

			// Generate mangled name for __invoke (matching how it's defined in generateLambdaInvokeFunction)
			std::string_view mangled = generateMangledNameForCall(
				StringTable::getStringView(invoke_name),
				return_type_node,
				param_types,
				false, // not variadic
				"", // not a member function
				{}, // namespace_path
				false // free function, never const
			);

			call_op.function_name = StringTable::getOrInternStringHandle(mangled);
			call_op.is_member_function = false;
			call_op.is_variadic = false; // Lambdas cannot be variadic in C++20

			// Add arguments
			memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
				const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
				ExprResult argument_result = visitExpressionNode(arg_expr);
				if (std::holds_alternative<IdentifierNode>(arg_expr)) {
					const auto& identifier = std::get<IdentifierNode>(arg_expr);
					const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
					const auto& decl_node = symbol->as<DeclarationNode>();
					const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
					// Convert to TypedValue
					TypedValue arg;
					arg.setType(type_node.type());
					arg.ir_type = toIrType(type_node.type());
					arg.size_in_bits = SizeInBits{static_cast<int>(type_node.size_in_bits())};
					arg.value = StringTable::getOrInternStringHandle(identifier.name());
					call_op.args.push_back(arg);
				} else {
					// Convert argumentIrOperands to TypedValue
					TypedValue arg = toTypedValue(argument_result);
					call_op.args.push_back(arg);
				}
			});

			// Capture return type info before moving call_op (use-after-move is UB)
			TypeCategory lambda_return_type = call_op.returnType();
			int lambda_return_size = call_op.return_size_in_bits.value;

			// Add the function call instruction with typed payload
			ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberFunctionCallNode.called_from()));

			// Return the result with actual return type from lambda
			return makeExprResult(nativeTypeIndex(lambda_return_type), SizeInBits{static_cast<int>(lambda_return_size)}, IrOperand{ret_var}, PointerDepth{}, ValueStorage::ContainsData);
		}

		if (!std::holds_alternative<StringHandle>(lambda_result.value) || !lambda_result.type_index.is_valid()) {
			throw InternalError("Immediate capturing lambda did not produce a named closure object");
		}

		immediate_lambda_object_name = std::get<StringHandle>(lambda_result.value);
		immediate_lambda_object_type = TypeSpecifierNode(
			lambda_result.type_index.withCategory(TypeCategory::Struct),
			static_cast<int>(lambda_result.size_in_bits.value),
			memberFunctionCallNode.called_from(),
			CVQualifier::None,
			ReferenceQualifier::None);

		// For capturing lambdas, continue into the regular member function call path
		// using the generated closure variable as the object.
	}

	// Regular member function call on an expression
	// Get the object's type
	std::string_view object_name;
	const DeclarationNode* object_decl = nullptr;
	TypeSpecifierNode object_type;
	const ExpressionNode* object_expr = nullptr;
	const StringHandle call_operator_name = StringTable::getOrInternStringHandle("operator()");
	auto tryEmitFunctionPointerOperatorCall = [&](const TypeSpecifierNode& function_pointer_type) -> std::optional<ExprResult> {
		const DeclarationNode& called_decl = memberFunctionCallNode.function_declaration().decl_node();
		if (called_decl.identifier_token().handle() != call_operator_name) {
			return std::nullopt;
		}
		if (!function_pointer_type.is_function_pointer() && !function_pointer_type.has_function_signature()) {
			return std::nullopt;
		}
		if (!function_pointer_type.has_function_signature()) {
			throw InternalError("Function pointer call target missing function signature");
		}
		if (!object_expr) {
			throw InternalError("Function pointer operator() call missing expression object");
		}

		ExprResult func_ptr_result = visitExpressionNode(*object_expr);
		std::variant<StringHandle, TempVar> function_pointer;
		if (std::holds_alternative<TempVar>(func_ptr_result.value)) {
			function_pointer = std::get<TempVar>(func_ptr_result.value);
		} else if (std::holds_alternative<StringHandle>(func_ptr_result.value)) {
			function_pointer = std::get<StringHandle>(func_ptr_result.value);
		} else {
			throw InternalError("Function pointer call target did not produce a valid indirect call operand");
		}

		TempVar ret_var = var_counter.next();
		std::vector<TypedValue> arguments;
		memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
			ExprResult argument_result = visitExpressionNode(argument.as<ExpressionNode>());
			arguments.push_back(makeTypedValue(argument_result.typeEnum(), argument_result.size_in_bits, toIrValue(argument_result.value)));
		});

		IndirectCallOp op{
			.result = ret_var,
			.function_pointer = std::move(function_pointer),
			.arguments = std::move(arguments)};
		ir_.addInstruction(IrInstruction(IrOpcode::IndirectCall, std::move(op), memberFunctionCallNode.called_from()));

		const FunctionSignature& sig = function_pointer_type.function_signature();
		TypeCategory ret_type = sig.returnType();
		int ret_size = (ret_type == TypeCategory::Void) ? 0 : get_type_size_bits(ret_type);
		return makeExprResult(nativeTypeIndex(ret_type), SizeInBits{static_cast<int>(ret_size)}, IrOperand{ret_var}, PointerDepth{}, ValueStorage::ContainsData);
	};

	if (immediate_lambda_object_name.has_value() && immediate_lambda_object_type.has_value()) {
		object_name = StringTable::getStringView(*immediate_lambda_object_name);
		object_type = *immediate_lambda_object_type;
	} else {
		// The object must be an ExpressionNode for regular member function calls.
		// Immediate capturing lambdas synthesize a closure object above and do not
		// need the original AST node to be wrapped in an ExpressionNode.
		if (!object_node.is<ExpressionNode>()) {
			throw InternalError("Member function call object must be an ExpressionNode");
			return ExprResult{};
		}

		object_expr = &object_node.as<ExpressionNode>();
	}

	if (object_expr && std::holds_alternative<IdentifierNode>(*object_expr)) {
		const IdentifierNode& object_ident = std::get<IdentifierNode>(*object_expr);
		object_name = object_ident.name();

		// Look up the object in both local and global symbol tables
		std::optional<ASTNode> symbol = lookupSymbol(object_name);
		if (symbol.has_value()) {
			// Use helper to get DeclarationNode from either DeclarationNode or VariableDeclarationNode
			object_decl = get_decl_from_symbol(*symbol);
			if (object_decl) {
				object_type = object_decl->type_node().as<TypeSpecifierNode>();

				// If the type is 'auto', deduce the actual closure type from lambda initializer
				if (isPlaceholderAutoType(object_type.type())) {
					if (auto deduced = deduceLambdaClosureType(*symbol, object_decl->identifier_token())) {
						object_type = *deduced;
					} else if (current_lambda_context_.isActive() && object_type.is_rvalue_reference()) {
						// For auto&& parameters inside lambdas (recursive lambda pattern),
						// assume the parameter has the closure type of the current lambda.
						// This handles: auto factorial = [](auto&& self, int n) { ... self(self, n-1); }
						// where self's type is deduced to __lambda_N&& when called
						auto type_it = getTypesByNameMap().find(current_lambda_context_.closure_type);
						if (type_it != getTypesByNameMap().end()) {
							const TypeInfo* closure_type = type_it->second;
							int closure_size = closure_type->getStructInfo()
												   ? closure_type->getStructInfo()->sizeInBits().value
												   : 64;
							object_type = TypeSpecifierNode(
								closure_type->type_index_.withCategory(TypeCategory::Struct),
								closure_size,
								object_decl->identifier_token(),
								CVQualifier::None,
								ReferenceQualifier::None);
							// Preserve rvalue reference flag
							object_type.set_reference_qualifier(ReferenceQualifier::RValueReference);
						}
					}
				}
			}
		}
	} else if (object_expr && std::holds_alternative<UnaryOperatorNode>(*object_expr)) {
		// Handle dereference operator (from ptr->member transformation)
		const UnaryOperatorNode& unary_op = std::get<UnaryOperatorNode>(*object_expr);
		if (unary_op.op() == "*") {
			// This is a dereference - get the pointer operand
			const ASTNode& operand_node = unary_op.get_operand();
			if (operand_node.is<ExpressionNode>()) {
				const ExpressionNode& operand_expr = operand_node.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(operand_expr)) {
					const IdentifierNode& ptr_ident = std::get<IdentifierNode>(operand_expr);
					object_name = ptr_ident.name();

					// Look up the pointer in both local and global symbol tables
					const std::optional<ASTNode> symbol = lookupSymbol(object_name);
					if (symbol.has_value()) {
						const DeclarationNode* ptr_decl = get_decl_from_symbol(*symbol);
						if (ptr_decl) {
							object_decl = ptr_decl;
							// Get the pointer type and remove one level of indirection
							TypeSpecifierNode ptr_type = ptr_decl->type_node().as<TypeSpecifierNode>();
							if (ptr_type.pointer_levels().size() > 0) {
								object_type = ptr_type;
								object_type.remove_pointer_level();
							}
						}
					}
				}
			}
		}
	} else if (object_expr && std::holds_alternative<FunctionCallNode>(*object_expr)) {
		// Handle function call returning a struct or function pointer.
		const FunctionCallNode& func_call = std::get<FunctionCallNode>(*object_expr);
		const DeclarationNode& decl = func_call.function_declaration();
		if (decl.type_node().is<TypeSpecifierNode>()) {
			TypeSpecifierNode ret_type = decl.type_node().as<TypeSpecifierNode>();
			if (isIrStructType(toIrType(ret_type.type())) ||
				ret_type.is_function_pointer() ||
				ret_type.has_function_signature()) {
				object_type = ret_type;
				// object_name remains empty; expression will be evaluated when needed
			}
		}
	} else if (object_expr && std::holds_alternative<MemberFunctionCallNode>(*object_expr)) {
		// Handle member function call returning a struct or function pointer.
		const MemberFunctionCallNode& mem_call = std::get<MemberFunctionCallNode>(*object_expr);
		const DeclarationNode& decl = mem_call.function_declaration().decl_node();
		if (decl.type_node().is<TypeSpecifierNode>()) {
			TypeSpecifierNode ret_type = decl.type_node().as<TypeSpecifierNode>();
			if (isIrStructType(toIrType(ret_type.type())) ||
				ret_type.is_function_pointer() ||
				ret_type.has_function_signature()) {
				object_type = ret_type;
				// object_name remains empty; expression will be evaluated when needed
			}
		}
	} else if (object_expr && std::holds_alternative<ConstructorCallNode>(*object_expr)) {
		// Handle temporary constructed via brace/paren-init (e.g., Counter<int,int,int>{}.size())
		// Set object_type from the constructor's TypeSpecifierNode so the struct-type check below
		// passes and the object_name.empty() path below correctly evaluates the ConstructorCallNode
		// to get an addressable TempVar for the 'this' pointer.
		const ConstructorCallNode& ctor_call = std::get<ConstructorCallNode>(*object_expr);
		if (ctor_call.type_node().is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& ctor_type = ctor_call.type_node().as<TypeSpecifierNode>();
			if (isIrStructType(toIrType(ctor_type.type()))) {
				object_type = ctor_type;
				// object_name remains empty; expression will be evaluated when needed
			}
		}
	} else if (object_expr && std::holds_alternative<MemberAccessNode>(*object_expr)) {
		// Handle member access for function pointer calls
		// This handles both simple cases like "this->callback" and nested cases like "o.inner.callback"
		// When we see o.inner.callback():
		// - object_expr is o.inner (a MemberAccessNode)
		// - func_name (from function_declaration) is "callback"
		// We need to resolve the type of o.inner to get Inner, then check if callback is a function pointer member

		const MemberAccessNode& member_access = std::get<MemberAccessNode>(*object_expr);
		const FunctionDeclarationNode& check_func_decl = memberFunctionCallNode.function_declaration();
		std::string_view called_func_name = check_func_decl.decl_node().identifier_token().value();
		bool resolved_member_object_type = false;

		// Try to resolve the type of the object (e.g., o.inner resolves to type Inner)
		const StructTypeInfo* resolved_struct_info = nullptr;
		const StructMember* resolved_member = nullptr;
		if (resolveMemberAccessType(member_access, resolved_struct_info, resolved_member)) {
			if (resolved_member && resolved_member->memberType() == TypeCategory::FunctionPointer) {
				ExprResult func_ptr_result = visitExpressionNode(*object_expr);
				std::variant<StringHandle, TempVar> function_pointer;
				if (std::holds_alternative<TempVar>(func_ptr_result.value)) {
					function_pointer = std::get<TempVar>(func_ptr_result.value);
				} else if (std::holds_alternative<StringHandle>(func_ptr_result.value)) {
					function_pointer = std::get<StringHandle>(func_ptr_result.value);
				} else {
					throw InternalError("Function pointer member access did not produce a valid call target");
				}

				TempVar ret_var = var_counter.next();
				std::vector<TypedValue> arguments;
				memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
					ExprResult argument_result = visitExpressionNode(argument.as<ExpressionNode>());
					arguments.push_back(makeTypedValue(argument_result.typeEnum(), argument_result.size_in_bits, toIrValue(argument_result.value)));
				});

				IndirectCallOp op{
					.result = ret_var,
					.function_pointer = std::move(function_pointer),
					.arguments = std::move(arguments)};
				ir_.addInstruction(IrInstruction(IrOpcode::IndirectCall, std::move(op), memberFunctionCallNode.called_from()));

				if (!resolved_member->function_signature) {
					throw InternalError("Function pointer member missing function_signature for indirect call return type");
				}
				TypeCategory ret_type = resolved_member->function_signature->returnType();
				int ret_size = (ret_type == TypeCategory::Void) ? 0 : get_type_size_bits(ret_type);
				return makeExprResult(nativeTypeIndex(ret_type), SizeInBits{static_cast<int>(ret_size)}, IrOperand{ret_var}, PointerDepth{}, ValueStorage::ContainsData);
			}

			// We resolved the member access - now check if it's a struct type
			if (resolved_member && isIrStructType(toIrType(resolved_member->memberType()))) {
				// Get the struct info for the member's type
				if (const TypeInfo* member_type_info = tryGetTypeInfo(resolved_member->type_index)) {
					const StructTypeInfo* member_struct_info = member_type_info->getStructInfo();
					if (member_struct_info) {
						// Look for the called function name in this struct's members
						StringHandle func_name_handle = StringTable::getOrInternStringHandle(called_func_name);
						for (const auto& member : member_struct_info->members) {
							if (member.getName() == func_name_handle && member.type_index.category() == TypeCategory::FunctionPointer) {
								// Found a function pointer member! Generate indirect call
								TempVar ret_var = var_counter.next();

								// Generate member access chain for o.inner.callback
								// First get o.inner
								ExprResult base_result = visitExpressionNode(*object_expr);
								if (!std::holds_alternative<TempVar>(base_result.value)) {
									throw InternalError("Function pointer member base expression did not produce a TempVar");
								}
								TempVar base_temp = std::get<TempVar>(base_result.value);

								// Now access the callback member from that
								TempVar func_ptr_temp = var_counter.next();
								MemberLoadOp member_load;
								member_load.result.value = func_ptr_temp;
								member_load.result.setType(TypeCategory::FunctionPointer);
								member_load.result.ir_type = IrType::FunctionPointer;
								member_load.result.size_in_bits = SizeInBits{static_cast<int>(member.size * 8)};
								member_load.object = base_temp;
								member_load.member_name = func_name_handle;
								member_load.offset = static_cast<int>(member.offset);
								member_load.ref_qualifier = ((member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
								member_load.struct_type_info = member_type_info; // MemberLoadOp expects TypeInfo*
								member_load.is_pointer_to_member = member_access.is_arrow();

								ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

								// Build arguments for the indirect call
								std::vector<TypedValue> arguments;
								memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
									ExprResult argument_result = visitExpressionNode(argument.as<ExpressionNode>());
									arguments.push_back(makeTypedValue(argument_result.typeEnum(), argument_result.size_in_bits, toIrValue(argument_result.value)));
								});

								IndirectCallOp op{
									.result = ret_var,
									.function_pointer = func_ptr_temp,
									.arguments = std::move(arguments)};
								ir_.addInstruction(IrInstruction(IrOpcode::IndirectCall, std::move(op), memberFunctionCallNode.called_from()));

								// Use the function pointer's stored return type
								if (!member.function_signature) {
									throw InternalError("Function pointer member missing function_signature for indirect call return type");
								}
								TypeCategory ret_type = member.function_signature->returnType();
								int ret_size = (ret_type == TypeCategory::Void) ? 0 : get_type_size_bits(ret_type);
								return makeExprResult(nativeTypeIndex(ret_type), SizeInBits{static_cast<int>(ret_size)}, IrOperand{ret_var}, PointerDepth{}, ValueStorage::ContainsData);
							}
						}

						// Not a function pointer member - set object_type for regular member function lookup
						object_type = TypeSpecifierNode(resolved_member->type_index.withCategory(TypeCategory::Struct),
														resolved_member->size * 8, Token(), CVQualifier::None, ReferenceQualifier::None); // size in bits
						resolved_member_object_type = true;
					}
				}
			}
		}

		(void)resolved_member_object_type;
	}

	// For immediate lambda invocation, object_decl can be nullptr
	// In that case, we still need object_type to be set correctly

	std::string_view current_struct_name_sv;
	std::string_view base_template_name;
	if (current_struct_name_.isValid()) {
		current_struct_name_sv = StringTable::getStringView(current_struct_name_);
		base_template_name = extractBaseTemplateName(current_struct_name_sv);
	}

	auto isSameClassAsCurrentInstantiation = [&](std::string_view candidate_name) {
		if (candidate_name.empty() || current_struct_name_sv.empty()) {
			return false;
		}

		if (candidate_name == current_struct_name_sv) {
			return true;
		}

		if (base_template_name.empty()) {
			return false;
		}

		if (candidate_name == base_template_name) {
			return true;
		}

		size_t sep_pos = base_template_name.rfind("::");
		return sep_pos != std::string_view::npos &&
			   candidate_name == base_template_name.substr(sep_pos + 2);
	};

	// Injected-class-name inside an instantiated member body can leave the object
	// expression typed as the template pattern (e.g. `Box`) instead of the current
	// instantiation (e.g. `Box$hash`). Remap same-class member-call receivers to
	// the active instantiated owner so later overload/mangling picks the concrete
	// member definition instead of emitting stale `Box::member` references.
	if (object_type.type_index().is_valid() &&
		memberFunctionCallNode.function_declaration().is_member_function() &&
		isSameClassAsCurrentInstantiation(memberFunctionCallNode.function_declaration().parent_struct_name())) {
		if (const TypeInfo* object_type_info = tryGetTypeInfo(object_type.type_index())) {
			if (object_type_info->isStruct() &&
				isSameClassAsCurrentInstantiation(StringTable::getStringView(object_type_info->name()))) {
				auto current_struct_it = getTypesByNameMap().find(current_struct_name_);
				if (current_struct_it != getTypesByNameMap().end() &&
					current_struct_it->second->isStruct()) {
					const TypeInfo* current_struct_type_info = current_struct_it->second;
					object_type.set_type_index(current_struct_type_info->type_index_.withCategory(TypeCategory::Struct));
					object_type.set_size_in_bits(current_struct_type_info->sizeInBits());
				}
			}
		}
	}

	// Special case: Handle namespace-qualified function calls that were incorrectly parsed as member function calls
	// This can happen when std::function() is parsed and the object is a namespace identifier
	if (object_expr && std::holds_alternative<QualifiedIdentifierNode>(*object_expr)) {
		// This is a namespace-qualified function call, not a member function call
		// Treat it as a regular function call instead
		return convertMemberCallToFunctionCall(memberFunctionCallNode, context);
	}

	if (auto function_pointer_call = tryEmitFunctionPointerOperatorCall(object_type)) {
		return *function_pointer_call;
	}

	// Verify this is a struct type BEFORE checking other cases
	// If object_type is not a struct, this might be a misparsed namespace-qualified function call
	// Note: Template instantiations may be registered as Type::UserDefined but carry full struct info
	if (!isIrStructType(toIrType(object_type.type()))) {
		// The object is not a struct - this might be a namespace identifier or other non-struct type
		// Treat this as a regular function call instead of a member function call
		return convertMemberCallToFunctionCall(memberFunctionCallNode, context);
	}

	// Get the function declaration directly from the node (no need to look it up)
	const FunctionDeclarationNode& func_decl = memberFunctionCallNode.function_declaration();
	const DeclarationNode& func_decl_node = func_decl.decl_node();

	// consteval enforcement: every call to a consteval function is an immediate invocation
	// and must be a constant expression (C++20 [dcl.consteval]).  Try compile-time evaluation
	// first; only throw if the call genuinely cannot be constant-evaluated.
	//
	// Strategy: first try the MemberFunctionCallNode path, which correctly resolves 'this'
	// member bindings when the object is constexpr.  If that fails (e.g., non-constexpr
	// object), fall back to a synthetic FunctionCallNode (stripping the object), which works
	// for consteval members that don't access 'this' state.
	if (func_decl.is_consteval()) {
		std::string_view func_name_sv = func_decl_node.identifier_token().value();
		extern SymbolTable gSymbolTable;
		ConstExpr::EvaluationContext ctx(global_symbol_table_ ? *global_symbol_table_ : gSymbolTable);
		ctx.global_symbols = global_symbol_table_ ? global_symbol_table_ : &gSymbolTable;
		ctx.parser = parser_;
		// Step 1: Try evaluation via the member-function path (handles constexpr objects
		// with 'this' access correctly).
		auto member_eval_node = ASTNode::emplace_node<ExpressionNode>(memberFunctionCallNode);
		auto eval_result = ConstExpr::Evaluator::evaluate(member_eval_node, ctx);
		if (!eval_result.success()) {
			// Step 2: Fall back to a synthetic FunctionCallNode (no object-constexpr
			// requirement).  Works for consteval members that don't read 'this' state
			// (e.g., `Calc c; c.triple(14)` where triple doesn't use any member).
			// Preserve the first error so that if the fallback also fails we report
			// the more specific member-path diagnostic (not a "can't see member
			// variables" error from the fallback).
			auto first_error = eval_result;
			ChunkedVector<ASTNode> args_copy;
			memberFunctionCallNode.arguments().visit([&](ASTNode arg) {
				args_copy.push_back(arg);
			});
			FunctionCallNode synth_call(func_decl_node, std::move(args_copy), memberFunctionCallNode.called_from());
			auto eval_call_node = ASTNode::emplace_node<ExpressionNode>(synth_call);
			eval_result = ConstExpr::Evaluator::evaluate(eval_call_node, ctx);
			if (!eval_result.success()) {
				eval_result = first_error;
			}
		}
		if (!eval_result.success()) {
			throw CompileError("call to consteval function '" + std::string(func_name_sv) +
							   "' cannot be used in a non-constant context: " + eval_result.error_message);
		}
		// Materialize the constant result — reuse the same scalar/struct helpers as the direct path.
		const TypeSpecifierNode& ret_spec =
			func_decl_node.type_node().as<TypeSpecifierNode>();
		const TypeCategory ret_type = ret_spec.type();
		const int ret_bits_raw = static_cast<int>(ret_spec.size_in_bits());
		const SizeInBits ret_size{ret_bits_raw != 0 ? ret_bits_raw : static_cast<int>(get_type_size_bits(ret_type))};

		if (ret_type == TypeCategory::Float) {
			float fval = static_cast<float>(eval_result.as_double());
			uint32_t fbits;
			std::memcpy(&fbits, &fval, sizeof(float));
			return makeExprResult(nativeTypeIndex(ret_type), SizeInBits{32}, IrOperand{static_cast<unsigned long long>(fbits)}, PointerDepth{}, ValueStorage::ContainsData);
		}
		if (ret_type == TypeCategory::Double || ret_type == TypeCategory::LongDouble) {
			double dval = eval_result.as_double();
			unsigned long long dbits;
			std::memcpy(&dbits, &dval, sizeof(double));
			return makeExprResult(nativeTypeIndex(ret_type), SizeInBits{64}, IrOperand{dbits}, PointerDepth{}, ValueStorage::ContainsData);
		}
		if (!eval_result.object_member_bindings.empty()) {
			auto agg = materializeConstevalAggregateResult(
				eval_result, ret_spec, ret_type, ret_size,
				memberFunctionCallNode.called_from());
			if (agg.category() != TypeCategory::Void)
				return agg;
		}
		return makeExprResult(nativeTypeIndex(ret_type), ret_size, IrOperand{evalResultScalarToRaw(eval_result)}, PointerDepth{}, ValueStorage::ContainsData);
	}
	auto getParamDecl = [](const ASTNode& param_node) -> const DeclarationNode* {
		if (param_node.is<DeclarationNode>()) {
			return &param_node.as<DeclarationNode>();
		}
		if (param_node.is<VariableDeclarationNode>()) {
			return &param_node.as<VariableDeclarationNode>().declaration();
		}
		return nullptr;
	};
	auto sameTypeSpec = [](const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) {
		return lhs.type() == rhs.type() && lhs.type_index() == rhs.type_index() && lhs.pointer_depth() == rhs.pointer_depth() && lhs.reference_qualifier() == rhs.reference_qualifier() && lhs.cv_qualifier() == rhs.cv_qualifier();
	};
	auto matchesSelectedMemberDecl = [&](const FunctionDeclarationNode& candidate) {
		if (&candidate == &func_decl || &candidate.decl_node() == &func_decl_node) {
			return true;
		}
		if (candidate.decl_node().identifier_token().value() != func_decl_node.identifier_token().value()) {
			return false;
		}
		if (candidate.has_mangled_name() && func_decl.has_mangled_name()) {
			return candidate.mangled_name() == func_decl.mangled_name();
		}
		const auto& lhs_params = candidate.parameter_nodes();
		const auto& rhs_params = func_decl.parameter_nodes();
		if (lhs_params.size() != rhs_params.size()) {
			return false;
		}
		for (size_t i = 0; i < lhs_params.size(); ++i) {
			const DeclarationNode* lhs_decl = getParamDecl(lhs_params[i]);
			const DeclarationNode* rhs_decl = getParamDecl(rhs_params[i]);
			if (!lhs_decl || !rhs_decl) {
				return false;
			}
			if (lhs_decl->is_parameter_pack() != rhs_decl->is_parameter_pack()) {
				return false;
			}
			if (!sameTypeSpec(lhs_decl->type_node().as<TypeSpecifierNode>(), rhs_decl->type_node().as<TypeSpecifierNode>())) {
				return false;
			}
		}
		return true;
	};
	const size_t explicit_arg_count = memberFunctionCallNode.arguments().size();
	auto isViableMemberOverload = [&](const FunctionDeclarationNode& candidate) {
		const auto& params = candidate.parameter_nodes();
		if (explicit_arg_count > params.size()) {
			if (params.empty()) {
				return false;
			}
			const DeclarationNode* last_param = getParamDecl(params.back());
			return last_param && last_param->is_parameter_pack();
		}
		for (size_t i = explicit_arg_count; i < params.size(); ++i) {
			const DeclarationNode* param_decl = getParamDecl(params[i]);
			if (!param_decl) {
				return false;
			}
			if (param_decl->is_parameter_pack()) {
				return true;
			}
			if (!param_decl->has_default_value()) {
				return false;
			}
		}
		return true;
	};

	// Check if this is a virtual function call
	// Look up the struct type to check if the function is virtual
	bool is_virtual_call = false;
	int vtable_index = -1;

	const StructMemberFunction* called_member_func = nullptr;
	const StructTypeInfo* struct_info = nullptr;
	const FunctionDeclarationNode* materialized_member_func_decl = nullptr;

	if (const TypeInfo* type_info = tryGetTypeInfo(object_type.type_index())) {
		struct_info = type_info->getStructInfo();

		if (struct_info) {
			// Find the member function in the struct
			std::string_view func_name = func_decl_node.identifier_token().value();
			StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
			for (const auto& member_func : struct_info->member_functions) {
				if (member_func.getName() == func_name_handle &&
					member_func.function_decl.is<FunctionDeclarationNode>() &&
					matchesSelectedMemberDecl(member_func.function_decl.as<FunctionDeclarationNode>())) {
					called_member_func = &member_func;
					if (member_func.is_virtual) {
						is_virtual_call = true;
						vtable_index = member_func.vtable_index;
					}
					break;
				}
			}
			if (!called_member_func) {
				for (const auto& member_func : struct_info->member_functions) {
					if (member_func.getName() == func_name_handle &&
						member_func.function_decl.is<FunctionDeclarationNode>() &&
						isViableMemberOverload(member_func.function_decl.as<FunctionDeclarationNode>())) {
						called_member_func = &member_func;
						if (member_func.is_virtual) {
							is_virtual_call = true;
							vtable_index = member_func.vtable_index;
						}
						break;
					}
				}
			}

			// If not found in the current class, search base classes
			const StructTypeInfo* declaring_struct = struct_info;
			if (!called_member_func && !struct_info->base_classes.empty()) {
				auto searchBaseClasses = [&](auto&& self, const StructTypeInfo* current_struct) -> void {
					for (const auto& base_spec : current_struct->base_classes) {
						if (const TypeInfo* base_type_info = tryGetTypeInfo(base_spec.type_index)) {
							if (base_type_info->isStruct()) {
								const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();
								if (base_struct_info) {
									// Check member functions in base class
									for (const auto& member_func : base_struct_info->member_functions) {
										if (member_func.getName() == func_name_handle &&
											member_func.function_decl.is<FunctionDeclarationNode>() &&
											matchesSelectedMemberDecl(member_func.function_decl.as<FunctionDeclarationNode>())) {
											called_member_func = &member_func;
											declaring_struct = base_struct_info; // Update to use base class name
											if (member_func.is_virtual) {
												is_virtual_call = true;
												vtable_index = member_func.vtable_index;
											}
											return; // Stop searching once found
										}
									}
									for (const auto& member_func : base_struct_info->member_functions) {
										if (member_func.getName() == func_name_handle &&
											member_func.function_decl.is<FunctionDeclarationNode>() &&
											isViableMemberOverload(member_func.function_decl.as<FunctionDeclarationNode>())) {
											called_member_func = &member_func;
											declaring_struct = base_struct_info;
											if (member_func.is_virtual) {
												is_virtual_call = true;
												vtable_index = member_func.vtable_index;
											}
											return;
										}
									}
									// Recursively search base classes of this base class
									if (!called_member_func) {
										self(self, base_struct_info);
									}
								}
							}
						}
					}
				};
				searchBaseClasses(searchBaseClasses, struct_info);
			}

			auto buildNamespaceStack = [](std::string_view qualified_name, std::vector<std::string>& out) {
				size_t ns_end = qualified_name.rfind("::");
				if (ns_end == std::string_view::npos)
					return;
				std::string_view ns_part = qualified_name.substr(0, ns_end);
				size_t start = 0;
				while (start < ns_part.size()) {
					size_t pos = ns_part.find("::", start);
					if (pos == std::string_view::npos) {
						out.emplace_back(ns_part.substr(start));
						break;
					}
					out.emplace_back(ns_part.substr(start, pos - start));
					start = pos + 2;
				}
			};

			auto instantiateLazySelectedMember = [&](StringHandle owner_name, StringHandle member_name, bool is_const_member) {
				if (!parser_ ||
					!type_info->isTemplateInstantiation() ||
					!LazyMemberInstantiationRegistry::getInstance().needsInstantiation(owner_name, member_name, is_const_member)) {
					return;
				}
				auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(
					owner_name,
					member_name,
					is_const_member);
				if (!lazy_info_opt.has_value()) {
					return;
				}
				auto instantiated_func = parser_->instantiateLazyMemberFunction(*lazy_info_opt);
				LazyMemberInstantiationRegistry::getInstance().markInstantiated(
					owner_name,
					member_name,
					lazy_info_opt->identity.is_const_method);
				if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
					materialized_member_func_decl = &instantiated_func->as<FunctionDeclarationNode>();
					DeferredMemberFunctionInfo deferred_info;
					deferred_info.struct_name = owner_name;
					deferred_info.function_node = *instantiated_func;
					buildNamespaceStack(StringTable::getStringView(owner_name), deferred_info.namespace_stack);
					deferred_member_functions_.push_back(std::move(deferred_info));
				}
			};

			if (!func_decl.get_definition().has_value()) {
				instantiateLazySelectedMember(
					type_info->name(),
					func_decl_node.identifier_token().handle(),
					func_decl.is_const_member_function());
			}

			if (parser_ &&
				called_member_func &&
				called_member_func->function_decl.is<FunctionDeclarationNode>()) {
				const auto& selected_member_func = called_member_func->function_decl.as<FunctionDeclarationNode>();
				if (!selected_member_func.get_definition().has_value()) {
					instantiateLazySelectedMember(
						type_info->name(),
						called_member_func->getName(),
						called_member_func->is_const());
				}
			}

			// Use declaring_struct instead of struct_info for mangled name generation
			// This ensures we use the correct class name where the function is declared
			struct_info = declaring_struct;

			// If not found as member function, check if it's a function pointer data member
			// Use findMemberRecursive to also search base classes for inherited function pointer members
			if (!called_member_func) {
				auto fp_member = struct_info->findMemberRecursive(func_name_handle);
				if (fp_member.has_value() && fp_member->memberType() == TypeCategory::FunctionPointer) {
					const auto& member = *fp_member;
					// This is a call through a function pointer member!
					// Generate an indirect call instead of a member function call

					TempVar ret_var = var_counter.next();
					// Get the function pointer member
					// We need to generate member access to get the pointer value
					TempVar func_ptr_temp = var_counter.next();

					// Generate member access IR to load the function pointer
					MemberLoadOp member_load;
					member_load.result.value = func_ptr_temp;
					member_load.result.setType(member.type_index.category());
					member_load.result.size_in_bits = SizeInBits{static_cast<int>(member.size * 8)}; // Convert bytes to bits

					// Add object operand
					if (object_name.empty()) {
						// Object is not a named variable - evaluate the expression to get a TempVar
						ExprResult obj_result = visitExpressionNode(*object_expr);
						if (!std::holds_alternative<TempVar>(obj_result.value)) {
							throw InternalError("Function pointer member call: expression did not produce a TempVar");
						}
						member_load.object = std::get<TempVar>(obj_result.value);
					} else {
						member_load.object = StringTable::getOrInternStringHandle(object_name);
					}

					member_load.member_name = StringTable::getOrInternStringHandle(func_name); // Member name
					member_load.offset = static_cast<int>(member.offset); // Member offset
					member_load.ref_qualifier = ((member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
					member_load.struct_type_info = nullptr; // Not used downstream; consistent with all other MemberLoadOp sites
					member_load.is_pointer_to_member = object_decl &&
													   (object_decl->type_node().as<TypeSpecifierNode>().pointer_depth() > 0 ||
														object_decl->type_node().as<TypeSpecifierNode>().is_reference() ||
														object_decl->type_node().as<TypeSpecifierNode>().is_rvalue_reference());

					ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

					// Now add the indirect call with the function pointer temp var
					irOperands.emplace_back(func_ptr_temp);

					// Add arguments
					std::vector<TypedValue> arguments;
					memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
						ExprResult argument_result = visitExpressionNode(argument.as<ExpressionNode>());
						arguments.push_back(makeTypedValue(argument_result.typeEnum(), argument_result.size_in_bits, toIrValue(argument_result.value)));
					});

					IndirectCallOp op{
						.result = ret_var,
						.function_pointer = func_ptr_temp,
						.arguments = std::move(arguments)};
					ir_.addInstruction(IrInstruction(IrOpcode::IndirectCall, std::move(op), memberFunctionCallNode.called_from()));

					// Use the function pointer's stored return type
					if (!member.function_signature) {
						throw InternalError("Function pointer member missing function_signature for indirect call return type");
					}
					TypeCategory ret_type = member.function_signature->returnType();
					int ret_size = (ret_type == TypeCategory::Void) ? 0 : get_type_size_bits(ret_type);
					return makeExprResult(nativeTypeIndex(ret_type), SizeInBits{static_cast<int>(ret_size)}, IrOperand{ret_var}, PointerDepth{}, ValueStorage::ContainsData);
				}
			}
		}
	}

	// Check if this is a member function template that needs instantiation
	if (struct_info) {
		std::string_view func_name = func_decl_node.identifier_token().value();
		StringBuilder qualified_name_sb;
		qualified_name_sb.append(StringTable::getStringView(struct_info->getName())).append("::").append(func_name);
		StringHandle qualified_template_name = StringTable::getOrInternStringHandle(qualified_name_sb);
		// DEBUG removed

		// Look up if this is a template
		auto template_opt = gTemplateRegistry.lookupTemplate(qualified_template_name);
		if (template_opt.has_value()) {
			// DEBUG removed
			if (template_opt->is<TemplateFunctionDeclarationNode>()) {
				// DEBUG removed
				// This is a member function template - we need to instantiate it

				// Deduce template argument types from call arguments
				std::vector<TypeIndex> arg_types;
				// DEBUG removed
				memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
					// DEBUG removed
					if (!argument.is<ExpressionNode>()) {
						FLASH_LOG(Codegen, Debug, "Argument is not an ExpressionNode");
						return;
					}
					FLASH_LOG(Codegen, Trace, "Argument is an ExpressionNode");

					const ExpressionNode& arg_expr = argument.as<ExpressionNode>();

					// DEBUG removed

					// Get type of argument - for literals, use the literal type
					if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
						arg_types.push_back(TypeIndex{}.withCategory(TypeCategory::Bool));
					} else if (const auto* numeric_literal = std::get_if<NumericLiteralNode>(&arg_expr)) {
						const NumericLiteralNode& lit = *numeric_literal;
						// DEBUG removed
						arg_types.push_back(TypeIndex{}.withCategory(lit.type()));
					} else if (std::holds_alternative<IdentifierNode>(arg_expr)) {
						// Look up variable type
						const IdentifierNode& ident = std::get<IdentifierNode>(arg_expr);
						// DEBUG removed
						auto symbol_opt = symbol_table.lookup(ident.name());
						if (symbol_opt.has_value() && symbol_opt->is<DeclarationNode>()) {
							const DeclarationNode& decl = symbol_opt->as<DeclarationNode>();
							const TypeSpecifierNode& type = decl.type_node().as<TypeSpecifierNode>();
							// DEBUG removed
							arg_types.push_back(type.type_index().withCategory(type.type()));
						}
					} else {
						// DEBUG removed
					}
				});

				// DEBUG removed

				// Try to instantiate the template with deduced argument types
				if (!arg_types.empty()) {
					// Build instantiation key
					const TemplateFunctionDeclarationNode& template_func = template_opt->as<TemplateFunctionDeclarationNode>();

					InlineVector<TemplateTypeArg, 4> template_args;
					for (const auto& arg_type_index : arg_types) {
						template_args.push_back(TemplateTypeArg::makeType(arg_type_index));
					}

					// Check if we already have this instantiation
					auto inst_key = FlashCpp::makeInstantiationKey(qualified_template_name, template_args);

					auto existing_inst = gTemplateRegistry.getInstantiation(inst_key);
					if (!existing_inst.has_value()) {
						// Check requires clause constraint before instantiation
						bool should_instantiate = true;
						if (template_func.has_requires_clause()) {
							const RequiresClauseNode& requires_clause =
								template_func.requires_clause()->as<RequiresClauseNode>();

							// Get template parameter names for evaluation
							InlineVector<std::string_view, 4> eval_param_names;
							for (const auto& tparam_node : template_func.template_parameters()) {
								if (tparam_node.is<TemplateParameterNode>()) {
									eval_param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
								}
							}

							// Convert arg_types to TemplateTypeArg for evaluation
							InlineVector<TemplateTypeArg, 4> type_args;
							for (const auto& arg_type_index : arg_types) {
								type_args.push_back(TemplateTypeArg::makeType(arg_type_index));
							}

							// Evaluate the constraint with the template arguments
							auto constraint_result = evaluateConstraint(
								requires_clause.constraint_expr(), type_args, eval_param_names);

							if (!constraint_result.satisfied) {
								// Constraint not satisfied - report detailed error
								// Build template arguments string
								std::string args_str;
								for (size_t i = 0; i < arg_types.size(); ++i) {
									if (i > 0)
										args_str += ", ";
									args_str += std::string(TemplateRegistry::typeToString(arg_types[i].category()));
								}

								FLASH_LOG(Codegen, Error, "constraint not satisfied for template function '", func_name, "'");
								FLASH_LOG(Codegen, Error, "  ", constraint_result.error_message);
								if (!constraint_result.failed_requirement.empty()) {
									FLASH_LOG(Codegen, Error, "  failed requirement: ", constraint_result.failed_requirement);
								}
								if (!constraint_result.suggestion.empty()) {
									FLASH_LOG(Codegen, Error, "  suggestion: ", constraint_result.suggestion);
								}
								FLASH_LOG(Codegen, Error, "  template arguments: ", args_str);

								// Don't create instantiation - constraint failed
								should_instantiate = false;
							}
						}

						// Create new instantiation only if constraint was satisfied (or no constraint)
						if (should_instantiate) {
							gTemplateRegistry.registerInstantiation(inst_key, template_func.function_declaration());
						}

						// Get template parameter names
						InlineVector<std::string_view, 4> param_names;
						for (const auto& tparam_node : template_func.template_parameters()) {
							if (tparam_node.is<TemplateParameterNode>()) {
								param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
							}
						}

						// Generate the mangled name
						[[maybe_unused]] std::string_view mangled_func_name = gTemplateRegistry.mangleTemplateName(func_name, template_args);

						// Template instantiation now happens during parsing
						// The instantiated function should already be in the AST
						// We just use the mangled name for the call

						/*
						// OLD: Collect this instantiation for deferred generation
						const FunctionDeclarationNode& template_func_decl = template_func.function_decl_node();
						if (template_func_decl.has_template_body_position()) {
							TemplateInstantiationInfo inst_info;
							inst_info.qualified_template_name = qualified_template_name;
							inst_info.mangled_name = StringTable::getOrInternStringHandle(mangled_func_name);
							inst_info.struct_name = struct_info->getName();
							for (const auto& arg_type : arg_types) {
								inst_info.template_args.push_back(arg_type);
							}
							inst_info.body_position = template_func_decl.template_body_position();
							inst_info.template_param_names = param_names;
							inst_info.template_node_ptr = &template_func;

							// Collect the instantiation - it will be generated later at the top level
							// This ensures the FunctionDecl IR appears before any calls to it
							collected_template_instantiations_.push_back(std::move(inst_info));
						}
						*/
					}
				}
			} else {
				// DEBUG removed
			}
		} else {
			// DEBUG removed
		}
	}

	// Check access control for member function calls
	if (called_member_func && struct_info) {
		const StructTypeInfo* current_context = getCurrentStructContext();
		std::string_view current_function = getCurrentFunctionName();
		if (!checkMemberFunctionAccess(called_member_func, struct_info, current_context, current_function)) {
			std::string_view access_str = (called_member_func->access == AccessSpecifier::Private) ? "private"sv : "protected"sv;
			std::string context_str = current_context ? (std::string(" from '") + std::string(StringTable::getStringView(current_context->getName())) + "'") : "";
			FLASH_LOG(Codegen, Error, "Cannot access ", access_str, " member function '", called_member_func->getName(),
					  "' of '", struct_info->getName(), "'", context_str);
			throw CompileError("Access control violation");
		}
	}

	TempVar ret_var = var_counter.next();

	if (is_virtual_call && vtable_index >= 0) {
		// Generate virtual function call using VirtualCallOp
		VirtualCallOp vcall_op;
		// Get return type from the actual member function (if found) instead of the placeholder declaration
		// The placeholder may not have correct pointer depth information for the return type
		const auto& return_type = (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>())
									  ? called_member_func->function_decl.as<FunctionDeclarationNode>().decl_node().type_node().as<TypeSpecifierNode>()
									  : func_decl_node.type_node().as<TypeSpecifierNode>();
		vcall_op.result.setType(return_type.category());
		vcall_op.result.ir_type = toIrType(return_type.type());
		// For pointer return types, use 64 bits (pointer size), otherwise use the type's size
		// Also handle reference return types as pointers (64 bits)
		FLASH_LOG(Codegen, Debug, "VirtualCall return_type: ptr_depth=", return_type.pointer_depth(),
				  " is_ptr=", return_type.is_pointer(),
				  " is_ref=", return_type.is_reference(),
				  " is_rref=", return_type.is_rvalue_reference(),
				  " size_bits=", return_type.size_in_bits());
		if (return_type.pointer_depth() > 0 || return_type.is_pointer() || return_type.is_reference() || return_type.is_rvalue_reference()) {
			vcall_op.result.size_in_bits = SizeInBits{64};
		} else {
			vcall_op.result.size_in_bits = SizeInBits{return_type.size_in_bits()};
		}
		populateReferenceReturnInfo(vcall_op, return_type);
		FLASH_LOG(Codegen, Debug, "VirtualCall result.size_in_bits=", vcall_op.result.size_in_bits);
		vcall_op.result.value = ret_var;
		vcall_op.object_type_index = object_type.type_index();
		vcall_op.object_size = static_cast<int>(object_type.size_in_bits());
		if (object_name.empty()) {
			// Object is a temporary expression result - evaluate it to get a TempVar
			ExprResult obj_result = visitExpressionNode(*object_expr);
			if (!std::holds_alternative<TempVar>(obj_result.value)) {
				throw InternalError("Virtual call on expression: did not produce a TempVar");
			}
			vcall_op.object = std::get<TempVar>(obj_result.value);
		} else {
			vcall_op.object = StringTable::getOrInternStringHandle(object_name);
		}
		vcall_op.vtable_index = vtable_index;
		// Set is_pointer_access based on whether the object is accessed through a pointer (ptr->method)
		// or through a reference (ref.method()). References are implemented as pointers internally,
		// so they need the same treatment as pointer access for virtual dispatch.
		vcall_op.is_pointer_access = (object_type.pointer_depth() > 0) || object_type.is_reference() || object_type.is_rvalue_reference();

		// Generate IR for function arguments
		memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
			ExprResult argument_result = visitExpressionNode(argument.as<ExpressionNode>());

			// For variables, we need to add the type and size
			if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				const auto& decl_node = symbol->as<DeclarationNode>();
				const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

				TypedValue tv;
				tv.setType(type_node.type());
				tv.ir_type = toIrType(type_node.type());
				tv.size_in_bits = SizeInBits{type_node.size_in_bits()};
				tv.value = StringTable::getOrInternStringHandle(identifier.name());
				vcall_op.arguments.push_back(tv);
			} else {
				// Convert from IrOperand to TypedValue
				// Format: [type, size, value]
				TypedValue tv = toTypedValue(argument_result);
				vcall_op.arguments.push_back(tv);
			}
		});

		// Add the virtual call instruction
		ir_.addInstruction(IrInstruction(IrOpcode::VirtualCall, std::move(vcall_op), memberFunctionCallNode.called_from()));
	} else {
		// Generate regular (non-virtual) member function call using CallOp typed payload

		// Vector to hold deduced parameter types (populated for generic lambdas)
		std::vector<TypeSpecifierNode> param_types;
		std::optional<TypeSpecifierNode> resolved_generic_return_type;

		// Check if this is an instantiated template function
		std::string_view func_name = func_decl_node.identifier_token().value();
		StringHandle function_name;

		// Check if this is a member function - use struct_info to determine
		if (struct_info) {
			// For nested classes, we need the fully qualified name from TypeInfo
			auto struct_name = struct_info->getName();
			auto type_it = getTypesByNameMap().find(struct_name);
			if (type_it != getTypesByNameMap().end()) {
				struct_name = type_it->second->name();
			}
			auto qualified_template_name = StringTable::getOrInternStringHandle(StringBuilder().append(struct_name).append("::"sv).append(func_name));

			// Check if this is a template that has been instantiated
			auto template_opt = gTemplateRegistry.lookupTemplate(qualified_template_name);
			if (template_opt.has_value() && template_opt->is<TemplateFunctionDeclarationNode>()) {
				// This is a member function template - use the mangled name

				// Deduce template arguments from call arguments
				InlineVector<TemplateTypeArg, 4> template_args;
				memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
					if (!argument.is<ExpressionNode>())
						return;
					const ExpressionNode& arg_expr = argument.as<ExpressionNode>();

					// Get type of argument
					if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
						template_args.push_back(TemplateTypeArg::makeType(nativeTypeIndex(TypeCategory::Bool)));
					} else if (const auto* numeric_literal = std::get_if<NumericLiteralNode>(&arg_expr)) {
						const NumericLiteralNode& lit = *numeric_literal;
						template_args.push_back(TemplateTypeArg::makeType(nativeTypeIndex(lit.type())));
					} else if (std::holds_alternative<IdentifierNode>(arg_expr)) {
						const IdentifierNode& ident = std::get<IdentifierNode>(arg_expr);
						auto symbol_opt = symbol_table.lookup(ident.name());
						if (symbol_opt.has_value() && symbol_opt->is<DeclarationNode>()) {
							const DeclarationNode& decl = symbol_opt->as<DeclarationNode>();
							const TypeSpecifierNode& type = decl.type_node().as<TypeSpecifierNode>();
							template_args.push_back(TemplateTypeArg::makeType(nativeTypeIndex(type.type())));
						}
					}
				});

				// Generate the mangled name
				std::string_view mangled_func_name = gTemplateRegistry.mangleTemplateName(func_name, template_args);

				// Build qualified function name with mangled template name
				function_name = StringTable::getOrInternStringHandle(StringBuilder().append(struct_name).append("::"sv).append(mangled_func_name));
			} else {
				// Regular member function (not a template) - generate proper mangled name
				// Prefer the function declaration from struct_info (has correctly substituted
				// parameter types for template instantiations). The MemberFunctionCallNode's
				// embedded func_decl may still reference the unsubstituted pattern declaration
				// (e.g., with T& instead of int&) because MemberFunctionCallNode stores a
				// const reference that cannot be rebound during template substitution.
				const FunctionDeclarationNode* func_for_mangling =
					materialized_member_func_decl ? materialized_member_func_decl : &func_decl;
				if (called_member_func &&
					called_member_func->function_decl.is<FunctionDeclarationNode>()) {
					func_for_mangling = &called_member_func->function_decl.as<FunctionDeclarationNode>();
				}

				// Get return type and parameter types from the function declaration.
				// Generic lambda calls can refine this below once sema has normalized
				// the instantiated lambda body with concrete argument types.
				const TypeSpecifierNode* mangling_return_type = &func_for_mangling->decl_node().type_node().as<TypeSpecifierNode>();

				// Check if this is a generic lambda call (lambda with auto parameters)
				bool is_generic_lambda = StringTable::getStringView(struct_name).substr(0, 9) == "__lambda_"sv;
				if (is_generic_lambda) {
					// For generic lambdas, we need to deduce auto parameter types from arguments
					// Collect argument types first
					std::vector<TypeSpecifierNode> arg_types;
					memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
						const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(arg_expr)) {
							const auto& identifier = std::get<IdentifierNode>(arg_expr);
							const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
							if (symbol.has_value()) {
								const DeclarationNode* decl = get_decl_from_symbol(*symbol);
								if (decl) {
									TypeSpecifierNode type_node = decl->type_node().as<TypeSpecifierNode>();
									// Resolve auto type from lambda initializer if available
									if (isPlaceholderAutoType(type_node.type())) {
										if (auto deduced = deduceLambdaClosureType(*symbol, decl->identifier_token())) {
											type_node = *deduced;
										} else if (current_lambda_context_.isActive() && type_node.is_rvalue_reference()) {
											// For auto&& parameters inside lambdas (recursive lambda pattern),
											// assume the parameter has the closure type of the current lambda.
											// This handles: auto factorial = [](auto&& self, int n) { ... self(self, n-1); }
											auto type_it = getTypesByNameMap().find(current_lambda_context_.closure_type);
											if (type_it != getTypesByNameMap().end()) {
												const TypeInfo* closure_type = type_it->second;
												int closure_size = closure_type->getStructInfo()
																	   ? closure_type->getStructInfo()->sizeInBits().value
																	   : 64;
												type_node = TypeSpecifierNode(
													closure_type->type_index_.withCategory(TypeCategory::Struct),
													closure_size,
													decl->identifier_token(),
													CVQualifier::None,
													ReferenceQualifier::None);
												// Preserve rvalue reference flag
												type_node.set_reference_qualifier(ReferenceQualifier::RValueReference);
											}
										}
									}
									arg_types.push_back(type_node);
								} else {
									arg_types.push_back(TypeSpecifierNode(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None));
								}
							} else {
								arg_types.push_back(TypeSpecifierNode(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None));
							}
						} else if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
							arg_types.push_back(TypeSpecifierNode(TypeCategory::Bool, TypeQualifier::None, 8, Token{}, CVQualifier::None));
						} else if (const auto* literal = std::get_if<NumericLiteralNode>(&arg_expr)) {
							arg_types.push_back(TypeSpecifierNode(literal->type(), TypeQualifier::None,
																  static_cast<unsigned char>(literal->sizeInBits()), Token{}, CVQualifier::None));
						} else {
							// Default to int for complex expressions
							arg_types.push_back(TypeSpecifierNode(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None));
						}
					});

					LambdaInfo* matched_lambda_info = nullptr;

					// Now build param_types with deduced types for auto parameters
					size_t arg_idx = 0;
					for (const auto& param_node : func_for_mangling->parameter_nodes()) {
						if (param_node.is<DeclarationNode>()) {
							const auto& param_decl = param_node.as<DeclarationNode>();
							const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();

							if (isPlaceholderAutoType(param_type.type()) && arg_idx < arg_types.size()) {
								// Deduce type from argument, preserving reference flags from auto&& parameter
								TypeSpecifierNode deduced_type = arg_types[arg_idx];
								deduced_type.set_reference_qualifier(param_type.reference_qualifier());
								param_types.push_back(deduced_type);

								// Also store the deduced type in LambdaInfo for use by generateLambdaOperatorCallFunction
								for (auto& lambda_info : collected_lambdas_) {
									if (lambda_info.closure_type_name == struct_name) {
										matched_lambda_info = &lambda_info;
										lambda_info.setDeducedType(arg_idx, deduced_type);
										break;
									}
								}
							} else {
								param_types.push_back(param_type);
							}
						}
						arg_idx++;
					}

					if (matched_lambda_info && sema_) {
						sema_->normalizeInstantiatedLambdaBody(*matched_lambda_info);
						if (!isPlaceholderAutoType(matched_lambda_info->return_type_index.category())) {
							resolved_generic_return_type.emplace(
								matched_lambda_info->return_type_index.withCategory(matched_lambda_info->returnType()),
								matched_lambda_info->return_size,
								matched_lambda_info->lambda_token,
								CVQualifier::None,
								ReferenceQualifier::None);
							if (matched_lambda_info->returns_reference) {
								resolved_generic_return_type->set_reference_qualifier(ReferenceQualifier::LValueReference);
								resolved_generic_return_type->set_size_in_bits(64);
							}
							mangling_return_type = &*resolved_generic_return_type;
						}
					}
				} else {
					// Non-lambda: use parameter types directly from declaration
					for (const auto& param_node : func_for_mangling->parameter_nodes()) {
						if (param_node.is<DeclarationNode>()) {
							const auto& param_decl = param_node.as<DeclarationNode>();
							const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
							param_types.push_back(param_type);
						}
					}
				}

				// Build namespace path from the struct's declaration-site namespace
				// so member function calls get the correct mangled name.
				std::vector<std::string> namespace_for_mangling;
				auto struct_name_view = StringTable::getStringView(struct_name);
				if (struct_name_view.find("::") == std::string_view::npos) {
					auto ns_views = buildNamespacePathFromHandle(struct_info->getNamespaceHandle());
					namespace_for_mangling.reserve(ns_views.size());
					for (auto sv : ns_views)
						namespace_for_mangling.emplace_back(sv);
				}

				// Generate proper mangled name including parameter types
				std::string_view mangled = generateMangledNameForCall(
					func_name,
					*mangling_return_type,
					param_types,
					func_for_mangling->is_variadic(),
					struct_name_view,
					namespace_for_mangling,
					func_for_mangling->is_const_member_function());
				function_name = StringTable::getOrInternStringHandle(mangled);
			}
		} else {
			// Non-member function or fallback
			function_name = StringTable::getOrInternStringHandle(func_name);
		}

		// Create CallOp structure
		CallOp call_op;
		call_op.result = ret_var;
		call_op.function_name = function_name;

		// Get return type information from the actual member function declaration
		// Use called_member_func if available (has the substituted template types)
		// Otherwise fall back to func_decl or func_decl_node
		const TypeSpecifierNode* return_type_ptr = nullptr;
		if (resolved_generic_return_type.has_value()) {
			return_type_ptr = &*resolved_generic_return_type;
		} else if (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) {
			return_type_ptr = &called_member_func->function_decl.as<FunctionDeclarationNode>().decl_node().type_node().as<TypeSpecifierNode>();
		} else {
			return_type_ptr = &func_decl_node.type_node().as<TypeSpecifierNode>();
		}
		const auto& return_type = *return_type_ptr;
		call_op.return_type_index = return_type.type_index();
		// For reference return types, use 64-bit size (pointer size) since references are returned as pointers
		call_op.return_size_in_bits = SizeInBits{(return_type.pointer_depth() > 0 || return_type.is_reference() || return_type.is_rvalue_reference()) ? 64 : static_cast<int>(return_type.size_in_bits())};
		populateReferenceReturnInfo(call_op, return_type);
		call_op.is_member_function = true;

		// Get the actual function declaration to check if it's variadic
		const FunctionDeclarationNode* actual_func_decl_for_variadic = &func_decl;
		if (called_member_func &&
			called_member_func->function_decl.is<FunctionDeclarationNode>()) {
			actual_func_decl_for_variadic = &called_member_func->function_decl.as<FunctionDeclarationNode>();
		}
		call_op.is_variadic = actual_func_decl_for_variadic->is_variadic();

		// Detect if calling a member function that returns struct by value (needs hidden return parameter for RVO)
		bool returns_struct_by_value = returnsStructByValue(return_type.type(), return_type.pointer_depth(), return_type.is_reference());
		bool needs_hidden_return_param = needsHiddenReturnParam(return_type.type(), return_type.pointer_depth(), return_type.is_reference(), return_type.size_in_bits(), context_->isLLP64());

		FLASH_LOG_FORMAT(Codegen, Debug,
						 "Member function call check: returns_struct={}, size={}, threshold={}, needs_hidden={}",
						 returns_struct_by_value, return_type.size_in_bits(), getStructReturnThreshold(context_->isLLP64()), needs_hidden_return_param);

		if (needs_hidden_return_param) {
			call_op.return_slot = ret_var; // The result temp var serves as the return slot

			FLASH_LOG_FORMAT(Codegen, Debug,
							 "Member function call {} returns struct by value (size={} bits) - using return slot (temp_{})",
							 StringTable::getStringView(function_name), return_type.size_in_bits(), ret_var.var_number);
		} else if (returns_struct_by_value) {
			// Small struct return - no return slot needed
			FLASH_LOG_FORMAT(Codegen, Debug,
							 "Member function call {} returns small struct by value (size={} bits) - will return in RAX",
							 StringTable::getStringView(function_name), return_type.size_in_bits());
		}

		// Add the object as the first argument (this pointer)
		// The 'this' pointer is always 64 bits (pointer size on x64), regardless of struct size
		// This is critical for empty structs (size 0) which still need a valid address
		IrValue this_arg_value;
		bool object_is_pointer_like = object_type.pointer_depth() > 0 || object_type.is_reference() || object_type.is_rvalue_reference();
		if (object_name.empty()) {
			// Object is a temporary expression result (e.g., getContainer().method())
			// Evaluate the expression to get a TempVar, then take its address for the this pointer
			ExprResult obj_result = visitExpressionNode(*object_expr);
			if (!std::holds_alternative<TempVar>(obj_result.value)) {
				throw InternalError("Member function call on expression: did not produce a TempVar");
			}
			TempVar obj_temp = std::get<TempVar>(obj_result.value);

			if (object_is_pointer_like) {
				// Temporary is already a pointer/reference - pass through directly
				this_arg_value = IrValue(obj_temp);
			} else {
				// Temporary is a value - take its address
				TempVar this_addr = var_counter.next();
				AddressOfOp addr_op;
				addr_op.result = this_addr;
				addr_op.operand.setType(object_type.category());
				addr_op.operand.ir_type = toIrType(object_type.type());
				addr_op.operand.size_in_bits = SizeInBits{object_type.size_in_bits()};
				addr_op.operand.pointer_depth = PointerDepth{static_cast<int>(object_type.pointer_depth())};
				addr_op.operand.value = obj_temp;
				ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), memberFunctionCallNode.called_from()));
				this_arg_value = IrValue(this_addr);
			}
		} else if (object_is_pointer_like) {
			// For pointer/reference objects, pass through directly
			this_arg_value = IrValue(StringTable::getOrInternStringHandle(object_name));
		} else {
			// For object values, take the address so member functions receive a pointer to the object
			TempVar this_addr = var_counter.next();
			AddressOfOp addr_op;
			addr_op.result = this_addr;
			addr_op.operand.setType(object_type.category());
			addr_op.operand.ir_type = toIrType(object_type.type());
			addr_op.operand.size_in_bits = SizeInBits{object_type.size_in_bits()};
			addr_op.operand.pointer_depth = PointerDepth{static_cast<int>(object_type.pointer_depth())};
			addr_op.operand.value = StringTable::getOrInternStringHandle(object_name);
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), memberFunctionCallNode.called_from()));
			this_arg_value = IrValue(this_addr);
		}
		call_op.args.push_back(makeTypedValue(object_type.type(), SizeInBits{64}, this_arg_value));

		// Generate IR for function arguments and add to CallOp
		size_t arg_index = 0;

		// Prefer the function declaration embedded in the MemberFunctionCallNode.
		// For member function templates, this is the instantiated declaration and
		// carries any substituted default arguments from the outer class template
		// bindings. struct_info may still point at the original uninstantiated
		// template declaration.
		const FunctionDeclarationNode* actual_func_decl =
			materialized_member_func_decl ? materialized_member_func_decl : &func_decl;
		if (called_member_func &&
			called_member_func->function_decl.is<FunctionDeclarationNode>()) {
			actual_func_decl = &called_member_func->function_decl.as<FunctionDeclarationNode>();
		}

		memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
			// Get the parameter type from the function declaration to check if it's a reference
			// For generic lambdas, use the deduced types from param_types instead of the original auto types
			const TypeSpecifierNode* param_type = nullptr;
			std::optional<TypeSpecifierNode> deduced_param_type;
			if (arg_index < param_types.size()) {
				// Use deduced type from param_types (handles generic lambdas correctly)
				deduced_param_type = param_types[arg_index];
				param_type = &(*deduced_param_type);
			} else if (arg_index < actual_func_decl->parameter_nodes().size()) {
				const ASTNode& param_node = actual_func_decl->parameter_nodes()[arg_index];
				if (param_node.is<DeclarationNode>()) {
					const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
					param_type = &param_decl.type_node().as<TypeSpecifierNode>();
				} else if (param_node.is<VariableDeclarationNode>()) {
					const VariableDeclarationNode& var_decl = param_node.as<VariableDeclarationNode>();
					const DeclarationNode& param_decl = var_decl.declaration();
					param_type = &param_decl.type_node().as<TypeSpecifierNode>();
				}
			}
			const CallArgReferenceBindingInfo* sema_ref_binding = nullptr;
			if (param_type && sema_) {
				sema_ref_binding = sema_->getMemberFunctionCallRefBinding(&memberFunctionCallNode, arg_index);
			}

			// Evaluate the argument expression once when sema ref-binding is active so that
			// the result can be reused in the fallback path without double evaluation.
			std::optional<ExprResult> sema_evaluated_arg;
			bool sema_ref_binding_applied = false;
			if (param_type && sema_ref_binding && sema_ref_binding->is_valid()) {
				ExpressionContext arg_context = sema_ref_binding->binds_directly()
													? ExpressionContext::LValueAddress
													: ExpressionContext::Load;
				sema_evaluated_arg = visitExpressionNode(argument.as<ExpressionNode>(), arg_context);
				if (auto sema_bound_arg = tryApplySemaCallArgReferenceBinding(
						*sema_evaluated_arg, argument, *param_type, sema_ref_binding, memberFunctionCallNode.called_from())) {
					TypedValue typed_arg = toTypedValue(*sema_bound_arg);
					typed_arg.cv_qualifier = param_type->cv_qualifier();
					typed_arg.pointer_depth = PointerDepth{static_cast<int>(param_type->pointer_depth())};
					if (param_type->type_index().is_valid()) {
						typed_arg.type_index = param_type->type_index();
					}
					typed_arg.ref_qualifier = param_type->is_rvalue_reference()
												  ? ReferenceQualifier::RValueReference
												  : ReferenceQualifier::LValueReference;
					call_op.args.push_back(std::move(typed_arg));
					arg_index++;
					sema_ref_binding_applied = true;
					return;
				}
			}

			// For variables (identifiers), handle specially to avoid unnecessary dereferences
			// when passing reference arguments to reference parameters
			if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());

				// Check if this is a function being passed as a function pointer argument
				if (symbol.has_value() && symbol->is<FunctionDeclarationNode>()) {
					// Function being passed as function pointer - just pass its name
					call_op.args.push_back(makeTypedValue(TypeCategory::FunctionPointer, SizeInBits{64}, IrValue(StringTable::getOrInternStringHandle(identifier.name()))));
				} else if (symbol.has_value() && symbol->is<DeclarationNode>()) {
					const auto& decl_node = symbol->as<DeclarationNode>();
					const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

					// Check if parameter expects a reference
					if (!sema_ref_binding_applied &&
						param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
						// Parameter expects a reference - pass the address of the argument
						if (type_node.is_reference() || type_node.is_rvalue_reference()) {
							// Argument is already a reference - just pass it through
							// Use 64-bit pointer size since references are passed as pointers
							call_op.args.push_back(makeTypedValue(type_node.type(), SizeInBits{64},
																  IrValue(StringTable::getOrInternStringHandle(identifier.name())), ReferenceQualifier::LValueReference));
						} else {
							// Argument is a value - take its address
							TempVar addr_var = emitAddressOf(type_node.category(), static_cast<int>(type_node.size_in_bits()), IrValue(StringTable::getOrInternStringHandle(identifier.name())));

							// Pass the address with pointer size
							call_op.args.push_back(makeTypedValue(type_node.type(), SizeInBits{64},
																  IrValue(addr_var), ReferenceQualifier::LValueReference));
						}
					} else {
						// Regular pass by value; reuse already-evaluated result to avoid double evaluation.
						ExprResult arg_result = sema_evaluated_arg
													? *sema_evaluated_arg
													: visitExpressionNode(argument.as<ExpressionNode>());
						if (param_type) {
							if (auto materialized = tryMaterializeSemaSelectedConvertingConstructor(
									arg_result, argument, *param_type, memberFunctionCallNode.called_from())) {
								arg_result = *materialized;
							} else {
								arg_result = applyConstructorArgConversion(arg_result, argument, *param_type, memberFunctionCallNode.called_from());
							}
						}
						call_op.args.push_back(toTypedValue(arg_result));
					}
				} else if (symbol.has_value() && symbol->is<VariableDeclarationNode>()) {
					// Handle VariableDeclarationNode (local variables)
					const auto& var_decl = symbol->as<VariableDeclarationNode>();
					const auto& decl_node = var_decl.declaration();
					const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

					// Check if parameter expects a reference
					if (!sema_ref_binding_applied &&
						param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
						// Parameter expects a reference - pass the address of the argument
						if (type_node.is_reference() || type_node.is_rvalue_reference()) {
							// Argument is already a reference - just pass it through
							// Use 64-bit pointer size since references are passed as pointers
							call_op.args.push_back(makeTypedValue(type_node.type(), SizeInBits{64},
																  IrValue(StringTable::getOrInternStringHandle(identifier.name())), ReferenceQualifier::LValueReference));
						} else {
							// Argument is a value - take its address
							TempVar addr_var = emitAddressOf(type_node.category(), static_cast<int>(type_node.size_in_bits()), IrValue(StringTable::getOrInternStringHandle(identifier.name())));

							// Pass the address with pointer size
							call_op.args.push_back(makeTypedValue(type_node.type(), SizeInBits{64},
																  IrValue(addr_var), ReferenceQualifier::LValueReference));
						}
					} else {
						// Regular pass by value; reuse already-evaluated result to avoid double evaluation.
						ExprResult arg_result = sema_evaluated_arg
													? *sema_evaluated_arg
													: visitExpressionNode(argument.as<ExpressionNode>());
						if (param_type) {
							if (auto materialized = tryMaterializeSemaSelectedConvertingConstructor(
									arg_result, argument, *param_type, memberFunctionCallNode.called_from())) {
								arg_result = *materialized;
							} else {
								arg_result = applyConstructorArgConversion(arg_result, argument, *param_type, memberFunctionCallNode.called_from());
							}
						}
						call_op.args.push_back(toTypedValue(arg_result));
					}
				} else {
					// Unknown symbol type - fall back to visitExpressionNode
					ExprResult argument_result = sema_evaluated_arg
													 ? *sema_evaluated_arg
													 : visitExpressionNode(argument.as<ExpressionNode>());
					if (param_type) {
						if (auto materialized = tryMaterializeSemaSelectedConvertingConstructor(
								argument_result, argument, *param_type, memberFunctionCallNode.called_from())) {
							argument_result = *materialized;
						} else {
							argument_result = applyConstructorArgConversion(argument_result, argument, *param_type, memberFunctionCallNode.called_from());
						}
					}
					call_op.args.push_back(toTypedValue(argument_result));
				}
			} else {
				// Not an identifier - reuse the already-evaluated result when sema
				// ref-binding ran but returned nullopt to avoid double expression evaluation.
				ExprResult argument_result = sema_evaluated_arg
												 ? *sema_evaluated_arg
												 : visitExpressionNode(argument.as<ExpressionNode>());

				// Check if parameter expects a reference and argument is a literal
				if (!sema_ref_binding_applied &&
					param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
					// Parameter expects a reference, but argument is not an identifier
					// We need to materialize the value into a temporary and pass its address

					// Check if this is a literal value
					bool is_literal =
						std::holds_alternative<unsigned long long>(argument_result.value) ||
						std::holds_alternative<double>(argument_result.value);

					if (is_literal) {
						// Materialize the literal into a temporary variable
						TypeCategory literal_type = argument_result.typeEnum();
						int literal_size = argument_result.size_in_bits.value;

						// Create a temporary variable to hold the literal value
						TempVar temp_var = var_counter.next();

						// Generate an assignment IR to store the literal using typed payload
						AssignmentOp assign_op;
						assign_op.result = temp_var; // unused but required

						// Convert IrOperand to IrValue for the literal
						IrValue rhs_value;
						if (const auto* ull_val = std::get_if<unsigned long long>(&argument_result.value)) {
							rhs_value = *ull_val;
						} else if (const auto* d_val = std::get_if<double>(&argument_result.value)) {
							rhs_value = *d_val;
						}

						// Create TypedValue for lhs and rhs
						assign_op.lhs = makeTypedValue(literal_type, SizeInBits{static_cast<int>(literal_size)}, temp_var);
						assign_op.rhs = makeTypedValue(literal_type, SizeInBits{static_cast<int>(literal_size)}, rhs_value);

						ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));

						// Now take the address of the temporary
						TempVar addr_var = emitAddressOf(literal_type, literal_size, IrValue(temp_var));

						// Pass the address
						call_op.args.push_back(makeTypedValue(literal_type, SizeInBits{64},
															  IrValue(addr_var), ReferenceQualifier::LValueReference));
					} else {
						// Not a literal (expression result in a TempVar) - take its address
						if (std::holds_alternative<TempVar>(argument_result.value)) {
							TypeCategory expr_type = argument_result.typeEnum();
							int expr_size = argument_result.size_in_bits.value;
							TempVar expr_var = std::get<TempVar>(argument_result.value);

							TempVar addr_var = emitAddressOf(expr_type, expr_size, IrValue(expr_var));

							call_op.args.push_back(makeTypedValue(expr_type, SizeInBits{64},
																  IrValue(addr_var), ReferenceQualifier::LValueReference));
						} else {
							// Fallback - just pass through
							call_op.args.push_back(toTypedValue(argument_result));
						}
					}
				} else {
					// Parameter doesn't expect a reference - pass through as-is
					if (param_type) {
						if (auto materialized = tryMaterializeSemaSelectedConvertingConstructor(
								argument_result, argument, *param_type, memberFunctionCallNode.called_from())) {
							argument_result = *materialized;
						} else {
							argument_result = applyConstructorArgConversion(argument_result, argument, *param_type, memberFunctionCallNode.called_from());
						}
					}
					call_op.args.push_back(toTypedValue(argument_result));
				}
			}

			arg_index++;
		});

		// Fill in default arguments for parameters that weren't explicitly provided
		if (actual_func_decl) {
			fillInDefaultArguments(call_op, actual_func_decl->parameter_nodes(), arg_index);
		}

		// Add the function call instruction with typed payload
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberFunctionCallNode.called_from()));
	}

	// Build the final ExprResult from the return type — handles reference metadata,
	// auto-dereference, size/type_index computation, and ValueStorage selection.
	const auto& return_type = (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>())
								  ? called_member_func->function_decl.as<FunctionDeclarationNode>().decl_node().type_node().as<TypeSpecifierNode>()
								  : func_decl_node.type_node().as<TypeSpecifierNode>();
	return buildCallReturnResult(return_type, ret_var, context, memberFunctionCallNode.called_from());
}

// Helper function to convert a MemberFunctionCallNode to a regular FunctionCallNode
// Used when a member function call syntax is used but the object is not a struct
ExprResult AstToIr::convertMemberCallToFunctionCall(const MemberFunctionCallNode& memberFunctionCallNode, ExpressionContext context) {
	const FunctionDeclarationNode& func_decl = memberFunctionCallNode.function_declaration();
	const DeclarationNode& decl_node = func_decl.decl_node();

	// Copy the arguments using the visit method
	ChunkedVector<ASTNode> args_copy;
	memberFunctionCallNode.arguments().visit([&](ASTNode arg) {
		args_copy.push_back(arg);
	});

	FunctionCallNode function_call(decl_node, std::move(args_copy), memberFunctionCallNode.called_from());
	return generateFunctionCallIr(function_call, context);
}
