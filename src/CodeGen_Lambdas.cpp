	std::vector<IrOperand> generateLambdaExpressionIr(const LambdaExpressionNode& lambda, std::string_view target_var_name = "") {
		// Collect lambda information for deferred generation
		// Following Clang's approach: generate closure class, operator(), __invoke, and conversion operator
		// If target_var_name is provided, use it as the closure variable name (for variable declarations)
		// Otherwise, create a temporary __closure_N variable

		LambdaInfo info;
		info.lambda_id = lambda.lambda_id();
		
		// Use StringBuilder to create persistent string_views for lambda names
		// This ensures the names remain valid after LambdaInfo is moved
		info.closure_type_name = StringBuilder()
			.append("__lambda_")
			.append(static_cast<int64_t>(lambda.lambda_id()))
			.commit();

		// Build operator_call_name: closure_type_name + "_operator_call"
		info.operator_call_name = StringBuilder()
			.append(info.closure_type_name)
			.append("_operator_call")
			.commit();

		// Build invoke_name: closure_type_name + "_invoke"
		info.invoke_name = StringBuilder()
			.append(info.closure_type_name)
			.append("_invoke")
			.commit();

		// Build conversion_op_name: closure_type_name + "_conversion"
		info.conversion_op_name = StringBuilder()
			.append(info.closure_type_name)
			.append("_conversion")
			.commit();

		info.lambda_token = lambda.lambda_token();
		
		// Store enclosing struct info for [this] capture support
		info.enclosing_struct_name = current_struct_name_.isValid() ? StringTable::getStringView(current_struct_name_) : std::string_view();
		if (current_struct_name_.isValid()) {
			auto type_it = gTypesByName.find(current_struct_name_);
			if (type_it != gTypesByName.end()) {
				info.enclosing_struct_type_index = type_it->second->type_index_;
			}
		}

		// Copy lambda body and captures (we need them later)
		info.lambda_body = lambda.body();
		info.captures = lambda.captures();
		info.is_mutable = lambda.is_mutable();

		// Collect captured variable declarations from current scope
		for (const auto& capture : lambda.captures()) {
			if (capture.is_capture_all()) {
				// Capture-all ([=] or [&]) should have been expanded by the parser into explicit captures
				// If we see one here, it means the parser didn't expand it (shouldn't happen)
				continue;
			}
			
			// Skip [this] and [*this] captures as they don't have an identifier to look up
			if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
			    capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
				continue;
			}

			// Skip init-captures: [x = expr] defines a new variable, doesn't capture existing one
			if (capture.has_initializer()) {
				continue;
			}

			// Look up the captured variable in the current scope
			std::string_view var_name = capture.identifier_name();
			std::optional<ASTNode> var_symbol = symbol_table.lookup(var_name);

			if (var_symbol.has_value()) {
				// Store the variable declaration for later use
				info.captured_var_decls.push_back(*var_symbol);
			} else {
				std::cerr << "Warning: Captured variable '" << var_name << "' not found in scope during lambda generation\n";
			}
		}

		// Determine return type
		info.return_type = Type::Int;  // Default to int
		info.return_size = 32;
		info.return_type_index = 0;
		info.returns_reference = false;
		if (lambda.return_type().has_value()) {
			const auto& ret_type_node = lambda.return_type()->as<TypeSpecifierNode>();
			info.return_type = ret_type_node.type();
			info.return_size = ret_type_node.size_in_bits();
			info.return_type_index = ret_type_node.type_index();
			info.returns_reference = ret_type_node.is_reference();
			// If returning a reference, the size should be 64 bits (pointer size)
			if (info.returns_reference) {
				info.return_size = 64;
			}
		}

		// Collect parameters and detect generic lambda (auto parameters)
		size_t param_index = 0;
		for (const auto& param : lambda.parameters()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				
				// Detect auto parameters (generic lambda)
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
				// Also store the actual parameter node for symbol table
				info.parameter_nodes.push_back(param);
			}
			param_index++;
		}

		// Look up the closure type (registered during parsing) BEFORE moving info
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(info.closure_type_name));
		if (type_it == gTypesByName.end()) {
			// Error: closure type not found
			TempVar dummy = var_counter.next();
			return {Type::Int, 32, dummy};
		}

		const TypeInfo* closure_type = type_it->second;

		// Store lambda info for later generation (after we've used closure_type_name)
		collected_lambdas_.push_back(std::move(info));
		const LambdaInfo& lambda_info = collected_lambdas_.back();

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
			lambda_decl_op.is_reference = false;
			lambda_decl_op.is_rvalue_reference = false;
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
			lambda_decl_op.is_reference = false;
			lambda_decl_op.is_rvalue_reference = false;
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
							store_this.is_reference = false;
							store_this.is_rvalue_reference = false;
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
									load_op.is_reference = enclosing_member.is_reference;
									load_op.is_rvalue_reference = enclosing_member.is_rvalue_reference;
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
									store_copy_this.is_reference = enclosing_member.is_reference;
									store_copy_this.is_rvalue_reference = enclosing_member.is_rvalue_reference;
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
							auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());

							if (init_operands.size() < 3) {
								continue;
							}

							// visitExpressionNode returns {type, size, value, ...}
							// The actual value is at index 2
							IrOperand init_value = init_operands[2];

							// For init-capture by reference [&y = x], we need to store the address of x
							if (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference) {
								// Get the type info from the init operands
								Type init_type = Type::Int;
								int init_size = 32;
								if (init_operands.size() > 0 && std::holds_alternative<Type>(init_operands[0])) {
									init_type = std::get<Type>(init_operands[0]);
								}
								if (init_operands.size() > 1) {
									if (std::holds_alternative<int>(init_operands[1])) {
										init_size = std::get<int>(init_operands[1]);
									} else if (std::holds_alternative<unsigned long long>(init_operands[1])) {
										init_size = static_cast<int>(std::get<unsigned long long>(init_operands[1]));
									}
								}

								// Generate AddressOf for the initializer
								TempVar addr_temp = var_counter.next();
								AddressOfOp addr_op;
								addr_op.result = addr_temp;
								addr_op.operand.type = init_type;
								addr_op.operand.size_in_bits = init_size;
								addr_op.operand.pointer_depth = 0;

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
								member_store.is_reference = true;
								member_store.is_rvalue_reference = false;
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
								member_store.is_reference = member->is_reference;
								member_store.is_rvalue_reference = member->is_rvalue_reference;
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
									member_load.is_reference = true;
									member_load.is_rvalue_reference = false;
									ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), lambda.lambda_token()));
								} else {
									// Enclosing captured by value - need to get address of this->x
									AddressOfOp addr_op;
									addr_op.result = addr_temp;
									addr_op.operand.type = orig_type.type();
									addr_op.operand.size_in_bits = static_cast<int>(orig_type.size_in_bits());
									addr_op.operand.pointer_depth = 0;
									addr_op.operand.value = StringTable::getOrInternStringHandle(var_name);
									ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), lambda.lambda_token()));
								}
							} else {
								// Regular variable - generate AddressOf directly
								AddressOfOp addr_op;
								addr_op.result = addr_temp;
								addr_op.operand.type = orig_type.type();
								addr_op.operand.size_in_bits = static_cast<int>(orig_type.size_in_bits());
								addr_op.operand.pointer_depth = 0;
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
							member_store.is_reference = member->is_reference;
							member_store.is_rvalue_reference = member->is_rvalue_reference;
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
								member_load.is_reference = false;
								member_load.is_rvalue_reference = false;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), lambda.lambda_token()));

								member_store.value.value = loaded_value;
							} else {
								// Regular variable - use directly
								member_store.value.value = StringTable::getOrInternStringHandle(var_name);
							}

							member_store.object = StringTable::getOrInternStringHandle(closure_var_name);
							member_store.member_name = member->getName();
							member_store.offset = static_cast<int>(member->offset);
							member_store.is_reference = member->is_reference;
							member_store.is_rvalue_reference = member->is_rvalue_reference;
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
		return {Type::Struct, closure_size_bits, StringTable::getOrInternStringHandle(closure_var_name), static_cast<unsigned long long>(closure_type->type_index_)};
	}



	// Generate all functions for a lambda (following Clang's approach)
	void generateLambdaFunctions(const LambdaInfo& lambda_info) {
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
			TypeInfo* closure_type = const_cast<TypeInfo*>(type_it->second);
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
					false,  // is_operator_overload
					""     // operator_symbol
				);
				member_func.is_const = false;  // Mutable lambdas have non-const operator()
				member_func.is_virtual = false;
				member_func.is_pure_virtual = false;
				member_func.is_override = false;
				member_func.is_final = false;
				member_func.vtable_index = 0;
				
				struct_info->member_functions.push_back(std::move(member_func));
			}
		}
	}

	// Generate the operator() member function for a lambda
	void generateLambdaOperatorCallFunction(const LambdaInfo& lambda_info) {
		// Generate function declaration for operator()
		FunctionDeclOp func_decl_op;
		func_decl_op.function_name = StringTable::getOrInternStringHandle("operator()"sv);  // Phase 4: Variant needs explicit type
		func_decl_op.struct_name = StringTable::getOrInternStringHandle(lambda_info.closure_type_name);  // Phase 4: Variant needs explicit type
		func_decl_op.return_type = lambda_info.return_type;
		func_decl_op.return_size_in_bits = lambda_info.return_size;
		func_decl_op.return_pointer_depth = 0;  // pointer depth
		func_decl_op.linkage = Linkage::None;  // C++ linkage
		func_decl_op.is_variadic = false;
		
		// Detect if lambda returns struct by value (needs hidden return parameter for RVO/NRVO)
		// Only non-pointer, non-reference struct returns need this
		// Windows x64 ABI: structs of 1, 2, 4, or 8 bytes return in RAX, larger structs use hidden parameter
		// SystemV AMD64 ABI: structs up to 16 bytes can return in RAX/RDX, larger structs use hidden parameter
		bool returns_struct_by_value = (lambda_info.return_type == Type::Struct && !lambda_info.returns_reference);
		int struct_return_threshold = context_->isLLP64() ? 64 : 128;  // Windows: 64 bits (8 bytes), Linux: 128 bits (16 bytes)
		bool needs_hidden_return_param = returns_struct_by_value && (lambda_info.return_size > struct_return_threshold);
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
						TypeSpecifierNode int_type(Type::Int, 0, 32, lambda_info.lambda_token);
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
				
				func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						func_param.type = deduced->type();
						func_param.size_in_bits = deduced->size_in_bits();
						// Use reference flags from the deduced type (set at call site)
						func_param.is_reference = deduced->is_reference();
						func_param.is_rvalue_reference = deduced->is_rvalue_reference();
					} else {
						// No deduced type available, fallback to int
						func_param.type = Type::Int;
						func_param.size_in_bits = 32;
						func_param.is_reference = param_type.is_reference();
						func_param.is_rvalue_reference = param_type.is_rvalue_reference();
					}
				} else {
					func_param.type = param_type.type();
					func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
					func_param.is_reference = param_type.is_reference();
					func_param.is_rvalue_reference = param_type.is_rvalue_reference();
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

	// Generate the __invoke static function for a lambda
	void generateLambdaInvokeFunction(const LambdaInfo& lambda_info) {
		// Generate function declaration for __invoke
		FunctionDeclOp func_decl_op;
		func_decl_op.function_name = StringTable::getOrInternStringHandle(lambda_info.invoke_name);  // Variant needs explicit type
		func_decl_op.struct_name = StringHandle();  // no struct name (static function)
		func_decl_op.return_type = lambda_info.return_type;
		func_decl_op.return_size_in_bits = lambda_info.return_size;
		func_decl_op.return_pointer_depth = 0;  // pointer depth
		func_decl_op.linkage = Linkage::None;  // C++ linkage
		func_decl_op.is_variadic = false;
		
		// Detect if lambda returns struct by value (needs hidden return parameter for RVO/NRVO)
		// Windows x64 ABI: structs of 1, 2, 4, or 8 bytes return in RAX, larger structs use hidden parameter
		// SystemV AMD64 ABI: structs up to 16 bytes can return in RAX/RDX, larger structs use hidden parameter
		bool returns_struct_by_value = (lambda_info.return_type == Type::Struct && !lambda_info.returns_reference);
		int struct_return_threshold = context_->isLLP64() ? 64 : 128;  // Windows: 64 bits (8 bytes), Linux: 128 bits (16 bytes)
		bool needs_hidden_return_param = returns_struct_by_value && (lambda_info.return_size > struct_return_threshold);
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
						TypeSpecifierNode int_type(Type::Int, 0, 32, lambda_info.lambda_token);
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
				
				func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
				
				// For 'auto' parameters (generic lambdas), use deduced type from call site
				if (param_type.type() == Type::Auto) {
					auto deduced = lambda_info.getDeducedType(param_idx);
					if (deduced.has_value()) {
						func_param.type = deduced->type();
						func_param.size_in_bits = deduced->size_in_bits();
						// Use reference flags from the deduced type (set at call site)
						func_param.is_reference = deduced->is_reference();
						func_param.is_rvalue_reference = deduced->is_rvalue_reference();
					} else {
						func_param.type = Type::Int;
						func_param.size_in_bits = 32;
						func_param.is_reference = param_type.is_reference();
						func_param.is_rvalue_reference = param_type.is_rvalue_reference();
					}
				} else {
					func_param.type = param_type.type();
					func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
					func_param.is_reference = param_type.is_reference();
					func_param.is_rvalue_reference = param_type.is_rvalue_reference();
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

	// Helper function to add captured variables to symbol table
	void addCapturedVariablesToSymbolTable(const std::vector<LambdaCaptureNode>& captures,
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


	Ir ir_;
	TempVar var_counter{ 0 };
	SymbolTable symbol_table;
	SymbolTable* global_symbol_table_;  // Reference to the global symbol table for function overload lookup
	CompileContext* context_;  // Reference to compile context for flags
	Parser* parser_;  // Reference to parser for template instantiation

	// Current function name (for mangling static local variables)
	StringHandle current_function_name_;
	StringHandle current_struct_name_;  // For tracking which struct we're currently visiting member functions for
	Type current_function_return_type_;  // Current function's return type
	int current_function_return_size_;   // Current function's return size in bits
	TypeIndex current_function_return_type_index_ = 0;  // Type index for struct/class return types
	bool current_function_has_hidden_return_param_ = false;  // True if function uses hidden return parameter
	bool current_function_returns_reference_ = false;  // True if function returns a reference type (T& or T&&)
	bool in_return_statement_with_rvo_ = false;  // True when evaluating return expr that should use RVO
	
	// Current namespace path stack (for proper name mangling of namespace-scoped functions)
	std::vector<std::string> current_namespace_stack_;

	// Static local variable information
	struct StaticLocalInfo {
		StringHandle mangled_name;  // Phase 4: Using StringHandle
		Type type;
		int size_in_bits;
	};

	// Map from local static variable name to info
	// Key: local variable name, Value: static local info
	// Phase 4: Using StringHandle for keys
	std::unordered_map<StringHandle, StaticLocalInfo> static_local_names_;

	// Map from simple global variable name to mangled name
	// Key: simple identifier (e.g., "value"), Value: mangled name (e.g., "_ZN12_GLOBAL__N_15valueE")
	// This is needed because anonymous namespace variables need special mangling
	// Phase 4: Using StringHandle
	std::unordered_map<StringHandle, StringHandle> global_variable_names_;

	// Map from function name to deduced auto return type
	// Key: function name (mangled), Value: deduced TypeSpecifierNode
	std::unordered_map<std::string, TypeSpecifierNode> deduced_auto_return_types_;
	
	struct CachedParamInfo {
		bool is_reference = false;
		bool is_rvalue_reference = false;
		bool is_parameter_pack = false;
	};
	// Cache parameter reference info by mangled function name to aid call-site lowering
	std::unordered_map<StringHandle, std::vector<CachedParamInfo>> function_param_cache_;

	// Collected lambdas for deferred generation
	std::vector<LambdaInfo> collected_lambdas_;
	std::unordered_set<int> generated_lambda_ids_;  // Track which lambdas have been generated to prevent duplicates
	
	// Track generated functions to prevent duplicate codegen
	// Key: mangled function name - prevents generating the same function body multiple times
	// Phase 4: Using StringHandle
	std::unordered_set<StringHandle> generated_function_names_;
	
	// Generic lambda instantiation tracking
	// Key: lambda_id concatenated with deduced type signature (e.g., "0_int_double")
	// Value: The deduced parameter types for that instantiation
	struct GenericLambdaInstantiation {
		size_t lambda_id;
		std::vector<std::pair<size_t, TypeSpecifierNode>> deduced_types;  // param_index -> deduced type
		StringHandle instantiation_key;  // Unique key for this instantiation
	};
	std::vector<GenericLambdaInstantiation> pending_generic_lambda_instantiations_;
	std::unordered_set<std::string> generated_generic_lambda_instantiations_;  // Track already generated ones
	
	// Structure to hold info for local struct member functions
	struct LocalStructMemberInfo {
		StringHandle struct_name;
		StringHandle enclosing_function_name;
		ASTNode member_function_node;
	};
	
	// Collected local struct member functions for deferred generation
	std::vector<LocalStructMemberInfo> collected_local_struct_members_;
	
	// Structure to hold template instantiation info for deferred generation
	struct TemplateInstantiationInfo {
		StringHandle qualified_template_name;  // e.g., "Container::insert"
		StringHandle mangled_name;  // e.g., "insert_int"
		StringHandle struct_name;   // e.g., "Container"
		std::vector<Type> template_args;  // Concrete types
		SaveHandle body_position;  // Handle to saved position where the template body starts
		std::vector<std::string_view> template_param_names;  // e.g., ["U"]
		const TemplateFunctionDeclarationNode* template_node_ptr;  // Pointer to the template
	};
	
	// Collected template instantiations for deferred generation
	std::vector<TemplateInstantiationInfo> collected_template_instantiations_;

	// Track emitted static members to avoid duplicates
	std::unordered_set<StringHandle> emitted_static_members_;
	
	// Track processed TypeInfo pointers to avoid processing the same struct twice
	// (same struct can be registered under multiple keys in gTypesByName)
	std::unordered_set<const TypeInfo*> processed_type_infos_;

	// Current lambda context (for tracking captured variables)
	// When generating lambda body, this contains the closure type name and capture metadata
	struct LambdaContext {
		StringHandle closure_type;
		std::unordered_set<StringHandle> captures;
		std::unordered_map<StringHandle, LambdaCaptureNode::CaptureKind> capture_kinds;
		std::unordered_map<StringHandle, TypeSpecifierNode> capture_types;
		TypeIndex enclosing_struct_type_index = 0;  // For [this] capture type resolution
		bool has_copy_this = false;
		bool has_this_pointer = false;
		bool is_mutable = false;  // Whether the lambda is mutable (allows modifying captures)
		bool isActive() const { return closure_type.isValid(); }
	};
	LambdaContext current_lambda_context_;
	std::vector<LambdaContext> lambda_context_stack_;

	// Generate just the function declaration for a template instantiation (without body)
	// This is called immediately when a template call is detected, so the IR converter
	// knows the full function signature before the call is converted to object code
	void generateTemplateFunctionDecl(const TemplateInstantiationInfo& inst_info) {
		const FunctionDeclarationNode& template_func_decl = inst_info.template_node_ptr->function_decl_node();
		const DeclarationNode& template_decl = template_func_decl.decl_node();

		// Create mangled name token
		Token mangled_token(
			Token::Type::Identifier,
			StringTable::getStringView(inst_info.mangled_name),
			template_decl.identifier_token().line(),
			template_decl.identifier_token().column(),
			template_decl.identifier_token().file_index()
		);

		StringHandle full_func_name = inst_info.mangled_name;
		StringHandle struct_name = inst_info.struct_name;

		// Generate function declaration IR using typed payload
		FunctionDeclOp func_decl_op;
		
		// Add return type
		const TypeSpecifierNode& return_type = template_decl.type_node().as<TypeSpecifierNode>();
		func_decl_op.return_type = return_type.type();
		func_decl_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
		func_decl_op.return_pointer_depth = static_cast<int>(return_type.pointer_depth());
		
		// Add function name and struct name
		func_decl_op.function_name = full_func_name;
		func_decl_op.struct_name = struct_name;
		
		// Add linkage (C++)
		func_decl_op.linkage = Linkage::None;

		// Add variadic flag (template functions are typically not variadic, but check anyway)
		func_decl_op.is_variadic = template_func_decl.is_variadic();

		// Mangled name is the full function name (already stored in StringBuilder's stable storage)
		func_decl_op.mangled_name = full_func_name;

		// Add function parameters with concrete types
		size_t template_unnamed_param_counter = 0;
		for (size_t i = 0; i < template_func_decl.parameter_nodes().size(); ++i) {
			const auto& param_node = template_func_decl.parameter_nodes()[i];
			if (param_node.is<DeclarationNode>()) {
				const DeclarationNode& param_decl = param_node.as<DeclarationNode>();

				FunctionParam func_param;
				// Use concrete type if this parameter uses a template parameter
				if (i < inst_info.template_args.size()) {
					Type concrete_type = inst_info.template_args[i];
					func_param.type = concrete_type;
					func_param.size_in_bits = static_cast<int>(get_type_size_bits(concrete_type));
					func_param.pointer_depth = 0;  // pointer depth
				} else {
					// Use original parameter type
					const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					func_param.type = param_type.type();
					func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
					func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
				}
				
				// Handle empty parameter names
				std::string_view param_name = param_decl.identifier_token().value();
				if (param_name.empty()) {
					func_param.name = StringTable::getOrInternStringHandle(
						StringBuilder().append("__param_").append(template_unnamed_param_counter++).commit());
				} else {
					func_param.name = StringTable::getOrInternStringHandle(param_name);
				}
				
				func_param.is_reference = false;
				func_param.is_rvalue_reference = false;
				func_param.cv_qualifier = CVQualifier::None;
				func_decl_op.parameters.push_back(func_param);
			}
		}

		// Emit function declaration IR (declaration only, no body)
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), mangled_token));
	}

	// Generate an instantiated member function template
	void generateTemplateInstantiation(const TemplateInstantiationInfo& inst_info) {
		// First, generate the FunctionDecl IR for the template instantiation
		// This must be done at the top level, BEFORE any function bodies that might call it
		generateTemplateFunctionDecl(inst_info);
		
		// Get the template function declaration
		const FunctionDeclarationNode& template_func_decl = inst_info.template_node_ptr->function_decl_node();
		const DeclarationNode& template_decl = template_func_decl.decl_node();

		// Create mangled name token
		Token mangled_token(
			Token::Type::Identifier,
			StringTable::getStringView(inst_info.mangled_name),
			template_decl.identifier_token().line(),
			template_decl.identifier_token().column(),
			template_decl.identifier_token().file_index()
		);

		// Enter function scope
		symbol_table.enter_scope(ScopeType::Function);

		// Get struct type info for member functions
		const TypeInfo* struct_type_info = nullptr;
		if (inst_info.struct_name.isValid()) {
			auto struct_type_it = gTypesByName.find(inst_info.struct_name);
			if (struct_type_it != gTypesByName.end()) {
				struct_type_info = struct_type_it->second;
			}
		}

		// For member functions, add implicit 'this' pointer to symbol table
		// This is needed so member variable access works during template body parsing
		if (struct_type_info) {
			// Create a 'this' pointer type (pointer to the struct)
			auto this_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
				Type::UserDefined,
				struct_type_info->type_index_,
				64,  // Pointer size in bits
				template_decl.identifier_token()
			);
			
			// Set pointer depth to 1 (this is a pointer)
			this_type_node.as<TypeSpecifierNode>().add_pointer_level(CVQualifier::None);
			
			// Create 'this' declaration
			Token this_token(Token::Type::Identifier, "this"sv, 
				template_decl.identifier_token().line(),
				template_decl.identifier_token().column(),
				template_decl.identifier_token().file_index());
			auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type_node, this_token);
			
			// Add 'this' to symbol table
			symbol_table.insert("this"sv, this_decl);
		}

		// Add function parameters to symbol table for name resolution during body parsing
		for (size_t i = 0; i < template_func_decl.parameter_nodes().size(); ++i) {
			const auto& param_node = template_func_decl.parameter_nodes()[i];
			if (param_node.is<DeclarationNode>()) {
				const DeclarationNode& param_decl = param_node.as<DeclarationNode>();

				// Create declaration with concrete type
				if (i < inst_info.template_args.size()) {
					Type concrete_type = inst_info.template_args[i];
					auto concrete_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
						concrete_type, 
						TypeQualifier::None, 
						get_type_size_bits(concrete_type),
						param_decl.identifier_token()
					);
					auto concrete_param_decl = ASTNode::emplace_node<DeclarationNode>(concrete_type_node, param_decl.identifier_token());
					symbol_table.insert(param_decl.identifier_token().value(), concrete_param_decl);
				} else {
					symbol_table.insert(param_decl.identifier_token().value(), param_node);
				}
			}
		}

		// Parse the template body with concrete types
		// Pass the struct name and type index so the parser can set up member function context
		auto body_node_opt = parser_->parseTemplateBody(
			inst_info.body_position,
			inst_info.template_param_names,
			inst_info.template_args,
			inst_info.struct_name.isValid() ? inst_info.struct_name : StringHandle(),  // Pass struct name
			struct_type_info ? struct_type_info->type_index_ : 0  // Pass type index
		);

		if (body_node_opt.has_value()) {
			if (body_node_opt->is<BlockNode>()) {
				const BlockNode& block = body_node_opt->as<BlockNode>();
				const auto& stmts = block.get_statements();
				
				// Visit each statement in the block to generate IR
				for (size_t i = 0; i < stmts.size(); ++i) {
					visit(stmts[i]);
				}
			}
		} else {
			std::cerr << "Warning: Template body does NOT have value!\n";
		}

		// Add implicit return for void functions
		const TypeSpecifierNode& return_type = template_decl.type_node().as<TypeSpecifierNode>();
		if (return_type.type() == Type::Void) {
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), mangled_token));
		}

		// Exit function scope
		symbol_table.exit_scope();
	}

	std::vector<IrOperand> generateTemplateParameterReferenceIr(const TemplateParameterReferenceNode& templateParamRefNode) {
		// This should not happen during normal code generation - template parameters should be substituted
		// during template instantiation. If we get here, it means template instantiation failed.
		std::string param_name = std::string(templateParamRefNode.param_name().view());
		std::cerr << "Error: Template parameter '" << param_name << "' was not substituted during template instantiation\n";
		std::cerr << "This indicates a bug in template instantiation - template parameters should be replaced with concrete types/values\n";
		assert(false && "Template parameter reference found during code generation - should have been substituted");
		return {};
	}

	// Generate IR for std::initializer_list construction
	// This is the "compiler magic" that creates a backing array on the stack
	// and constructs an initializer_list pointing to it
	std::vector<IrOperand> generateInitializerListConstructionIr(const InitializerListConstructionNode& init_list) {
		FLASH_LOG(Codegen, Debug, "Generating IR for InitializerListConstructionNode with ", 
		          init_list.size(), " elements");
		
		// Get the target initializer_list type
		const ASTNode& target_type_node = init_list.target_type();
		if (!target_type_node.is<TypeSpecifierNode>()) {
			FLASH_LOG(Codegen, Error, "InitializerListConstructionNode: target_type is not TypeSpecifierNode");
			return {};
		}
		const TypeSpecifierNode& target_type = target_type_node.as<TypeSpecifierNode>();
		
		// Get element type (default to int for now)
		int element_size_bits = 32;  // Default: int
		Type element_type = Type::Int;
		
		// Infer element type from first element if available
		std::vector<std::vector<IrOperand>> element_operands;
		for (size_t i = 0; i < init_list.elements().size(); ++i) {
			const ASTNode& elem = init_list.elements()[i];
			if (elem.is<ExpressionNode>()) {
				auto operands = visitExpressionNode(elem.as<ExpressionNode>());
				element_operands.push_back(operands);
				if (i == 0 && operands.size() >= 2) {
					if (std::holds_alternative<Type>(operands[0])) {
						element_type = std::get<Type>(operands[0]);
					}
					if (std::holds_alternative<int>(operands[1])) {
						element_size_bits = std::get<int>(operands[1]);
					}
				}
			}
		}
		
		// Step 1: Create a backing array on the stack using VariableDecl
		size_t array_size = init_list.size();
		size_t total_size_bits = array_size * element_size_bits;
		
		// Create a unique name for the backing array using the temp var number
		TempVar array_var = var_counter.next();
		StringBuilder array_name_builder;
		array_name_builder.append("__init_list_array_"sv).append(array_var.var_number);
		StringHandle array_name = StringTable::getOrInternStringHandle(array_name_builder.commit());
		
		VariableDeclOp array_decl;
		array_decl.var_name = array_name;
		array_decl.type = element_type;
		array_decl.size_in_bits = static_cast<int>(total_size_bits);
		array_decl.is_array = true;
		array_decl.array_element_type = element_type;
		array_decl.array_element_size = element_size_bits;
		array_decl.array_count = array_size;
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(array_decl), init_list.called_from()));
		
		// Step 2: Store each element into the backing array using ArrayStore
		for (size_t i = 0; i < element_operands.size(); ++i) {
			if (element_operands[i].size() < 3) continue;
			
			ArrayStoreOp store_op;
			store_op.element_type = element_type;
			store_op.element_size_in_bits = element_size_bits;
			store_op.array = array_name;
			store_op.index = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(i)};
			store_op.value = toTypedValue(element_operands[i]);
			store_op.member_offset = 0;  // Not a member array - direct local array
			store_op.is_pointer_to_array = false;  // This is an actual array, not a pointer
			ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(store_op), init_list.called_from()));
		}
		
		// Step 3: Create the initializer_list struct
		TypeIndex init_list_type_index = target_type.type_index();
		if (init_list_type_index >= gTypeInfo.size()) {
			FLASH_LOG(Codegen, Error, "InitializerListConstructionNode: invalid type index");
			return {};
		}
		
		const TypeInfo& init_list_type_info = gTypeInfo[init_list_type_index];
		const StructTypeInfo* init_list_struct_info = init_list_type_info.getStructInfo();
		if (!init_list_struct_info) {
			FLASH_LOG(Codegen, Error, "InitializerListConstructionNode: target type is not a struct");
			return {};
		}
		
		int init_list_size_bits = static_cast<int>(init_list_struct_info->total_size * 8);
		
		// Create a unique name for the initializer_list struct using the temp var number
		TempVar init_list_var = var_counter.next();
		StringBuilder init_list_name_builder;
		init_list_name_builder.append("__init_list_"sv).append(init_list_var.var_number);
		StringHandle init_list_name = StringTable::getOrInternStringHandle(init_list_name_builder.commit());
		
		VariableDeclOp init_list_decl;
		init_list_decl.var_name = init_list_name;
		init_list_decl.type = Type::Struct;
		init_list_decl.size_in_bits = init_list_size_bits;
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(init_list_decl), init_list.called_from()));
		
		// Store pointer to array (first member)
		if (init_list_struct_info->members.size() >= 1) {
			const auto& ptr_member = init_list_struct_info->members[0];
			MemberStoreOp store_ptr;
			store_ptr.object = init_list_name;  // Use StringHandle
			store_ptr.member_name = ptr_member.getName();
			store_ptr.offset = static_cast<int>(ptr_member.offset);
			// Create TypedValue for pointer to array - need to set pointer_depth explicitly
			TypedValue ptr_value;
			ptr_value.type = element_type;
			ptr_value.size_in_bits = 64;  // pointer size
			ptr_value.value = array_name;
			ptr_value.pointer_depth = 1;  // This is a pointer to the array
			store_ptr.value = ptr_value;
			store_ptr.struct_type_info = nullptr;
			store_ptr.is_reference = false;
			store_ptr.is_rvalue_reference = false;
			ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_ptr), init_list.called_from()));
		}
		
		// Store size (second member)
		if (init_list_struct_info->members.size() >= 2) {
			const auto& size_member = init_list_struct_info->members[1];
			MemberStoreOp store_size;
			store_size.object = init_list_name;  // Use StringHandle
			store_size.member_name = size_member.getName();
			store_size.offset = static_cast<int>(size_member.offset);
			store_size.value = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(array_size)};
			store_size.struct_type_info = nullptr;
			store_size.is_reference = false;
			store_size.is_rvalue_reference = false;
			ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_size), init_list.called_from()));
		}
		
		// Return operands for the constructed initializer_list
		// Return the StringHandle for the variable name so the caller can use it
		return { Type::Struct, init_list_size_bits, init_list_name, static_cast<unsigned long long>(init_list_type_index) };
	}

	std::vector<IrOperand> generateConstructorCallIr(const ConstructorCallNode& constructorCallNode) {
		// Get the type being constructed
		const ASTNode& type_node = constructorCallNode.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			assert(false && "Constructor call type node must be a TypeSpecifierNode");
			return {};
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();

		// For constructor calls, we need to generate a constructor call instruction
		// In C++, constructors are named after the class
		StringHandle constructor_name;
		if (is_struct_type(type_spec.type())) {
			// If type_index is set, use it
			if (type_spec.type_index() != 0) {
				constructor_name = gTypeInfo[type_spec.type_index()].name();
			} else {
				// Otherwise, use the token value (the identifier name)
				constructor_name = StringTable::getOrInternStringHandle(type_spec.token().value());
			}
		} else {
			// For basic types, constructors might not exist, but we can handle them as value construction
			constructor_name = gTypeInfo[type_spec.type_index()].name();
		}

		// Create a temporary variable for the result (the constructed object)
		TempVar ret_var = var_counter.next();
		
		// Get the actual size of the struct from gTypeInfo
		int actual_size_bits = static_cast<int>(type_spec.size_in_bits());
		const StructTypeInfo* struct_info = nullptr;
		if (type_spec.type() == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
			if (type_info.struct_info_) {
				actual_size_bits = static_cast<int>(type_info.struct_info_->total_size * 8);
				struct_info = type_info.struct_info_.get();
			}
		} else {
			// Fallback: look up by name
			auto type_it = gTypesByName.find(constructor_name);
			if (type_it != gTypesByName.end() && type_it->second->struct_info_) {
				actual_size_bits = static_cast<int>(type_it->second->struct_info_->total_size * 8);
				struct_info = type_it->second->struct_info_.get();
			}
		}
		
		// Build ConstructorCallOp
		ConstructorCallOp ctor_op;
		ctor_op.struct_name = constructor_name;
		ctor_op.object = ret_var;  // The temporary variable that will hold the result

		// Find the matching constructor to get parameter types for reference handling
		const ConstructorDeclarationNode* matching_ctor = nullptr;
		size_t num_args = 0;
		constructorCallNode.arguments().visit([&](ASTNode) { num_args++; });
		
		if (struct_info) {
			for (const auto& func : struct_info->member_functions) {
				if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
					const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
					const auto& params = ctor_node.parameter_nodes();
					
					// Match constructor with same number of parameters or with default parameters
					if (params.size() == num_args) {
						matching_ctor = &ctor_node;
						break;
					} else if (params.size() > num_args) {
						// Check if remaining params have defaults
						bool all_have_defaults = true;
						for (size_t i = num_args; i < params.size(); ++i) {
							if (!params[i].is<DeclarationNode>() || 
							    !params[i].as<DeclarationNode>().has_default_value()) {
								all_have_defaults = false;
								break;
							}
						}
						if (all_have_defaults) {
							matching_ctor = &ctor_node;
							break;
						}
					}
				}
			}
		}
		
		// Get constructor parameter types for reference handling
		const auto& ctor_params = matching_ctor ? matching_ctor->parameter_nodes() : std::vector<ASTNode>{};

		// Generate IR for constructor arguments and add them to ctor_op.arguments
		size_t arg_index = 0;
		constructorCallNode.arguments().visit([&](ASTNode argument) {
			// Get the parameter type for this argument (if it exists)
			const TypeSpecifierNode* param_type = nullptr;
			if (arg_index < ctor_params.size() && ctor_params[arg_index].is<DeclarationNode>()) {
				param_type = &ctor_params[arg_index].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			}
			
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
			// argumentIrOperands = [type, size, value]
			if (argumentIrOperands.size() >= 3) {
				TypedValue tv;
				
				// Check if parameter expects a reference and argument is an identifier
				if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference()) &&
				    std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
					const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
					std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
					if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& arg_decl = symbol->as<DeclarationNode>();
						const auto& arg_type = arg_decl.type_node().as<TypeSpecifierNode>();
						
						if (arg_type.is_reference() || arg_type.is_rvalue_reference()) {
							// Argument is already a reference - just pass it through
							tv = toTypedValue(argumentIrOperands);
						} else {
							// Argument is a value - take its address
							TempVar addr_var = var_counter.next();
							AddressOfOp addr_op;
							addr_op.result = addr_var;
							addr_op.operand.type = arg_type.type();
							addr_op.operand.size_in_bits = static_cast<int>(arg_type.size_in_bits());
							addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
							addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
							ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), constructorCallNode.called_from()));
							
							// Create TypedValue with the address
							tv.type = arg_type.type();
							tv.size_in_bits = 64;  // Pointer size
							tv.value = addr_var;
							tv.ref_qualifier = ReferenceQualifier::LValueReference;  // Mark as reference parameter
							tv.cv_qualifier = param_type->cv_qualifier();  // Set CV qualifier from parameter
						}
					} else {
						// Not a simple identifier or not found - use as-is
						tv = toTypedValue(argumentIrOperands);
					}
				} else {
					// Not a reference parameter or not an identifier - use as-is
					tv = toTypedValue(argumentIrOperands);
				}
				
				// If we have parameter type information, use it to set pointer depth and CV qualifiers
				if (param_type) {
					tv.pointer_depth = static_cast<int>(param_type->pointer_depth());
					// For pointer types, also extract CV qualifiers from pointer levels
					if (param_type->is_pointer() && !param_type->pointer_levels().empty()) {
						// Use CV qualifier from the first pointer level (T* const -> const)
						// For now, we'll use the main CV qualifier
						if (!tv.is_reference()) {
							tv.cv_qualifier = param_type->cv_qualifier();
						}
					}
					// For reference types, use the CV qualifier
					if (param_type->is_reference() || param_type->is_rvalue_reference()) {
						tv.cv_qualifier = param_type->cv_qualifier();
					}
					// Also update type_index if it's a struct type
					if (param_type->type() == Type::Struct && param_type->type_index() != 0) {
						tv.type_index = param_type->type_index();
					}
				}
				
				ctor_op.arguments.push_back(std::move(tv));
			}
			arg_index++;
		});

		// Fill in default arguments for parameters that weren't explicitly provided
		// Find the matching constructor and add default values for missing parameters
		if (struct_info) {
			size_t num_explicit_args = ctor_op.arguments.size();
			
			// Find a constructor that has MORE parameters than explicit arguments
			// and has default values for those extra parameters
			for (const auto& func : struct_info->member_functions) {
				if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
					const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
					const auto& params = ctor_node.parameter_nodes();
					
					// Only consider constructors that have MORE parameters than explicit args
					// (constructors with exact match don't need default argument filling)
					if (params.size() > num_explicit_args) {
						// Check if the remaining parameters all have default values
						bool all_remaining_have_defaults = true;
						for (size_t i = num_explicit_args; i < params.size(); ++i) {
							if (params[i].is<DeclarationNode>()) {
								if (!params[i].as<DeclarationNode>().has_default_value()) {
									all_remaining_have_defaults = false;
									break;
								}
							} else {
								all_remaining_have_defaults = false;
								break;
							}
						}
						
						if (all_remaining_have_defaults) {
							// Generate IR for the default values of the remaining parameters
							for (size_t i = num_explicit_args; i < params.size(); ++i) {
								const auto& param_decl = params[i].as<DeclarationNode>();
								const ASTNode& default_node = param_decl.default_value();
								if (default_node.is<ExpressionNode>()) {
									auto default_operands = visitExpressionNode(default_node.as<ExpressionNode>());
									if (default_operands.size() >= 3) {
										TypedValue default_arg = toTypedValue(default_operands);
										ctor_op.arguments.push_back(std::move(default_arg));
									}
								}
							}
							break;  // Found a matching constructor
						}
					}
				}
			}
		}

		// Check if we should use RVO (Return Value Optimization)
		// If we're in a return statement and the function has a hidden return parameter,
		// construct directly into the return slot instead of into a temporary
		if (in_return_statement_with_rvo_) {
			ctor_op.use_return_slot = true;
			// The return slot offset will be set by IRConverter when it processes the return
			// For now, we just mark that RVO should be used
			FLASH_LOG(Codegen, Debug,
				"Constructor call will use RVO (construct directly in return slot)");
		}

		// Add the constructor call instruction (use ConstructorCall opcode)
		ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), constructorCallNode.called_from()));

		// Mark the result as a prvalue eligible for RVO (C++17 mandatory copy elision)
		// Constructor calls always produce prvalues, which are eligible for copy elision
		// when returned from a function
		setTempVarMetadata(ret_var, TempVarMetadata::makeRVOEligiblePRValue());
		
		FLASH_LOG_FORMAT(Codegen, Debug,
			"Marked constructor call result {} as RVO-eligible prvalue", ret_var.name());

		// Return the result variable with the constructed type, including type_index for struct types
		TypeIndex result_type_index = type_spec.type_index();
		return { type_spec.type(), actual_size_bits, ret_var, static_cast<unsigned long long>(result_type_index) };
	}

};
