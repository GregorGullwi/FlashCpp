	std::vector<IrOperand> generateMemberFunctionCallIr(const MemberFunctionCallNode& memberFunctionCallNode) {
		std::vector<IrOperand> irOperands;
		irOperands.reserve(5 + memberFunctionCallNode.arguments().size() * 4);  // ret + name + this + ~4 per arg

		FLASH_LOG(Codegen, Debug, "=== generateMemberFunctionCallIr START ===");
		
		// Get the object expression
		ASTNode object_node = memberFunctionCallNode.object();

		// Special case: Immediate lambda invocation [](){}()
		// Check if the object is a LambdaExpressionNode (either directly or wrapped in ExpressionNode)
		const LambdaExpressionNode* lambda_ptr = nullptr;

		if (object_node.is<LambdaExpressionNode>()) {
			// Lambda stored directly
			lambda_ptr = &object_node.as<LambdaExpressionNode>();
		} else if (object_node.is<ExpressionNode>()) {
			const ExpressionNode& object_expr = object_node.as<ExpressionNode>();
			if (std::holds_alternative<LambdaExpressionNode>(object_expr)) {
				// Lambda wrapped in ExpressionNode
				lambda_ptr = &std::get<LambdaExpressionNode>(object_expr);
			}
		}

		if (lambda_ptr) {
			const LambdaExpressionNode& lambda = *lambda_ptr;

			// CRITICAL: First, collect the lambda for generation!
			// This ensures operator() and __invoke functions will be generated.
			// Without this, the lambda is never added to collected_lambdas_ and
			// its functions are never generated, causing linker errors.
			generateLambdaExpressionIr(lambda);
			
			// Check if this is a generic lambda (has auto parameters)
			bool is_generic = false;
			std::vector<size_t> auto_param_indices;
			size_t param_idx = 0;
			for (const auto& param_node : lambda.parameters()) {
				if (param_node.is<DeclarationNode>()) {
					const auto& param_decl = param_node.as<DeclarationNode>();
					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					if (param_type.type() == Type::Auto) {
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
				TypeSpecifierNode return_type_node(Type::Int, 0, 32, memberFunctionCallNode.called_from());
				if (lambda.return_type().has_value()) {
					const auto& ret_type = lambda.return_type()->as<TypeSpecifierNode>();
					return_type_node = ret_type;
					call_op.return_type = ret_type.type();
					call_op.return_size_in_bits = static_cast<int>(ret_type.size_in_bits());
				} else {
					// Default to int if no explicit return type
					call_op.return_type = Type::Int;
					call_op.return_size_in_bits = 32;
				}
				
				// Build TypeSpecifierNodes for parameters (needed for mangling)
				// For generic lambdas, we need to deduce auto parameters from arguments
				std::vector<TypeSpecifierNode> param_types;
				std::vector<TypeSpecifierNode> deduced_param_types;  // For generic lambdas
				
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
									if (type_node.type() == Type::Auto) {
										if (auto deduced = deduceLambdaClosureType(*symbol, decl->identifier_token())) {
											type_node = *deduced;
										}
									}
									arg_types.push_back(type_node);
								} else {
									// Default to int
									arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
								}
							} else {
								arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
							}
					} else if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
						arg_types.push_back(TypeSpecifierNode(Type::Bool, TypeQualifier::None, 8));
						} else if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
							const auto& literal = std::get<NumericLiteralNode>(arg_expr);
							arg_types.push_back(TypeSpecifierNode(literal.type(), TypeQualifier::None, 
								static_cast<unsigned char>(literal.sizeInBits())));
						} else {
							// For complex expressions, evaluate and get type
							auto operands = visitExpressionNode(arg_expr);
							Type type = std::get<Type>(operands[0]);
							int size = std::get<int>(operands[1]);
							arg_types.push_back(TypeSpecifierNode(type, TypeQualifier::None, static_cast<unsigned char>(size)));
						}
					});
					
					// Now build param_types with deduced types for auto parameters
					size_t arg_idx = 0;
					for (const auto& param_node : lambda.parameters()) {
						if (param_node.is<DeclarationNode>()) {
							const auto& param_decl = param_node.as<DeclarationNode>();
							const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
							
							if (param_type.type() == Type::Auto && arg_idx < arg_types.size()) {
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
										deduced_param_types[i]
									);
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
					false,  // not variadic
					""  // not a member function
				);
				
				call_op.function_name = StringTable::getOrInternStringHandle(mangled);
				call_op.is_member_function = false;
				call_op.is_variadic = false;  // Lambdas cannot be variadic in C++20


				// Add arguments
				memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
					const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
					auto argumentIrOperands = visitExpressionNode(arg_expr);
					if (std::holds_alternative<IdentifierNode>(arg_expr)) {
						const auto& identifier = std::get<IdentifierNode>(arg_expr);
						const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
						// Convert to TypedValue
						TypedValue arg;
						arg.type = type_node.type();
						arg.size_in_bits = static_cast<int>(type_node.size_in_bits());
						arg.value = StringTable::getOrInternStringHandle(identifier.name());
						call_op.args.push_back(arg);
					} else {
						// Convert argumentIrOperands to TypedValue
						TypedValue arg = toTypedValue(argumentIrOperands);
						call_op.args.push_back(arg);
					}
				});

				// Add the function call instruction with typed payload
				ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberFunctionCallNode.called_from()));

				// Return the result with actual return type from lambda
				return { call_op.return_type, call_op.return_size_in_bits, ret_var, 0ULL };
			}
			// For capturing lambdas, fall through to the regular member function call path
			// The closure object was already created by generateLambdaExpressionIr
		}

		// Regular member function call on an expression
		// Get the object's type
		std::string_view object_name;
		const DeclarationNode* object_decl = nullptr;
		TypeSpecifierNode object_type;

		// The object must be an ExpressionNode for regular member function calls
		if (!object_node.is<ExpressionNode>()) {
			throw std::runtime_error("Member function call object must be an ExpressionNode");
			return {};
		}

		const ExpressionNode& object_expr = object_node.as<ExpressionNode>();

		if (std::holds_alternative<IdentifierNode>(object_expr)) {
			const IdentifierNode& object_ident = std::get<IdentifierNode>(object_expr);
			object_name = object_ident.name();

			// Look up the object in the symbol table
			std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
			// Also check global symbol table if not found locally
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(object_name);
			}
			if (symbol.has_value()) {
				// Use helper to get DeclarationNode from either DeclarationNode or VariableDeclarationNode
				object_decl = get_decl_from_symbol(*symbol);
				if (object_decl) {
					object_type = object_decl->type_node().as<TypeSpecifierNode>();
					
					// If the type is 'auto', deduce the actual closure type from lambda initializer
					if (object_type.type() == Type::Auto) {
						if (auto deduced = deduceLambdaClosureType(*symbol, object_decl->identifier_token())) {
							object_type = *deduced;
						} else if (current_lambda_context_.isActive() && object_type.is_rvalue_reference()) {
							// For auto&& parameters inside lambdas (recursive lambda pattern),
							// assume the parameter has the closure type of the current lambda.
							// This handles: auto factorial = [](auto&& self, int n) { ... self(self, n-1); }
							// where self's type is deduced to __lambda_N&& when called
							auto type_it = gTypesByName.find(current_lambda_context_.closure_type);
							if (type_it != gTypesByName.end()) {
								const TypeInfo* closure_type = type_it->second;
								int closure_size = closure_type->getStructInfo()
									? closure_type->getStructInfo()->total_size * 8
									: 64;
								object_type = TypeSpecifierNode(
									Type::Struct,
									closure_type->type_index_,
									closure_size,
									object_decl->identifier_token()
								);
								// Preserve rvalue reference flag
								object_type.set_reference_qualifier(ReferenceQualifier::RValueReference);
							}
						}
					}
				}
			}
		} else if (std::holds_alternative<UnaryOperatorNode>(object_expr)) {
			// Handle dereference operator (from ptr->member transformation)
			const UnaryOperatorNode& unary_op = std::get<UnaryOperatorNode>(object_expr);
			if (unary_op.op() == "*") {
				// This is a dereference - get the pointer operand
				const ASTNode& operand_node = unary_op.get_operand();
				if (operand_node.is<ExpressionNode>()) {
					const ExpressionNode& operand_expr = operand_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(operand_expr)) {
						const IdentifierNode& ptr_ident = std::get<IdentifierNode>(operand_expr);
						object_name = ptr_ident.name();

						// Look up the pointer in the symbol table
						const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
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
		} else if (std::holds_alternative<MemberAccessNode>(object_expr)) {
			// Handle member access for function pointer calls
			// This handles both simple cases like "this->callback" and nested cases like "o.inner.callback"
			// When we see o.inner.callback():
			// - object_expr is o.inner (a MemberAccessNode)
			// - func_name (from function_declaration) is "callback"
			// We need to resolve the type of o.inner to get Inner, then check if callback is a function pointer member
			
			const MemberAccessNode& member_access = std::get<MemberAccessNode>(object_expr);
			const FunctionDeclarationNode& check_func_decl = memberFunctionCallNode.function_declaration();
			std::string_view called_func_name = check_func_decl.decl_node().identifier_token().value();
			
			// Try to resolve the type of the object (e.g., o.inner resolves to type Inner)
			const StructTypeInfo* resolved_struct_info = nullptr;
			const StructMember* resolved_member = nullptr;
			if (resolveMemberAccessType(member_access, resolved_struct_info, resolved_member)) {
				// We resolved the member access - now check if it's a struct type
				if (resolved_member && resolved_member->type == Type::Struct) {
					// Get the struct info for the member's type
					if (resolved_member->type_index < gTypeInfo.size()) {
						const TypeInfo& member_type_info = gTypeInfo[resolved_member->type_index];
						const StructTypeInfo* member_struct_info = member_type_info.getStructInfo();
						if (member_struct_info) {
							// Look for the called function name in this struct's members
							StringHandle func_name_handle = StringTable::getOrInternStringHandle(called_func_name);
							for (const auto& member : member_struct_info->members) {
								if (member.getName() == func_name_handle && member.type == Type::FunctionPointer) {
									// Found a function pointer member! Generate indirect call
									TempVar ret_var = var_counter.next();
									
									// Generate member access chain for o.inner.callback
									// First get o.inner
									std::vector<IrOperand> base_result = visitExpressionNode(object_expr);
									TempVar base_temp = std::get<TempVar>(base_result[2]);
									
									// Now access the callback member from that
									TempVar func_ptr_temp = var_counter.next();
									MemberLoadOp member_load;
									member_load.result.value = func_ptr_temp;
									member_load.result.type = Type::FunctionPointer;
									member_load.result.size_in_bits = static_cast<int>(member.size * 8);
									member_load.object = base_temp;
									member_load.member_name = func_name_handle;
									member_load.offset = static_cast<int>(member.offset);
									member_load.is_reference = member.is_reference();
									member_load.is_rvalue_reference = member.is_rvalue_reference();
									member_load.struct_type_info = &member_type_info;  // MemberLoadOp expects TypeInfo*
									
									ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
									
									// Build arguments for the indirect call
									std::vector<TypedValue> arguments;
									memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
										auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
										Type arg_type = std::get<Type>(argumentIrOperands[0]);
										int arg_size = std::get<int>(argumentIrOperands[1]);
										IrValue arg_value = std::visit([](auto&& arg) -> IrValue {
											using T = std::decay_t<decltype(arg)>;
											if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
											              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
												return arg;
											} else {
												return 0ULL;
											}
										}, argumentIrOperands[2]);
										arguments.push_back(TypedValue{arg_type, arg_size, arg_value});
									});
									
									IndirectCallOp op{
										.result = ret_var,
										.function_pointer = func_ptr_temp,
										.arguments = std::move(arguments)
									};
									ir_.addInstruction(IrInstruction(IrOpcode::IndirectCall, std::move(op), memberFunctionCallNode.called_from()));
									
									// TODO: Return type should be determined from the function pointer's signature
									// For now, return void as most callbacks are void-returning
									return { Type::Void, 0, ret_var, 0ULL };
								}
							}
							
							// Not a function pointer member - set object_type for regular member function lookup
							object_type = TypeSpecifierNode(Type::Struct, resolved_member->type_index,
							                                 resolved_member->size * 8, Token());  // size in bits
						}
					}
				}
			}
			
			// Fall back to simple base object handling for "this->member" pattern
			const ASTNode& base_node = member_access.object();
			if (base_node.is<ExpressionNode>()) {
				const ExpressionNode& base_expr = base_node.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(base_expr)) {
					const IdentifierNode& base_ident = std::get<IdentifierNode>(base_expr);
					std::string_view base_name = base_ident.name();
					
					// Look up the base object (e.g., "this")
					const std::optional<ASTNode> symbol = symbol_table.lookup(base_name);
					if (symbol.has_value()) {
						const DeclarationNode* base_decl = get_decl_from_symbol(*symbol);
						if (base_decl) {
							TypeSpecifierNode base_type_spec = base_decl->type_node().as<TypeSpecifierNode>();
							
							// If this is a pointer (like "this"), dereference it
							if (base_type_spec.pointer_levels().size() > 0) {
								base_type_spec.remove_pointer_level();
							}
							
							// Now base_type_spec should be the struct type
							if (base_type_spec.type() == Type::Struct) {
								object_type = base_type_spec;
								object_name = base_name;  // Use the base name for the call
							}
						}
					}
				}
			}
		}

		// For immediate lambda invocation, object_decl can be nullptr
		// In that case, we still need object_type to be set correctly

		// Special case: Handle namespace-qualified function calls that were incorrectly parsed as member function calls
		// This can happen when std::function() is parsed and the object is a namespace identifier
		if (std::holds_alternative<QualifiedIdentifierNode>(object_expr)) {
			// This is a namespace-qualified function call, not a member function call
			// Treat it as a regular function call instead
			return convertMemberCallToFunctionCall(memberFunctionCallNode);
		}
		
		// Verify this is a struct type BEFORE checking other cases
		// If object_type is not a struct, this might be a misparsed namespace-qualified function call
		if (object_type.type() != Type::Struct) {
			// The object is not a struct - this might be a namespace identifier or other non-struct type
			// Treat this as a regular function call instead of a member function call
			return convertMemberCallToFunctionCall(memberFunctionCallNode);
		}

		// Get the function declaration directly from the node (no need to look it up)
		const FunctionDeclarationNode& func_decl = memberFunctionCallNode.function_declaration();
		const DeclarationNode& func_decl_node = func_decl.decl_node();

		// Check if this is a virtual function call
		// Look up the struct type to check if the function is virtual
		bool is_virtual_call = false;
		int vtable_index = -1;

		size_t struct_type_index = object_type.type_index();
		const StructMemberFunction* called_member_func = nullptr;
		const StructTypeInfo* struct_info = nullptr;

		if (struct_type_index < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[struct_type_index];
			struct_info = type_info.getStructInfo();

			if (struct_info) {
				// Find the member function in the struct
				std::string_view func_name = func_decl_node.identifier_token().value();
				StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
				for (const auto& member_func : struct_info->member_functions) {
					if (member_func.getName() == func_name_handle) {
						called_member_func = &member_func;
						if (member_func.is_virtual) {
							is_virtual_call = true;
							vtable_index = member_func.vtable_index;
						}
						break;
					}
				}
				
				// If not found in the current class, search base classes
				const StructTypeInfo* declaring_struct = struct_info;
				if (!called_member_func && !struct_info->base_classes.empty()) {
					auto searchBaseClasses = [&](auto&& self, const StructTypeInfo* current_struct) -> void {
						for (const auto& base_spec : current_struct->base_classes) {
							if (base_spec.type_index < gTypeInfo.size()) {
								const TypeInfo& base_type_info = gTypeInfo[base_spec.type_index];
								if (base_type_info.isStruct()) {
									const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
									if (base_struct_info) {
										// Check member functions in base class
										for (const auto& member_func : base_struct_info->member_functions) {
											if (member_func.getName() == func_name_handle) {
												called_member_func = &member_func;
												declaring_struct = base_struct_info;  // Update to use base class name
												if (member_func.is_virtual) {
													is_virtual_call = true;
													vtable_index = member_func.vtable_index;
												}
												return; // Stop searching once found
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
				
				// Use declaring_struct instead of struct_info for mangled name generation
				// This ensures we use the correct class name where the function is declared
				struct_info = declaring_struct;
				
				// If not found as member function, check if it's a function pointer data member
				if (!called_member_func) {
					for (const auto& member : struct_info->members) {
						if (member.getName() == func_name_handle && member.type == Type::FunctionPointer) {
							// This is a call through a function pointer member!
							// Generate an indirect call instead of a member function call
							// TODO: Get actual return type from function signature stored in member's TypeSpecifierNode
							// For now, we assume int return type which works for most common cases
							
							TempVar ret_var = var_counter.next();
							std::vector<IrOperand> func_ptr_call_operands;
							func_ptr_call_operands.emplace_back(ret_var);
							
							// Get the function pointer member
							// We need to generate member access to get the pointer value
							TempVar func_ptr_temp = var_counter.next();
							
							// Generate member access IR to load the function pointer
							MemberLoadOp member_load;
							member_load.result.value = func_ptr_temp;
							member_load.result.type = member.type;
							member_load.result.size_in_bits = static_cast<int>(member.size * 8);  // Convert bytes to bits
							
							// Add object operand
							if (object_name.empty()) {
								// Use temp var
								// TODO: Need to handle object expression properly
								throw std::runtime_error("Function pointer member call on expression not yet supported");
							} else {
								member_load.object = StringTable::getOrInternStringHandle(object_name);
							}
							
							member_load.member_name = StringTable::getOrInternStringHandle(func_name);  // Member name
							member_load.offset = static_cast<int>(member.offset);  // Member offset
							member_load.is_reference = member.is_reference();
							member_load.is_rvalue_reference = member.is_rvalue_reference();
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));
							
							// Now add the indirect call with the function pointer temp var
							irOperands.emplace_back(func_ptr_temp);
							
							// Add arguments
							std::vector<TypedValue> arguments;
							memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
								auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
								// Extract type, size, and value from the expression result
								Type arg_type = std::get<Type>(argumentIrOperands[0]);
								int arg_size = std::get<int>(argumentIrOperands[1]);
								IrValue arg_value = std::visit([](auto&& arg) -> IrValue {
									using T = std::decay_t<decltype(arg)>;
									if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
												  std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
										return arg;
									} else {
										return 0ULL;
									}
								}, argumentIrOperands[2]);
								arguments.push_back(TypedValue{arg_type, arg_size, arg_value});
							});
						
							IndirectCallOp op{
								.result = ret_var,
								.function_pointer = func_ptr_temp,
								.arguments = std::move(arguments)
							};
							ir_.addInstruction(IrInstruction(IrOpcode::IndirectCall, std::move(op), memberFunctionCallNode.called_from()));
							
							// Return with function pointer's return type
							// TODO: Need to get the actual return type from the function signature stored in the member's TypeSpecifierNode
							// For now, assume int return type (common case)
							return { Type::Int, 32, ret_var, 0ULL };
						}
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
				std::vector<std::pair<Type, TypeIndex>> arg_types;
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
						arg_types.push_back({Type::Bool, 0});
					} else if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
						const NumericLiteralNode& lit = std::get<NumericLiteralNode>(arg_expr);
						// DEBUG removed
						arg_types.push_back({lit.type(), 0});
					} else if (std::holds_alternative<IdentifierNode>(arg_expr)) {
						// Look up variable type
						const IdentifierNode& ident = std::get<IdentifierNode>(arg_expr);
						// DEBUG removed
						auto symbol_opt = symbol_table.lookup(ident.name());
						if (symbol_opt.has_value() && symbol_opt->is<DeclarationNode>()) {
							const DeclarationNode& decl = symbol_opt->as<DeclarationNode>();
							const TypeSpecifierNode& type = decl.type_node().as<TypeSpecifierNode>();
							// DEBUG removed
							arg_types.push_back({type.type(), type.type_index()});
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
					
					std::vector<TemplateArgument> template_args;
					for (const auto& [arg_type, arg_type_index] : arg_types) {
						template_args.push_back(TemplateArgument::makeType(arg_type, arg_type_index));
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
							std::vector<std::string_view> eval_param_names;
							for (const auto& tparam_node : template_func.template_parameters()) {
								if (tparam_node.is<TemplateParameterNode>()) {
									eval_param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
								}
							}
							
							// Convert arg_types to TemplateTypeArg for evaluation
							std::vector<TemplateTypeArg> type_args;
							for (const auto& [arg_type, arg_type_index] : arg_types) {
								TemplateTypeArg type_arg;
								type_arg.base_type = arg_type;
								type_arg.type_index = arg_type_index;
								type_args.push_back(type_arg);
							}
							
							// Evaluate the constraint with the template arguments
							auto constraint_result = evaluateConstraint(
								requires_clause.constraint_expr(), type_args, eval_param_names);
							
							if (!constraint_result.satisfied) {
								// Constraint not satisfied - report detailed error
								// Build template arguments string
								std::string args_str;
								for (size_t i = 0; i < arg_types.size(); ++i) {
									if (i > 0) args_str += ", ";
									args_str += std::string(TemplateRegistry::typeToString(arg_types[i].first));
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
						std::vector<std::string_view> param_names;
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
				throw std::runtime_error("Access control violation");
				return { Type::Int, 32, TempVar{0} };
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
			vcall_op.result.type = return_type.type();
			// For pointer return types, use 64 bits (pointer size), otherwise use the type's size
			// Also handle reference return types as pointers (64 bits)
			FLASH_LOG(Codegen, Debug, "VirtualCall return_type: ptr_depth=", return_type.pointer_depth(),
			          " is_ptr=", return_type.is_pointer(),
			          " is_ref=", return_type.is_reference(),
			          " is_rref=", return_type.is_rvalue_reference(),
			          " size_bits=", return_type.size_in_bits());
			if (return_type.pointer_depth() > 0 || return_type.is_pointer() || return_type.is_reference() || return_type.is_rvalue_reference()) {
				vcall_op.result.size_in_bits = 64;
			} else {
				vcall_op.result.size_in_bits = static_cast<int>(return_type.size_in_bits());
			}
			FLASH_LOG(Codegen, Debug, "VirtualCall result.size_in_bits=", vcall_op.result.size_in_bits);
			vcall_op.result.value = ret_var;
			vcall_op.object_type = object_type.type();
			vcall_op.object_size = static_cast<int>(object_type.size_in_bits());
			vcall_op.object = StringTable::getOrInternStringHandle(object_name);
			vcall_op.vtable_index = vtable_index;
			// Set is_pointer_access based on whether the object is accessed through a pointer (ptr->method)
			// or through a reference (ref.method()). References are implemented as pointers internally,
			// so they need the same treatment as pointer access for virtual dispatch.
			vcall_op.is_pointer_access = (object_type.pointer_depth() > 0) || object_type.is_reference() || object_type.is_rvalue_reference();

			// Generate IR for function arguments
			memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
				auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
				
				// For variables, we need to add the type and size
				if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
					const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
					const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
					const auto& decl_node = symbol->as<DeclarationNode>();
					const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
					
					TypedValue tv;
					tv.type = type_node.type();
					tv.size_in_bits = static_cast<int>(type_node.size_in_bits());
					tv.value = StringTable::getOrInternStringHandle(identifier.name());
					vcall_op.arguments.push_back(tv);
				}
				else {
					// Convert from IrOperand to TypedValue
					// Format: [type, size, value]
					if (argumentIrOperands.size() >= 3) {
						TypedValue tv = toTypedValue(argumentIrOperands);
						vcall_op.arguments.push_back(tv);
					}
				}
			});

			// Add the virtual call instruction
			ir_.addInstruction(IrInstruction(IrOpcode::VirtualCall, std::move(vcall_op), memberFunctionCallNode.called_from()));
		} else {
			// Generate regular (non-virtual) member function call using CallOp typed payload
			
			// Vector to hold deduced parameter types (populated for generic lambdas)
			std::vector<TypeSpecifierNode> param_types;
			
			// Check if this is an instantiated template function
			std::string_view func_name = func_decl_node.identifier_token().value();
			StringHandle function_name;
			
			// Check if this is a member function - use struct_info to determine
			if (struct_info) {
				// For nested classes, we need the fully qualified name from TypeInfo
				auto struct_name = struct_info->getName();
				auto type_it = gTypesByName.find(struct_name);
				if (type_it != gTypesByName.end()) {
					struct_name = type_it->second->name();
				}
				auto qualified_template_name = StringTable::getOrInternStringHandle(StringBuilder().append(struct_name).append("::"sv).append(func_name));
				
				// Check if this is a template that has been instantiated
				auto template_opt = gTemplateRegistry.lookupTemplate(qualified_template_name);
				if (template_opt.has_value() && template_opt->is<TemplateFunctionDeclarationNode>()) {
					// This is a member function template - use the mangled name
					
					// Deduce template arguments from call arguments
					std::vector<TemplateArgument> template_args;
					memberFunctionCallNode.arguments().visit([&](ASTNode argument) {
						if (!argument.is<ExpressionNode>()) return;
						const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
						
						// Get type of argument
						if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
							template_args.push_back(TemplateArgument::makeType(Type::Bool));
						} else if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
							const NumericLiteralNode& lit = std::get<NumericLiteralNode>(arg_expr);
							template_args.push_back(TemplateArgument::makeType(lit.type()));
						} else if (std::holds_alternative<IdentifierNode>(arg_expr)) {
							const IdentifierNode& ident = std::get<IdentifierNode>(arg_expr);
							auto symbol_opt = symbol_table.lookup(ident.name());
							if (symbol_opt.has_value() && symbol_opt->is<DeclarationNode>()) {
								const DeclarationNode& decl = symbol_opt->as<DeclarationNode>();
								const TypeSpecifierNode& type = decl.type_node().as<TypeSpecifierNode>();
								template_args.push_back(TemplateArgument::makeType(type.type()));
							}
						}
					});
					
					// Generate the mangled name
					std::string_view mangled_func_name = gTemplateRegistry.mangleTemplateName(func_name, template_args);
					
					// Build qualified function name with mangled template name
					function_name = StringTable::getOrInternStringHandle(StringBuilder().append(struct_name).append("::"sv).append(mangled_func_name));
				} else {
					// Regular member function (not a template) - generate proper mangled name
					// Use the function declaration from struct_info if available (has correct parameters)
					const FunctionDeclarationNode* func_for_mangling = &func_decl;
					if (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) {
						func_for_mangling = &called_member_func->function_decl.as<FunctionDeclarationNode>();
					}
					
					// Get return type and parameter types from the function declaration
					const auto& return_type_node = func_for_mangling->decl_node().type_node().as<TypeSpecifierNode>();
					
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
										if (type_node.type() == Type::Auto) {
											if (auto deduced = deduceLambdaClosureType(*symbol, decl->identifier_token())) {
												type_node = *deduced;
											} else if (current_lambda_context_.isActive() && type_node.is_rvalue_reference()) {
												// For auto&& parameters inside lambdas (recursive lambda pattern),
												// assume the parameter has the closure type of the current lambda.
												// This handles: auto factorial = [](auto&& self, int n) { ... self(self, n-1); }
												auto type_it = gTypesByName.find(current_lambda_context_.closure_type);
												if (type_it != gTypesByName.end()) {
													const TypeInfo* closure_type = type_it->second;
													int closure_size = closure_type->getStructInfo()
														? closure_type->getStructInfo()->total_size * 8
														: 64;
													type_node = TypeSpecifierNode(
														Type::Struct,
														closure_type->type_index_,
														closure_size,
														decl->identifier_token()
													);
													// Preserve rvalue reference flag
													type_node.set_reference_qualifier(ReferenceQualifier::RValueReference);
												}
											}
										}
										arg_types.push_back(type_node);
									} else {
										arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
									}
								} else {
									arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
								}
					} else if (std::holds_alternative<BoolLiteralNode>(arg_expr)) {
						arg_types.push_back(TypeSpecifierNode(Type::Bool, TypeQualifier::None, 8));
							} else if (std::holds_alternative<NumericLiteralNode>(arg_expr)) {
								const auto& literal = std::get<NumericLiteralNode>(arg_expr);
								arg_types.push_back(TypeSpecifierNode(literal.type(), TypeQualifier::None, 
									static_cast<unsigned char>(literal.sizeInBits())));
							} else {
								// Default to int for complex expressions
								arg_types.push_back(TypeSpecifierNode(Type::Int, TypeQualifier::None, 32));
							}
						});
						
						// Now build param_types with deduced types for auto parameters
						size_t arg_idx = 0;
						for (const auto& param_node : func_for_mangling->parameter_nodes()) {
							if (param_node.is<DeclarationNode>()) {
								const auto& param_decl = param_node.as<DeclarationNode>();
								const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
								
								if (param_type.type() == Type::Auto && arg_idx < arg_types.size()) {
									// Deduce type from argument, preserving reference flags from auto&& parameter
									TypeSpecifierNode deduced_type = arg_types[arg_idx];
									deduced_type.set_reference_qualifier(param_type.reference_qualifier());
									param_types.push_back(deduced_type);
									
									// Also store the deduced type in LambdaInfo for use by generateLambdaOperatorCallFunction
									for (auto& lambda_info : collected_lambdas_) {
										if (lambda_info.closure_type_name == struct_name) {
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
					
					// Generate proper mangled name including parameter types
					std::string_view mangled = generateMangledNameForCall(
						func_name,
						return_type_node,
						param_types,
						func_for_mangling->is_variadic(),
						StringTable::getStringView(struct_name)
					);
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
			if (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) {
				return_type_ptr = &called_member_func->function_decl.as<FunctionDeclarationNode>().decl_node().type_node().as<TypeSpecifierNode>();
			} else {
				return_type_ptr = &func_decl_node.type_node().as<TypeSpecifierNode>();
			}
			const auto& return_type = *return_type_ptr;
			call_op.return_type = return_type.type();
			// For reference return types, use 64-bit size (pointer size) since references are returned as pointers
			call_op.return_size_in_bits = (return_type.pointer_depth() > 0 || return_type.is_reference()) ? 64 : static_cast<int>(return_type.size_in_bits());
			call_op.is_member_function = true;
			
			// Get the actual function declaration to check if it's variadic
			const FunctionDeclarationNode* actual_func_decl_for_variadic = nullptr;
			if (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) {
				actual_func_decl_for_variadic = &called_member_func->function_decl.as<FunctionDeclarationNode>();
			} else {
				actual_func_decl_for_variadic = &func_decl;
			}
			call_op.is_variadic = actual_func_decl_for_variadic->is_variadic();
			
			// Detect if calling a member function that returns struct by value (needs hidden return parameter for RVO)
			bool returns_struct_by_value = returnsStructByValue(return_type.type(), return_type.pointer_depth(), return_type.is_reference());
			bool needs_hidden_return_param = needsHiddenReturnParam(return_type.type(), return_type.pointer_depth(), return_type.is_reference(), return_type.size_in_bits(), context_->isLLP64());
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Member function call check: returns_struct={}, size={}, threshold={}, needs_hidden={}",
				returns_struct_by_value, return_type.size_in_bits(), getStructReturnThreshold(context_->isLLP64()), needs_hidden_return_param);
			
			if (needs_hidden_return_param) {
				call_op.return_slot = ret_var;  // The result temp var serves as the return slot
				call_op.return_type_index = return_type.type_index();
				
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Member function call {} returns struct by value (size={} bits) - using return slot (temp_{})",
					StringTable::getStringView(function_name), return_type.size_in_bits(), ret_var.var_number);
			} else if (returns_struct_by_value) {
				// Small struct return - no return slot needed
				call_op.return_type_index = return_type.type_index();
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Member function call {} returns small struct by value (size={} bits) - will return in RAX",
					StringTable::getStringView(function_name), return_type.size_in_bits());
			}
			
			// Add the object as the first argument (this pointer)
			// The 'this' pointer is always 64 bits (pointer size on x64), regardless of struct size
			// This is critical for empty structs (size 0) which still need a valid address
			IrValue this_arg_value;
			bool object_is_pointer_like = object_type.pointer_depth() > 0 || object_type.is_reference() || object_type.is_rvalue_reference();
			if (object_is_pointer_like) {
				// For pointer/reference objects, pass through directly
				this_arg_value = IrValue(StringTable::getOrInternStringHandle(object_name));
			} else {
				// For object values, take the address so member functions receive a pointer to the object
				TempVar this_addr = var_counter.next();
				AddressOfOp addr_op;
				addr_op.result = this_addr;
				addr_op.operand.type = object_type.type();
				addr_op.operand.size_in_bits = static_cast<int>(object_type.size_in_bits());
				addr_op.operand.pointer_depth = static_cast<int>(object_type.pointer_depth());
				addr_op.operand.value = StringTable::getOrInternStringHandle(object_name);
				ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), memberFunctionCallNode.called_from()));
				this_arg_value = IrValue(this_addr);
			}
			call_op.args.push_back(TypedValue{
				.type = object_type.type(),
				.size_in_bits = 64,  // Pointer size - always 64 bits on x64 architecture
				.value = this_arg_value
			});

			// Generate IR for function arguments and add to CallOp
			size_t arg_index = 0;
		
			// Get the actual function declaration with parameters from struct_info if available
			const FunctionDeclarationNode* actual_func_decl = nullptr;
			if (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) {
				actual_func_decl = &called_member_func->function_decl.as<FunctionDeclarationNode>();
			} else {
				actual_func_decl = &func_decl;
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
			
				// For variables (identifiers), handle specially to avoid unnecessary dereferences
				// when passing reference arguments to reference parameters
				if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
					const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
					const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
					
					// Check if this is a function being passed as a function pointer argument
					if (symbol.has_value() && symbol->is<FunctionDeclarationNode>()) {
						// Function being passed as function pointer - just pass its name
						call_op.args.push_back(TypedValue{
							.type = Type::FunctionPointer,
							.size_in_bits = 64,  // Pointer size
							.value = IrValue(StringTable::getOrInternStringHandle(identifier.name()))
						});
					} else if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
				
						// Check if parameter expects a reference
						if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
							// Parameter expects a reference - pass the address of the argument
							if (type_node.is_reference() || type_node.is_rvalue_reference()) {
								// Argument is already a reference - just pass it through
								// Use 64-bit pointer size since references are passed as pointers
								call_op.args.push_back(TypedValue{
									.type = type_node.type(),
									.size_in_bits = 64,  // Reference is passed as pointer (64 bits on x64)
									.value = IrValue(StringTable::getOrInternStringHandle(identifier.name())),
									.ref_qualifier = ReferenceQualifier::LValueReference
								});
							} else {
								// Argument is a value - take its address
								TempVar addr_var = emitAddressOf(type_node.type(), static_cast<int>(type_node.size_in_bits()), IrValue(StringTable::getOrInternStringHandle(identifier.name())));
						
								// Pass the address with pointer size
								call_op.args.push_back(TypedValue{
									.type = type_node.type(),
									.size_in_bits = 64,  // Pointer size
									.value = IrValue(addr_var),
									.ref_qualifier = ReferenceQualifier::LValueReference
								});
							}
						} else {
							// Regular pass by value
							call_op.args.push_back(TypedValue{
								.type = type_node.type(),
								.size_in_bits = static_cast<int>(type_node.size_in_bits()),
								.value = IrValue(StringTable::getOrInternStringHandle(identifier.name()))
							});
						}
					} else if (symbol.has_value() && symbol->is<VariableDeclarationNode>()) {
						// Handle VariableDeclarationNode (local variables)
						const auto& var_decl = symbol->as<VariableDeclarationNode>();
						const auto& decl_node = var_decl.declaration();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
				
						// Check if parameter expects a reference
						if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
							// Parameter expects a reference - pass the address of the argument
							if (type_node.is_reference() || type_node.is_rvalue_reference()) {
								// Argument is already a reference - just pass it through
								// Use 64-bit pointer size since references are passed as pointers
								call_op.args.push_back(TypedValue{
									.type = type_node.type(),
									.size_in_bits = 64,  // Reference is passed as pointer (64 bits on x64)
									.value = IrValue(StringTable::getOrInternStringHandle(identifier.name())),
									.ref_qualifier = ReferenceQualifier::LValueReference
								});
							} else {
								// Argument is a value - take its address
								TempVar addr_var = emitAddressOf(type_node.type(), static_cast<int>(type_node.size_in_bits()), IrValue(StringTable::getOrInternStringHandle(identifier.name())));
						
								// Pass the address with pointer size
								call_op.args.push_back(TypedValue{
									.type = type_node.type(),
									.size_in_bits = 64,  // Pointer size
									.value = IrValue(addr_var),
									.ref_qualifier = ReferenceQualifier::LValueReference
								});
							}
						} else {
							// Regular pass by value
							call_op.args.push_back(TypedValue{
								.type = type_node.type(),
								.size_in_bits = static_cast<int>(type_node.size_in_bits()),
								.value = IrValue(StringTable::getOrInternStringHandle(identifier.name()))
							});
						}
					} else {
						// Unknown symbol type - fall back to visitExpressionNode
						auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
						call_op.args.push_back(toTypedValue(std::span<const IrOperand>(argumentIrOperands.data(), argumentIrOperands.size())));
					}
				}
				else {
					// Not an identifier - call visitExpressionNode to get the value
					auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
					
					// Check if parameter expects a reference and argument is a literal
					if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference())) {
						// Parameter expects a reference, but argument is not an identifier
						// We need to materialize the value into a temporary and pass its address
						
						// Check if this is a literal value (has unsigned long long or double in operand[2])
						bool is_literal = (argumentIrOperands.size() >= 3 && 
						                  (std::holds_alternative<unsigned long long>(argumentIrOperands[2]) ||
						                   std::holds_alternative<double>(argumentIrOperands[2])));
						
						if (is_literal) {
							// Materialize the literal into a temporary variable
							Type literal_type = std::get<Type>(argumentIrOperands[0]);
							int literal_size = std::get<int>(argumentIrOperands[1]);
							
							// Create a temporary variable to hold the literal value
							TempVar temp_var = var_counter.next();
							
							// Generate an assignment IR to store the literal using typed payload
							AssignmentOp assign_op;
							assign_op.result = temp_var;  // unused but required
							
							// Convert IrOperand to IrValue for the literal
							IrValue rhs_value;
							if (std::holds_alternative<unsigned long long>(argumentIrOperands[2])) {
								rhs_value = std::get<unsigned long long>(argumentIrOperands[2]);
							} else if (std::holds_alternative<double>(argumentIrOperands[2])) {
								rhs_value = std::get<double>(argumentIrOperands[2]);
							}
							
							// Create TypedValue for lhs and rhs
							assign_op.lhs = TypedValue{literal_type, literal_size, temp_var};
							assign_op.rhs = TypedValue{literal_type, literal_size, rhs_value};
							
							ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
							
							// Now take the address of the temporary
							TempVar addr_var = emitAddressOf(literal_type, literal_size, IrValue(temp_var));
							
							// Pass the address
							call_op.args.push_back(TypedValue{
								.type = literal_type,
								.size_in_bits = 64,  // Pointer size
								.value = IrValue(addr_var),
								.ref_qualifier = ReferenceQualifier::LValueReference
							});
						} else {
							// Not a literal (expression result in a TempVar) - take its address
							if (argumentIrOperands.size() >= 3 && std::holds_alternative<TempVar>(argumentIrOperands[2])) {
								Type expr_type = std::get<Type>(argumentIrOperands[0]);
								int expr_size = std::get<int>(argumentIrOperands[1]);
								TempVar expr_var = std::get<TempVar>(argumentIrOperands[2]);
								
								TempVar addr_var = emitAddressOf(expr_type, expr_size, IrValue(expr_var));
								
								call_op.args.push_back(TypedValue{
									.type = expr_type,
									.size_in_bits = 64,  // Pointer size
									.value = IrValue(addr_var),
									.ref_qualifier = ReferenceQualifier::LValueReference
								});
							} else {
								// Fallback - just pass through
								call_op.args.push_back(toTypedValue(std::span<const IrOperand>(argumentIrOperands.data(), argumentIrOperands.size())));
							}
						}
					} else {
						// Parameter doesn't expect a reference - pass through as-is
						call_op.args.push_back(toTypedValue(std::span<const IrOperand>(argumentIrOperands.data(), argumentIrOperands.size())));
					}
				}
			
				arg_index++;
			});
			
			// Add the function call instruction with typed payload
			ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberFunctionCallNode.called_from()));
		}

		// Return the result variable with its type and size
		// If we found the actual member function from the struct, use its return type
		// Otherwise fall back to the placeholder function declaration
		const auto& return_type = (called_member_func && called_member_func->function_decl.is<FunctionDeclarationNode>()) 
			? called_member_func->function_decl.as<FunctionDeclarationNode>().decl_node().type_node().as<TypeSpecifierNode>()
			: func_decl_node.type_node().as<TypeSpecifierNode>();
		
		// For pointer/reference return types, use 64 bits (pointer size on x64)
		// Otherwise, use the type's natural size
		int return_size_bits = (return_type.pointer_depth() > 0 || return_type.is_reference() || return_type.is_rvalue_reference())
			? 64
			: static_cast<int>(return_type.size_in_bits());
		
		return { return_type.type(), return_size_bits, ret_var, static_cast<unsigned long long>(return_type.type_index()) };
	}

