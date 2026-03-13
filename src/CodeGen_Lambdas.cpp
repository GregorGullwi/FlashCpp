#include "CodeGen.h"

	LambdaInfo AstToIr::collectLambdaForDeferredGeneration(const LambdaExpressionNode& lambda) {
		LambdaInfo info;
		info.lambda_id = lambda.lambda_id();

		info.closure_type_name = StringBuilder()
				.append("__lambda_")
				.append(static_cast<int64_t>(lambda.lambda_id()))
				.commit();

		info.operator_call_name = StringBuilder()
				.append(info.closure_type_name)
				.append("_operator_call")
				.commit();

		info.invoke_name = StringBuilder()
				.append(info.closure_type_name)
				.append("_invoke")
				.commit();

		info.conversion_op_name = StringBuilder()
				.append(info.closure_type_name)
				.append("_conversion")
				.commit();

		info.lambda_token = lambda.lambda_token();
		info.enclosing_struct_name = current_struct_name_.isValid() ? StringTable::getStringView(current_struct_name_) : std::string_view();
		if (current_struct_name_.isValid()) {
			auto type_it = gTypesByName.find(current_struct_name_);
			if (type_it != gTypesByName.end()) {
				info.enclosing_struct_type_index = type_it->second->type_index_;
			}
		} else if (current_lambda_context_.enclosing_struct_type_index > 0 &&
			current_lambda_context_.enclosing_struct_type_index < gTypeInfo.size()) {
			info.enclosing_struct_type_index = current_lambda_context_.enclosing_struct_type_index;
			info.enclosing_struct_name = StringTable::getStringView(gTypeInfo[info.enclosing_struct_type_index].name());
		}

		info.lambda_body = lambda.body();
		info.captures = lambda.captures();
		info.is_mutable = lambda.is_mutable();

		for (const auto& capture : lambda.captures()) {
			if (capture.is_capture_all()) {
				continue;
			}
			if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
				capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis ||
				capture.has_initializer()) {
				continue;
			}

			std::string_view var_name = capture.identifier_name();
			std::optional<ASTNode> var_symbol = symbol_table.lookup(var_name);

			if (var_symbol.has_value()) {
				info.captured_var_decls.push_back(*var_symbol);
			} else if (current_lambda_context_.isActive()) {
				StringHandle var_name_handle = StringTable::getOrInternStringHandle(var_name);
				auto capture_type_it = current_lambda_context_.capture_types.find(var_name_handle);
				if (current_lambda_context_.captures.count(var_name_handle) > 0 &&
					capture_type_it != current_lambda_context_.capture_types.end()) {
					ASTNode type_node = ASTNode::emplace_node<TypeSpecifierNode>(capture_type_it->second);
					DeclarationNode& synthetic_decl = gChunkedAnyStorage.emplace_back<DeclarationNode>(type_node, capture.identifier_token());
					info.captured_var_decls.push_back(ASTNode::emplace_node<DeclarationNode>(synthetic_decl));
				} else {
					throw CompileError(std::string(StringBuilder()
						.append("Lambda capture variable not found in scope: '")
						.append(var_name)
						.append("'")
						.commit()));
				}
			} else {
				throw CompileError(std::string(StringBuilder()
					.append("Lambda capture variable not found in scope: '")
					.append(var_name)
					.append("'")
					.commit()));
			}
		}

		info.return_type = Type::Void;
		info.return_size = 0;
		info.return_type_index = TypeIndex{};
		info.returns_reference = false;
		if (lambda.return_type().has_value()) {
			const auto& ret_type_node = lambda.return_type()->as<TypeSpecifierNode>();
			info.return_type = ret_type_node.type();
			info.return_size = ret_type_node.size_in_bits();
			info.return_type_index = ret_type_node.type_index();
			info.returns_reference = ret_type_node.is_reference();
			if (info.returns_reference) {
				info.return_size = 64;
			}
		}

		size_t param_index = 0;
		for (const auto& param : lambda.parameters()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();

				if (param_type.type() == Type::Auto) {
					info.is_generic = true;
					info.auto_param_indices.push_back(param_index);
				}

				info.parameters.emplace_back(
					param_type.type(),
					param_type.size_in_bits(),
					static_cast<int>(param_type.pointer_levels().size()),
					std::string(param_decl.identifier_token().value())
				);
				info.parameter_nodes.push_back(param);
			}
			param_index++;
		}

		collected_lambdas_.push_back(std::move(info));
		return collected_lambdas_.back();
	}

	ExprResult AstToIr::generateLambdaExpressionIr(const LambdaExpressionNode& lambda, std::string_view target_var_name) {
		// Collect lambda information for deferred generation
		// Following Clang's approach: generate closure class, operator(), __invoke, and conversion operator
		// If target_var_name is provided, use it as the closure variable name (for variable declarations)
		// Otherwise, create a temporary __closure_N variable

		LambdaInfo lambda_info = collectLambdaForDeferredGeneration(lambda);

		// Look up the closure type (registered during parsing)
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(lambda_info.closure_type_name));
		if (type_it == gTypesByName.end()) {
			// Error: closure type not found
			TempVar dummy = var_counter.next();
			return makeExprResult(Type::Int, 32, IrOperand{dummy});
		}

		const TypeInfo* closure_type = type_it->second;

		// Use target variable name if provided, otherwise create a temporary closure variable
		std::string_view closure_var_name;
		if (!target_var_name.empty()) {
			// Use the target variable name directly
			// We MUST emit VariableDecl here before any MemberStore operations
			closure_var_name = target_var_name;
			
			// Declare the closure variable with the target name
			VariableDeclOp lambda_decl_op;
			lambda_decl_op.type = Type::Struct;
			lambda_decl_op.size_in_bits = static_cast<int>(closure_type->getStructInfo()->total_size * 8);
			lambda_decl_op.var_name = StringTable::getOrInternStringHandle(closure_var_name);
			lambda_decl_op.custom_alignment = 0;
			lambda_decl_op.ref_qualifier = CVReferenceQualifier::None;
			lambda_decl_op.is_array = false;
			ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(lambda_decl_op), lambda.lambda_token()));
		} else {
			// Create a temporary closure variable name
			closure_var_name = StringBuilder()
				.append("__closure_")
				.append(static_cast<int64_t>(lambda_info.lambda_id))
				.commit();

			// Declare the closure variable
			VariableDeclOp lambda_decl_op;
			lambda_decl_op.type = Type::Struct;
			lambda_decl_op.size_in_bits = static_cast<int>(closure_type->getStructInfo()->total_size * 8);
			lambda_decl_op.var_name = StringTable::getOrInternStringHandle(closure_var_name);
			lambda_decl_op.custom_alignment = 0;
			lambda_decl_op.ref_qualifier = CVReferenceQualifier::None;
			lambda_decl_op.is_array = false;
			ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(lambda_decl_op), lambda.lambda_token()));
		}

		// Now initialize captured members
		// The key insight: we need to generate the initialization code that will be
		// executed during IR conversion, after the variable has been added to scope
		if (!lambda_info.captures.empty()) {
			const StructTypeInfo* struct_info = closure_type->getStructInfo();
			if (struct_info) {
				size_t capture_index = 0;
				for (const auto& capture : lambda_info.captures) {
					if (capture.is_capture_all()) {
						continue;
					}
					
					// Handle [this] capture - stores pointer to enclosing object
					if (capture.kind() == LambdaCaptureNode::CaptureKind::This) {
						const StructMember* member = struct_info->findMember("__this");
						if (member) {
							// Store the enclosing 'this' pointer in the closure
							// Use the 'this' variable name to properly resolve to the member function's this parameter
							MemberStoreOp store_this;
							store_this.value.type = Type::Void;
							store_this.value.size_in_bits = 64;
							store_this.value.value = StringTable::getOrInternStringHandle("this");
							store_this.object = StringTable::getOrInternStringHandle(closure_var_name);
							store_this.member_name = StringTable::getOrInternStringHandle("__this");
							store_this.offset = static_cast<int>(member->offset);
							store_this.ref_qualifier = CVReferenceQualifier::None;
							store_this.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_this), lambda.lambda_token()));
						}
						continue;
					}
					
					// Handle [*this] capture - stores copy of entire enclosing object
					if (capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
						// For [*this], we need to copy the entire object into the closure
						// The closure should have a member named "__copy_this" of the enclosing struct type
						const StructMember* member = struct_info->findMember("__copy_this");
						if (member && lambda_info.enclosing_struct_type_index > 0) {
							// Copy each member of the enclosing struct into __copy_this
							const TypeInfo* enclosing_type = nullptr;
							for (const auto& ti : gTypeInfo) {
								if (ti.type_index_ == lambda_info.enclosing_struct_type_index) {
									enclosing_type = &ti;
									break;
								}
							}
							if (enclosing_type && enclosing_type->getStructInfo()) {
								const StructTypeInfo* enclosing_struct = enclosing_type->getStructInfo();
								int copy_base_offset = static_cast<int>(member->offset);

								for (const auto& enclosing_member : enclosing_struct->members) {
									// Load from original 'this'
									TempVar loaded_value = var_counter.next();
									MemberLoadOp load_op;
									load_op.result.value = loaded_value;
									load_op.result.type = enclosing_member.type;
									load_op.result.size_in_bits = static_cast<int>(enclosing_member.size * 8);
									load_op.object = StringTable::getOrInternStringHandle("this");
									load_op.member_name = enclosing_member.getName();
									load_op.offset = static_cast<int>(enclosing_member.offset);
									load_op.ref_qualifier = ((enclosing_member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((enclosing_member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
									load_op.struct_type_info = nullptr;
									ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), lambda.lambda_token()));

									// Store into closure->__copy_this at the appropriate offset
									MemberStoreOp store_copy_this;
									store_copy_this.value.type = enclosing_member.type;
									store_copy_this.value.size_in_bits = static_cast<int>(enclosing_member.size * 8);
									store_copy_this.value.value = loaded_value;
									store_copy_this.object = StringTable::getOrInternStringHandle(closure_var_name);
									store_copy_this.member_name = StringTable::getOrInternStringHandle("__copy_this");
									store_copy_this.offset = copy_base_offset + static_cast<int>(enclosing_member.offset);
									store_copy_this.ref_qualifier = ((enclosing_member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((enclosing_member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
									store_copy_this.struct_type_info = nullptr;
									ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_copy_this), lambda.lambda_token()));
								}
							}
						}
						continue;
					}

					std::string_view var_name = capture.identifier_name();  // Already a persistent string_view from AST
					StringHandle var_name_str = StringTable::getOrInternStringHandle(var_name);  // Single conversion for both uses below
					const StructMember* member = struct_info->findMember(var_name);

					if (member && (capture.has_initializer() || capture_index < lambda_info.captured_var_decls.size())) {
						// Check if this variable is a captured variable from an enclosing lambda
						bool is_captured_from_enclosing = current_lambda_context_.isActive() &&
						current_lambda_context_.captures.count(var_name_str) > 0;

						// Handle init-captures
						if (capture.has_initializer()) {
							// Init-capture: evaluate the initializer expression and store it
							const ASTNode& init_node = *capture.initializer();
							ExprResult init_result = visitExpressionNode(init_node.as<ExpressionNode>());

							IrOperand init_value = init_result.value;

							// For init-capture by reference [&y = x], we need to store the address of x
							if (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference) {
								// Get the type info from the init result
								Type init_type = init_result.type;
								int init_size = init_result.size_in_bits;

								// Generate AddressOf for the initializer
								TempVar addr_temp = var_counter.next();
								AddressOfOp addr_op;
								addr_op.result = addr_temp;
								addr_op.operand.type = init_type;
								addr_op.operand.size_in_bits = init_size;
								addr_op.operand.pointer_depth = PointerDepth{};

								if (std::holds_alternative<StringHandle>(init_value)) {
									addr_op.operand.value = std::get<StringHandle>(init_value);
								} else if (std::holds_alternative<TempVar>(init_value)) {
									addr_op.operand.value = std::get<TempVar>(init_value);
								} else {
									// For other types, skip
									continue;
								}

								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), lambda.lambda_token()));

								// Store the address in the closure member
								MemberStoreOp member_store;
								member_store.value.type = init_type;
								member_store.value.size_in_bits = 64; // pointer size
								member_store.value.value = addr_temp;
								member_store.object = StringTable::getOrInternStringHandle(closure_var_name);
								member_store.member_name = member->getName();
								member_store.offset = static_cast<int>(member->offset);
								member_store.ref_qualifier = CVReferenceQualifier::LValueReference;
								member_store.struct_type_info = nullptr;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), lambda.lambda_token()));
							} else {
								// Init-capture by value [x = expr] - store the value directly
								MemberStoreOp member_store;
								member_store.value.type = member->type;
								member_store.value.size_in_bits = static_cast<int>(member->size * 8);

								// Convert IrOperand to IrValue
								if (std::holds_alternative<TempVar>(init_value)) {
									member_store.value.value = std::get<TempVar>(init_value);
								} else if (std::holds_alternative<int>(init_value)) {
									member_store.value.value = static_cast<unsigned long long>(std::get<int>(init_value));
								} else if (std::holds_alternative<unsigned long long>(init_value)) {
									member_store.value.value = std::get<unsigned long long>(init_value);
								} else if (std::holds_alternative<double>(init_value)) {
									member_store.value.value = std::get<double>(init_value);
								} else if (std::holds_alternative<StringHandle>(init_value)) {
									member_store.value.value = std::get<StringHandle>(init_value);
								} else {
									// For other types, skip this capture
									continue;
								}

								member_store.object = StringTable::getOrInternStringHandle(closure_var_name);
								member_store.member_name = member->getName();
								member_store.offset = static_cast<int>(member->offset);
								member_store.ref_qualifier = ((member->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
								member_store.struct_type_info = nullptr;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), lambda.lambda_token()));
							}

							// Init-captures do not consume an entry from captured_var_decls.
							continue;
						} else if (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference) {
							// By-reference: store the address of the variable
							// Get the original variable type from captured_var_decls
							const ASTNode& var_decl = lambda_info.captured_var_decls[capture_index];
							const DeclarationNode* decl = get_decl_from_symbol(var_decl);
							if (!decl) {
								capture_index++;
								continue;
							}
							const auto& orig_type = decl->type_node().as<TypeSpecifierNode>();

							TempVar addr_temp = var_counter.next();

							if (is_captured_from_enclosing) {
								// Variable is captured from enclosing lambda - need to get address from this->x
								auto enclosing_kind_it = current_lambda_context_.capture_kinds.find(var_name_str);
								bool enclosing_is_ref = (enclosing_kind_it != current_lambda_context_.capture_kinds.end() &&
								enclosing_kind_it->second == LambdaCaptureNode::CaptureKind::ByReference);

								if (enclosing_is_ref) {
									// Enclosing captured by reference - it already holds a pointer, just copy it
									MemberLoadOp member_load;
									member_load.result.value = addr_temp;
									member_load.result.type = orig_type.type();
									member_load.result.size_in_bits = 64;
									member_load.object = StringTable::getOrInternStringHandle("this");
									member_load.member_name = StringTable::getOrInternStringHandle(var_name);

									int enclosing_offset = -1;
									auto enclosing_type_it = gTypesByName.find(current_lambda_context_.closure_type);
									if (enclosing_type_it != gTypesByName.end()) {
										const TypeInfo* enclosing_type = enclosing_type_it->second;
										if (const StructTypeInfo* enclosing_struct = enclosing_type->getStructInfo()) {
											const StructMember* enclosing_member = enclosing_struct->findMember(var_name);
											if (enclosing_member) {
												enclosing_offset = static_cast<int>(enclosing_member->offset);
											}
										}
									}
									member_load.offset = enclosing_offset;
									member_load.struct_type_info = nullptr;
									member_load.ref_qualifier = CVReferenceQualifier::LValueReference;
									ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), lambda.lambda_token()));
								} else {
									// Enclosing captured by value - need to get address of this->x
									AddressOfOp addr_op;
									addr_op.result = addr_temp;
									addr_op.operand.type = orig_type.type();
									addr_op.operand.size_in_bits = static_cast<int>(orig_type.size_in_bits());
									addr_op.operand.pointer_depth = PointerDepth{};
									addr_op.operand.value = StringTable::getOrInternStringHandle(var_name);
									ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), lambda.lambda_token()));
								}
							} else {
								// Regular variable - generate AddressOf directly
								AddressOfOp addr_op;
								addr_op.result = addr_temp;
								addr_op.operand.type = orig_type.type();
								addr_op.operand.size_in_bits = static_cast<int>(orig_type.size_in_bits());
								addr_op.operand.pointer_depth = PointerDepth{};
								addr_op.operand.value = StringTable::getOrInternStringHandle(var_name);
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), lambda.lambda_token()));
							}

							// Store the address in the closure member
							MemberStoreOp member_store;
							member_store.value.type = member->type;
							member_store.value.size_in_bits = static_cast<int>(member->size * 8);
							member_store.value.value = addr_temp;
							member_store.object = StringTable::getOrInternStringHandle(closure_var_name);
							member_store.member_name = member->getName();
							member_store.offset = static_cast<int>(member->offset);
							member_store.ref_qualifier = ((member->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
							member_store.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), lambda.lambda_token()));
						} else {
							// By-value: copy the value
							MemberStoreOp member_store;
							member_store.value.type = member->type;
							member_store.value.size_in_bits = static_cast<int>(member->size * 8);

							if (is_captured_from_enclosing) {
								// Variable is captured from enclosing lambda - load it via member access first
								TempVar loaded_value = var_counter.next();
								MemberLoadOp member_load;
								member_load.result.value = loaded_value;
								member_load.result.type = member->type;
								member_load.result.size_in_bits = static_cast<int>(member->size * 8);
								member_load.object = StringTable::getOrInternStringHandle("this");
								member_load.member_name = StringTable::getOrInternStringHandle(var_name);

								int enclosing_offset = -1;
								auto enclosing_type_it = gTypesByName.find(current_lambda_context_.closure_type);
								if (enclosing_type_it != gTypesByName.end()) {
									const TypeInfo* enclosing_type = enclosing_type_it->second;
									if (const StructTypeInfo* enclosing_struct = enclosing_type->getStructInfo()) {
										const StructMember* enclosing_member = enclosing_struct->findMember(var_name_str);
										if (enclosing_member) {
											enclosing_offset = static_cast<int>(enclosing_member->offset);
										}
									}
								}
								member_load.offset = enclosing_offset;
								member_load.struct_type_info = nullptr;
								member_load.ref_qualifier = CVReferenceQualifier::None;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), lambda.lambda_token()));

								member_store.value.value = loaded_value;
							} else {
								// Regular variable - use directly
								member_store.value.value = StringTable::getOrInternStringHandle(var_name);
							}

							member_store.object = StringTable::getOrInternStringHandle(closure_var_name);
							member_store.member_name = member->getName();
							member_store.offset = static_cast<int>(member->offset);
							member_store.ref_qualifier = ((member->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
							member_store.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), lambda.lambda_token()));
						}

						capture_index++;
					}
				}
			}
		}

		// Return the closure variable representing the lambda
		// Format: {type, size, value, type_index}
		// - type: Type::Struct (the closure is a struct)
		// - size: size of the closure in bits
		// - value: closure_var_name (the allocated closure variable)
		// - type_index: the type index for the closure struct
		int closure_size_bits = static_cast<int>(closure_type->getStructInfo()->total_size * 8);
		TypeIndex closure_type_index = TypeIndex{closure_type->type_index_};
		return makeExprResult(
			Type::Struct,
			closure_size_bits,
			IrOperand{StringTable::getOrInternStringHandle(closure_var_name)},
			closure_type_index
		);
	}

	void AstToIr::generateLambdaFunctions(const LambdaInfo& lambda_info) {
		// Following Clang's approach, we generate:
		// 1. operator() - member function with lambda body
		// 2. __invoke - static function that can be used as function pointer (only for non-capturing lambdas)
		// 3. conversion operator - returns pointer to __invoke (only for non-capturing lambdas)

		// Generate operator() member function
		generateLambdaOperatorCallFunction(lambda_info);

		// Generate __invoke static function only for non-capturing lambdas
		// Capturing lambdas can't be converted to function pointers
		if (lambda_info.captures.empty()) {
			generateLambdaInvokeFunction(lambda_info);
		}
		
		// CRITICAL FIX: Add operator() to the closure struct's member_functions list
		// This allows member function calls to find the correct declaration for mangling
		// Without this, lambda calls generate incorrect mangled names
		if (lambda_info.closure_type_name.empty()) {
			return;  // No closure type, can't add member functions
		}
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(lambda_info.closure_type_name));
		if (type_it != gTypesByName.end()) {
			TypeInfo* closure_type = type_it->second;
			StructTypeInfo* struct_info = closure_type->getStructInfo();
			if (struct_info) {
				// Create a FunctionDeclarationNode for operator()
				// We need this so member function calls can generate the correct mangled name
				TypeSpecifierNode return_type_node(lambda_info.return_type, lambda_info.return_type_index, 
					lambda_info.return_size, lambda_info.lambda_token);
				ASTNode return_type_ast = ASTNode::emplace_node<TypeSpecifierNode>(return_type_node);
				
				Token operator_token = lambda_info.lambda_token;  // Use lambda token as placeholder
				DeclarationNode& decl_node = gChunkedAnyStorage.emplace_back<DeclarationNode>(return_type_ast, operator_token);
				
				FunctionDeclarationNode& func_decl = gChunkedAnyStorage.emplace_back<FunctionDeclarationNode>(decl_node);
				
				// C++20: Lambda operator() is implicitly constexpr if it satisfies constexpr requirements
				// Mark it as constexpr so the ConstExprEvaluator can evaluate lambda calls at compile time
				func_decl.set_is_constexpr(true);
				
				// Add parameters to the function declaration
				for (const auto& param_node : lambda_info.parameter_nodes) {
					func_decl.add_parameter_node(param_node);
				}
				
				ASTNode func_decl_ast(&func_decl);
				
				// Create StructMemberFunction and add to struct
				StructMemberFunction member_func(
					StringTable::getOrInternStringHandle("operator()"),
					func_decl_ast,
					AccessSpecifier::Public,
					false,  // is_constructor
					false,  // is_destructor
					OverloadableOperator::Call
				);
				member_func.cv_qualifier = CVQualifier::None;  // Mutable lambdas have non-const operator()
				member_func.is_virtual = false;
				member_func.is_pure_virtual = false;
				member_func.is_override = false;
				member_func.is_final = false;
				member_func.vtable_index = 0;
				
				struct_info->member_functions.push_back(std::move(member_func));
			}
		}
	}

	void AstToIr::generateLambdaOperatorCallFunction(const LambdaInfo& lambda_info) {
		// Generate function declaration for operator()
		FunctionDeclOp func_decl_op;
		func_decl_op.function_name = StringTable::getOrInternStringHandle("operator()"sv);  // Phase 4: Variant needs explicit type
		func_decl_op.struct_name = StringTable::getOrInternStringHandle(lambda_info.closure_type_name);  // Phase 4: Variant needs explicit type
		func_decl_op.return_type = lambda_info.return_type;
		func_decl_op.return_size_in_bits = lambda_info.return_size;
		func_decl_op.return_pointer_depth = PointerDepth{};  // pointer depth
		func_decl_op.linkage = Linkage::None;  // C++ linkage
		func_decl_op.is_variadic = false;
		
		// Detect if lambda returns struct by value (needs hidden return parameter for RVO/NRVO)
		// Only non-pointer, non-reference struct returns need this
		bool returns_struct_by_value = returnsStructByValue(lambda_info.return_type, 0, lambda_info.returns_reference);
		bool needs_hidden_return_param = needsHiddenReturnParam(lambda_info.return_type, 0, lambda_info.returns_reference, lambda_info.return_size, context_->isLLP64());
		func_decl_op.has_hidden_return_param = needs_hidden_return_param;
		
		// Track hidden return parameter flag for current function context
		current_function_has_hidden_return_param_ = needs_hidden_return_param;
		
		if (returns_struct_by_value) {
			if (needs_hidden_return_param) {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Lambda operator() {} returns struct by value (size={} bits) - will use hidden return parameter (RVO/NRVO)",
					lambda_info.closure_type_name, lambda_info.return_size);
			} else {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Lambda operator() {} returns small struct by value (size={} bits) - will return in RAX",
					lambda_info.closure_type_name, lambda_info.return_size);
			}
		}

		// Build TypeSpecifierNode for return type (with proper type_index if struct)
		TypeSpecifierNode return_type_node(lambda_info.return_type, lambda_info.return_type_index, lambda_info.return_size, lambda_info.lambda_token);
		
		// Build TypeSpecifierNodes for parameters using parameter_nodes to preserve type_index
		std::vector<TypeSpecifierNode> param_types;
		size_t param_idx = 0;
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						// Use the deduced type from call site (already has reference flags)
						param_types.push_back(*deduced);
					} else {
						// No deduced type available, fallback to int
						TypeSpecifierNode int_type(Type::Int, TypeIndex{}, 32, lambda_info.lambda_token);
						param_types.push_back(int_type);
					}
				} else {
					// Use the parameter type as-is, preserving all reference flags
					// This ensures mangled names are consistent between call sites and definitions
					param_types.push_back(param_type);
				}
			}
			param_idx++;
		}
		
		// Generate mangled name using the same function as regular member functions
		std::string_view mangled = generateMangledNameForCall(
			"operator()",
			return_type_node,
			param_types,
			false,  // not variadic
			lambda_info.closure_type_name
		);
		func_decl_op.mangled_name = StringTable::getOrInternStringHandle(mangled);

		// Add parameters - use parameter_nodes to get complete type information
		param_idx = 0;
		size_t lambda_unnamed_param_counter = 0;
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				FunctionParam func_param;
				
				// Handle empty parameter names
				std::string_view param_name = param_decl.identifier_token().value();
				if (param_name.empty()) {
					func_param.name = StringTable::getOrInternStringHandle(
						StringBuilder().append("__param_").append(lambda_unnamed_param_counter++).commit());
				} else {
					func_param.name = StringTable::getOrInternStringHandle(param_name);
				}
				
				func_param.pointer_depth = PointerDepth{static_cast<int>(param_type.pointer_depth())};
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						func_param.type = deduced->type();
						func_param.size_in_bits = deduced->size_in_bits();
						// Use reference flags from the deduced type (set at call site)
						func_param.ref_qualifier = ((deduced->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((deduced->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
					} else {
						// No deduced type available, fallback to int
						func_param.type = Type::Int;
						func_param.size_in_bits = 32;
						func_param.ref_qualifier = ((param_type.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((param_type.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
					}
				} else {
					func_param.type = param_type.type();
					func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
					func_param.ref_qualifier = ((param_type.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((param_type.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
				}
				func_param.cv_qualifier = param_type.cv_qualifier();
				func_decl_op.parameters.push_back(func_param);
			}
			param_idx++;
		}
		
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), lambda_info.lambda_token));
		symbol_table.enter_scope(ScopeType::Function);

		// Reset the temporary variable counter for each new function
		// TempVar is 1-based (TempVar() starts at 1). For member functions (operator()),
		// TempVar(1) is reserved for 'this', so we start at TempVar(2).
		var_counter = TempVar(2);

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function return type and size for type checking in return statements
		// This is critical for lambdas returning other lambdas or structs
		current_function_return_type_ = lambda_info.return_type;
		current_function_return_size_ = lambda_info.return_size;
		current_function_returns_reference_ = lambda_info.returns_reference;

		// Set lambda context for captured variable access
		pushLambdaContext(lambda_info);

		// Add lambda parameters to symbol table as function parameters (operator() context)
		// This ensures they're recognized as local parameters, not external symbols
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				symbol_table.insert(param_decl.identifier_token().value(), param_node);
			}
		}

		// Add captured variables to symbol table
		// These will be accessed through member access (this->x)
		addCapturedVariablesToSymbolTable(lambda_info.captures, lambda_info.captured_var_decls);

		// Generate the lambda body
		bool has_return_statement = false;
		if (lambda_info.lambda_body.is<BlockNode>()) {
			const auto& body = lambda_info.lambda_body.as<BlockNode>();
			body.get_statements().visit([&](const ASTNode& stmt) {
				visit(stmt);
				if (stmt.is<ReturnStatementNode>()) {
					has_return_statement = true;
				}
			});
		}

		// Add implicit return for void lambdas (matching regular function behavior)
		if (!has_return_statement && lambda_info.return_type == Type::Void) {
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), lambda_info.lambda_token));
		}

		// Restore outer lambda context (if any)
		popLambdaContext();

		symbol_table.exit_scope();
		
		// Note: Nested lambdas collected during body generation will be processed
		// by the main generateCollectedLambdas() loop - no recursive call needed here
	}

	void AstToIr::generateLambdaInvokeFunction(const LambdaInfo& lambda_info) {
		// Generate function declaration for __invoke
		FunctionDeclOp func_decl_op;
		func_decl_op.function_name = StringTable::getOrInternStringHandle(lambda_info.invoke_name);  // Variant needs explicit type
		func_decl_op.struct_name = StringHandle();  // no struct name (static function)
		func_decl_op.return_type = lambda_info.return_type;
		func_decl_op.return_size_in_bits = lambda_info.return_size;
		func_decl_op.return_pointer_depth = PointerDepth{};  // pointer depth
		func_decl_op.linkage = Linkage::None;  // C++ linkage
		func_decl_op.is_variadic = false;
		
		// Detect if lambda returns struct by value (needs hidden return parameter for RVO/NRVO)
		// Detect if lambda returns struct by value (needs hidden return parameter for RVO/NRVO)
		bool needs_hidden_return_param = needsHiddenReturnParam(lambda_info.return_type, 0, lambda_info.returns_reference, lambda_info.return_size, context_->isLLP64());
		func_decl_op.has_hidden_return_param = needs_hidden_return_param;
		
		// Track hidden return parameter flag for current function context
		current_function_has_hidden_return_param_ = needs_hidden_return_param;

		// Build TypeSpecifierNode for return type (with proper type_index if struct)
		TypeSpecifierNode return_type_node(lambda_info.return_type, lambda_info.return_type_index, lambda_info.return_size, lambda_info.lambda_token);
		
		// Build TypeSpecifierNodes for parameters using parameter_nodes to preserve type_index
		std::vector<TypeSpecifierNode> param_types;
		size_t param_idx = 0;
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						// Use the deduced type from call site (already has reference flags)
						param_types.push_back(*deduced);
					} else {
						TypeSpecifierNode int_type(Type::Int, TypeIndex{}, 32, lambda_info.lambda_token);
						param_types.push_back(int_type);
					}
				} else {
					// Use the parameter type as-is, preserving all reference flags
					param_types.push_back(param_type);
				}
			}
			param_idx++;
		}
		
		// Generate mangled name for the __invoke function (free function, not member)
		std::string_view mangled = generateMangledNameForCall(
			lambda_info.invoke_name,
			return_type_node,
			param_types,
			false,  // not variadic
			""  // not a member function
		);
		func_decl_op.mangled_name = StringTable::getOrInternStringHandle(mangled);

		// Add parameters - use parameter_nodes to get complete type information
		param_idx = 0;
		size_t invoke_unnamed_param_counter = 0;
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				FunctionParam func_param;
				
				// Handle empty parameter names
				std::string_view param_name = param_decl.identifier_token().value();
				if (param_name.empty()) {
					func_param.name = StringTable::getOrInternStringHandle(
						StringBuilder().append("__param_").append(invoke_unnamed_param_counter++).commit());
				} else {
					func_param.name = StringTable::getOrInternStringHandle(param_name);
				}
				
				func_param.pointer_depth = PointerDepth{static_cast<int>(param_type.pointer_depth())};
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						func_param.type = deduced->type();
						func_param.size_in_bits = deduced->size_in_bits();
						// Use reference flags from the deduced type (set at call site)
						func_param.ref_qualifier = ((deduced->is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((deduced->is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
					} else {
						func_param.type = Type::Int;
						func_param.size_in_bits = 32;
						func_param.ref_qualifier = ((param_type.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((param_type.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
					}
				} else {
					func_param.type = param_type.type();
					func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
					func_param.ref_qualifier = ((param_type.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((param_type.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
				}
				func_param.cv_qualifier = param_type.cv_qualifier();
				func_decl_op.parameters.push_back(func_param);
			}
			param_idx++;
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), lambda_info.lambda_token));
		symbol_table.enter_scope(ScopeType::Function);

		// Reset the temporary variable counter for each new function
		// TempVar is 1-based. For static functions (like __invoke), no 'this' pointer,
		// so TempVar() starts at 1 which is the first available slot.
		var_counter = TempVar();

		// Set current function return type and size for type checking in return statements
		// This is critical for lambdas returning other lambdas or structs
		current_function_return_type_ = lambda_info.return_type;
		current_function_return_size_ = lambda_info.return_size;
		current_function_returns_reference_ = lambda_info.returns_reference;

		// Add lambda parameters to symbol table as function parameters (__invoke context)
		// This ensures they're recognized as local parameters, not external symbols
		for (const auto& param_node : lambda_info.parameter_nodes) {
			if (param_node.is<DeclarationNode>()) {
				const auto& param_decl = param_node.as<DeclarationNode>();
				symbol_table.insert(param_decl.identifier_token().value(), param_node);
			}
		}

		// Add captured variables to symbol table
		addCapturedVariablesToSymbolTable(lambda_info.captures, lambda_info.captured_var_decls);

		// Push lambda context so that recursive calls via auto&& parameters work correctly
		// This allows the auto-typed callable handling in generateFunctionCallIr to detect
		// that we're inside a lambda and generate the correct operator() call
		pushLambdaContext(lambda_info);

		// Generate the lambda body
		bool has_return_statement = false;
		if (lambda_info.lambda_body.is<BlockNode>()) {
			const auto& body = lambda_info.lambda_body.as<BlockNode>();
			body.get_statements().visit([&](const ASTNode& stmt) {
				visit(stmt);
				if (stmt.is<ReturnStatementNode>()) {
					has_return_statement = true;
				}
			});
		}

		// Add implicit return for void lambdas (matching regular function behavior)
		if (!has_return_statement && lambda_info.return_type == Type::Void) {
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), lambda_info.lambda_token));
		}

		// Restore outer lambda context
		popLambdaContext();

		symbol_table.exit_scope();
	}

	void AstToIr::addCapturedVariablesToSymbolTable(const std::vector<LambdaCaptureNode>& captures,
	const std::vector<ASTNode>& captured_var_decls) {
		// Add captured variables to the symbol table
		// We use the stored declarations from when the lambda was created
		size_t capture_index = 0;
		for (const auto& capture : captures) {
			if (capture.is_capture_all()) {
				// Capture-all ([=] or [&]) should have been expanded by the parser into explicit captures
				// If we see one here, it means the parser didn't expand it (shouldn't happen)
				continue;
			}
			
			// Skip [this] and [*this] captures - they don't have variable declarations
			if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
			capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
				continue;
			}

			// Skip init-captures: [x = expr] defines a new variable, doesn't capture existing one
			// These are handled separately by reading from the closure member
			if (capture.has_initializer()) {
				continue;
			}

			if (capture_index >= captured_var_decls.size()) {
				std::cerr << "Error: Mismatch between captures and captured variable declarations\n";
				break;
			}

			// Get the stored variable declaration
			const ASTNode& var_decl = captured_var_decls[capture_index];
			std::string_view var_name = capture.identifier_name();

			// Add the captured variable to the current scope
			// For by-value captures, we create a copy
			// For by-reference captures, we use the original
			symbol_table.insert(var_name, var_decl);

			capture_index++;
		}
	}




// Helper to generate FunctionAddress IR for a lambda's __invoke function
// Returns the TempVar holding the function pointer address
TempVar AstToIr::generateLambdaInvokeFunctionAddress(const LambdaExpressionNode& lambda) {
	std::string_view invoke_name = StringBuilder()
		.append(lambda.generate_lambda_name())
		.append("_invoke")
		.commit();
	
	// Compute the mangled name for the __invoke function
	// Per C++20 §7.5.5.1, a lambda with no return statements deduces void
	Type return_type = Type::Void;
	int return_size = 0;
	if (lambda.return_type().has_value()) {
		const auto& ret_type_node = lambda.return_type()->as<TypeSpecifierNode>();
		return_type = ret_type_node.type();
		return_size = ret_type_node.size_in_bits();
	}
	TypeSpecifierNode return_type_node(return_type, TypeIndex{}, return_size, lambda.lambda_token());
	
	// Build parameter types
	std::vector<TypeSpecifierNode> param_type_nodes;
	for (const auto& param : lambda.parameters()) {
		if (param.is<DeclarationNode>()) {
			const auto& param_decl = param.as<DeclarationNode>();
			const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
			param_type_nodes.push_back(param_type);
		}
	}
	
	// Generate mangled name
	std::string_view mangled = generateMangledNameForCall(
		invoke_name, return_type_node, param_type_nodes, false, "");
	
	// Generate FunctionAddress instruction to get the address
	TempVar func_addr_var = var_counter.next();
	FunctionAddressOp op;
	op.result.type = Type::FunctionPointer;
	op.result.size_in_bits = 64;
	op.result.value = func_addr_var;
	op.function_name = StringTable::getOrInternStringHandle(invoke_name);
	op.mangled_name = StringTable::getOrInternStringHandle(mangled);
	ir_.addInstruction(IrInstruction(IrOpcode::FunctionAddress, std::move(op), Token()));
	
	return func_addr_var;
}



// ========== Lambda Capture Helper Functions ==========

// Get the current lambda's closure StructTypeInfo, or nullptr if not in a lambda
const StructTypeInfo* AstToIr::getCurrentClosureStruct() const {
	if (!current_lambda_context_.isActive()) {
		return nullptr;
	}
	auto it = gTypesByName.find(current_lambda_context_.closure_type);
	if (it == gTypesByName.end() || !it->second->isStruct()) {
		return nullptr;
	}
	return it->second->getStructInfo();
}


// Check if we're in a lambda with [*this] capture
bool AstToIr::isInCopyThisLambda() const {
	if (!current_lambda_context_.isActive()) {
		return false;
	}
	if (current_lambda_context_.has_copy_this) {
		return true;
	}
	if (const StructTypeInfo* closure = getCurrentClosureStruct()) {
		return closure->findMember("__copy_this") != nullptr;
	}
	return false;
}



// Get the offset of a member in the current lambda closure struct
// Returns 0 if not found or not in a lambda context
int AstToIr::getClosureMemberOffset(std::string_view member_name) const {
	if (const StructTypeInfo* closure = getCurrentClosureStruct()) {
		if (const StructMember* member = closure->findMember(member_name)) {
			return static_cast<int>(member->offset);
		}
	}
	return 0;
}



// Emit IR to load __copy_this from current lambda closure into a TempVar.
// Returns the TempVar holding the copied object, or std::nullopt if not applicable.
// The Token parameter is used for source location in the IR instruction.
std::optional<TempVar> AstToIr::emitLoadCopyThis(const Token& token) {
	if (!isInCopyThisLambda()) {
		return std::nullopt;
	}
	const StructTypeInfo* closure_struct = getCurrentClosureStruct();
	if (!closure_struct) {
		return std::nullopt;
	}
	const StructMember* copy_this_member = closure_struct->findMember("__copy_this");
	if (!copy_this_member || current_lambda_context_.enclosing_struct_type_index == 0) {
		return std::nullopt;
	}

	TempVar copy_this_temp = var_counter.next();
	MemberLoadOp load_op;
	load_op.result.value = copy_this_temp;
	load_op.result.type = Type::Struct;
	load_op.result.size_in_bits = static_cast<int>(copy_this_member->size * 8);
	load_op.object = StringTable::getOrInternStringHandle("this");  // Lambda's this (the closure)
	load_op.member_name = StringTable::getOrInternStringHandle("__copy_this");
	load_op.offset = static_cast<int>(copy_this_member->offset);
	load_op.ref_qualifier = CVReferenceQualifier::None;
	load_op.struct_type_info = nullptr;
	ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), token));

	// Mark this temp var as an lvalue pointing to %this.__copy_this
	// This allows subsequent member accesses and stores to properly chain offsets
	LValueInfo lvalue_info(
		LValueInfo::Kind::Member,
		StringTable::getOrInternStringHandle("this"),
		static_cast<int>(copy_this_member->offset)
	);
	lvalue_info.member_name = StringTable::getOrInternStringHandle("__copy_this");
	lvalue_info.is_pointer_to_member = true;  // Treat closure 'this' as a pointer
	setTempVarMetadata(copy_this_temp, TempVarMetadata::makeLValue(lvalue_info));

	return copy_this_temp;
}


// Manage lambda context push/pop for nested lambdas
void AstToIr::pushLambdaContext(const LambdaInfo& lambda_info) {
	lambda_context_stack_.push_back(current_lambda_context_);
	current_lambda_context_ = {};
	current_lambda_context_.closure_type = StringTable::getOrInternStringHandle(lambda_info.closure_type_name);
	current_lambda_context_.enclosing_struct_type_index = lambda_info.enclosing_struct_type_index;
	current_lambda_context_.has_copy_this = false;
	current_lambda_context_.has_this_pointer = false;
	current_lambda_context_.is_mutable = lambda_info.is_mutable;

	size_t capture_index = 0;
	for (const auto& capture : lambda_info.captures) {
		if (capture.is_capture_all()) {
			continue;
		}
		StringHandle var_name = StringTable::getOrInternStringHandle(capture.identifier_name());
		current_lambda_context_.captures.insert(var_name);
		current_lambda_context_.capture_kinds[var_name] = capture.kind();
		if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
		capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
			current_lambda_context_.captures.insert(StringTable::getOrInternStringHandle("this"sv));
			current_lambda_context_.capture_kinds[StringTable::getOrInternStringHandle("this"sv)] = capture.kind();
			if (capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
				current_lambda_context_.has_copy_this = true;
			} else if (capture.kind() == LambdaCaptureNode::CaptureKind::This) {
				current_lambda_context_.has_this_pointer = true;
			}
		} else if (capture.has_initializer()) {
			// Init-capture: infer type from initializer expression or closure struct member
			// For init-capture by reference [&y = x], look up x's type
			const ASTNode& init_node = *capture.initializer();
			if (init_node.is<IdentifierNode>()) {
				// Simple identifier like [&y = x] - look up x's type
				const auto& init_id = init_node.as<IdentifierNode>();
				std::optional<ASTNode> init_symbol = symbol_table.lookup(init_id.name());
				if (init_symbol.has_value()) {
					const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
					if (init_decl) {
						current_lambda_context_.capture_types[var_name] = init_decl->type_node().as<TypeSpecifierNode>();
					}
				}
			} else if (init_node.is<ExpressionNode>()) {
				const auto& expr_node = init_node.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(expr_node)) {
					const auto& init_id = std::get<IdentifierNode>(expr_node);
					std::optional<ASTNode> init_symbol = symbol_table.lookup(init_id.name());
					if (init_symbol.has_value()) {
						const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
						if (init_decl) {
							current_lambda_context_.capture_types[var_name] = init_decl->type_node().as<TypeSpecifierNode>();
						}
					}
				}
			}
			// If type still not set, try to get it from closure struct member
			if (current_lambda_context_.capture_types.find(var_name) == current_lambda_context_.capture_types.end()) {
				auto type_it = gTypesByName.find(current_lambda_context_.closure_type);
				if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
					const StructTypeInfo* struct_info = type_it->second->getStructInfo();
					if (struct_info) {
						const StructMember* member = struct_info->findMember(std::string_view(StringTable::getStringView(var_name)));
						if (member) {
							// Create a TypeSpecifierNode from the member type
							TypeSpecifierNode member_type(member->type, TypeQualifier::None, static_cast<int>(member->size * 8));
							if (member->type == Type::Struct) {
								// Need to set type_index for struct types
								member_type = TypeSpecifierNode(member->type, member->type_index, static_cast<int>(member->size * 8), Token());
							}
							current_lambda_context_.capture_types[var_name] = member_type;
						}
					}
				}
			}
		} else {
			if (capture_index < lambda_info.captured_var_decls.size()) {
				const ASTNode& var_decl = lambda_info.captured_var_decls[capture_index];
				if (const DeclarationNode* decl = get_decl_from_symbol(var_decl)) {
					current_lambda_context_.capture_types[var_name] = decl->type_node().as<TypeSpecifierNode>();
				}
			}
			capture_index++;
		}
	}
	if (!current_lambda_context_.has_copy_this) {
		if (const StructTypeInfo* closure = getCurrentClosureStruct()) {
			if (closure->findMember("__copy_this")) {
				current_lambda_context_.has_copy_this = true;
			}
		}
	}
}



void AstToIr::popLambdaContext() {
	if (lambda_context_stack_.empty()) {
		current_lambda_context_ = {};
		return;
	}
	current_lambda_context_ = lambda_context_stack_.back();
	lambda_context_stack_.pop_back();
}



// Emit IR to load __this pointer from current lambda closure into a TempVar.
// Returns the TempVar holding the this pointer, or std::nullopt if not applicable.
std::optional<TempVar> AstToIr::emitLoadThisPointer(const Token& token) {
	if (!isInThisPointerLambda()) {
		return std::nullopt;
	}

	int this_member_offset = getClosureMemberOffset("__this");

	TempVar this_ptr = var_counter.next();
	MemberLoadOp load_op;
	load_op.result.value = this_ptr;
	load_op.result.type = Type::Void;
	load_op.result.size_in_bits = 64;
	load_op.object = StringTable::getOrInternStringHandle("this");  // Lambda's this (the closure)
	load_op.member_name = StringTable::getOrInternStringHandle("__this");
	load_op.offset = this_member_offset;
	load_op.ref_qualifier = CVReferenceQualifier::None;
	load_op.struct_type_info = nullptr;
	ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), token));

	return this_ptr;
}



// ========== Auto Type Deduction Helpers ==========

// Try to extract a LambdaExpressionNode from an initializer ASTNode.
// Returns nullptr if the node is not a lambda expression.
const LambdaExpressionNode* AstToIr::extractLambdaFromInitializer(const ASTNode& init) {
	if (init.is<LambdaExpressionNode>()) {
		return &init.as<LambdaExpressionNode>();
	}
	if (init.is<ExpressionNode>()) {
		const ExpressionNode& expr = init.as<ExpressionNode>();
		if (std::holds_alternative<LambdaExpressionNode>(expr)) {
			return &std::get<LambdaExpressionNode>(expr);
		}
	}
	return nullptr;
}



// Deduce the actual closure type from an auto-typed lambda variable.
// Given a symbol from the symbol table, if it's an auto-typed variable
// initialized with a lambda, returns the TypeSpecifierNode for the closure struct.
// Returns std::nullopt if type cannot be deduced.
std::optional<TypeSpecifierNode> AstToIr::deduceLambdaClosureType(const ASTNode& symbol,
const Token& fallback_token) const {
	if (!symbol.is<VariableDeclarationNode>()) {
		return std::nullopt;
	}
	const VariableDeclarationNode& var_decl = symbol.as<VariableDeclarationNode>();
	const std::optional<ASTNode>& init_opt = var_decl.initializer();
	if (!init_opt.has_value()) {
		return std::nullopt;
	}

	const LambdaExpressionNode* lambda_ptr = extractLambdaFromInitializer(*init_opt);
	if (!lambda_ptr) {
		return std::nullopt;
	}

	StringHandle closure_type_name = lambda_ptr->generate_lambda_name();
	auto type_it = gTypesByName.find(closure_type_name);
	if (type_it == gTypesByName.end()) {
		return std::nullopt;
	}

	const TypeInfo* closure_type = type_it->second;
	int closure_size = closure_type->getStructInfo()
		? closure_type->getStructInfo()->total_size * 8
		: 64;
	return TypeSpecifierNode(
		Type::Struct,
		closure_type->type_index_,
		closure_size,
		fallback_token
	);
}
