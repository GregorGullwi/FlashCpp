	std::vector<IrOperand> generateFunctionCallIr(const FunctionCallNode& functionCallNode) {
		std::vector<IrOperand> irOperands;

		const auto& decl_node = functionCallNode.function_declaration();
		std::string_view func_name_view = decl_node.identifier_token().value();
		
		FLASH_LOG_FORMAT(Codegen, Debug, "=== generateFunctionCallIr: func_name={} ===", func_name_view);

		// Check for compiler intrinsics and handle them specially
		auto intrinsic_result = tryGenerateIntrinsicIr(func_name_view, functionCallNode);
		if (intrinsic_result.has_value()) {
			return intrinsic_result.value();
		}

		// Check if this function is marked as inline_always (pure expression template instantiations)
		// These functions should always be inlined and never generate calls
		// Look up the function to check its inline_always flag
		extern SymbolTable gSymbolTable;
		auto all_overloads = gSymbolTable.lookup_all(func_name_view);
		
		for (const auto& overload : all_overloads) {
			if (overload.is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode* overload_func_decl = &overload.as<FunctionDeclarationNode>();
				const DeclarationNode* overload_decl = &overload_func_decl->decl_node();
				
				// Check if this is the matching overload
				if (overload_decl == &decl_node) {
					// Found the matching function - check if it should be inlined
					if (overload_func_decl->is_inline_always() && functionCallNode.arguments().size() == 1) {
						// Check if function returns a reference - if so, we need special handling
						const TypeSpecifierNode& return_type_spec = overload_decl->type_node().as<TypeSpecifierNode>();
						bool returns_reference = return_type_spec.is_reference() || return_type_spec.is_rvalue_reference();
						
						auto arg_node = functionCallNode.arguments()[0];
						if (arg_node.is<ExpressionNode>()) {
							FLASH_LOG(Codegen, Debug, "Inlining pure expression function (inline_always): ", func_name_view);
							
							if (returns_reference) {
								// For functions returning references (like std::move, std::forward),
								// we need to generate an addressof the argument, not just return it
								const ExpressionNode& arg_expr = arg_node.as<ExpressionNode>();
								
								// Check if the argument is an identifier (common case for move(x))
								if (std::holds_alternative<IdentifierNode>(arg_expr)) {
									const IdentifierNode& ident = std::get<IdentifierNode>(arg_expr);
									
									// Generate addressof for the identifier
									TempVar result_var = var_counter.next();
									AddressOfOp op;
									op.result = result_var;
									
									// Get type info from the identifier
									StringHandle id_handle = StringTable::getOrInternStringHandle(ident.name());
									std::optional<ASTNode> symbol = symbol_table.lookup(id_handle);
									if (!symbol.has_value() && global_symbol_table_) {
										symbol = global_symbol_table_->lookup(id_handle);
									}
									
									Type operand_type = Type::Int;  // Default
									int operand_size = 32;
									if (symbol.has_value()) {
										if (symbol->is<DeclarationNode>()) {
											const TypeSpecifierNode& type = symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
											operand_type = type.type();
											operand_size = static_cast<int>(type.size_in_bits());
											if (operand_size == 0) operand_size = get_type_size_bits(operand_type);
										} else if (symbol->is<VariableDeclarationNode>()) {
											const TypeSpecifierNode& type = symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
											operand_type = type.type();
											operand_size = static_cast<int>(type.size_in_bits());
											if (operand_size == 0) operand_size = get_type_size_bits(operand_type);
										}
									}
									
									op.operand.type = operand_type;
									op.operand.size_in_bits = operand_size;
									op.operand.pointer_depth = 0;
									op.operand.value = id_handle;
									
									ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, op, Token()));
									
									// Return pointer type (64-bit address) with pointer depth 1
									return { operand_type, 64, result_var, 1ULL };
								}
								// For non-identifier expressions, fall through to generate a regular call
								// (we can't inline complex expressions that need reference semantics)
							} else {
								// Non-reference return - can inline directly by returning argument
								auto arg_ir = visitExpressionNode(arg_node.as<ExpressionNode>());
								return arg_ir;
							}
						}
					}
					break;  // Found the matching function, stop searching
				}
			}
		}

		// Check if this is a function pointer call
		// Look up the identifier in the symbol table to see if it's a function pointer variable
		const std::optional<ASTNode> func_symbol = symbol_table.lookup(func_name_view);
		const DeclarationNode* func_ptr_decl = nullptr;
		
		// Check for DeclarationNode directly
		if (func_symbol.has_value() && func_symbol->is<DeclarationNode>()) {
			func_ptr_decl = &func_symbol->as<DeclarationNode>();
		}
		// Also check for VariableDeclarationNode (from comma-separated declarations)
		else if (func_symbol.has_value() && func_symbol->is<VariableDeclarationNode>()) {
			func_ptr_decl = &func_symbol->as<VariableDeclarationNode>().declaration();
		}
		
		if (func_ptr_decl) {
			const auto& func_type = func_ptr_decl->type_node().as<TypeSpecifierNode>();

			// Check if this is a function pointer or auto type (which could be a callable)
			// auto&& parameters in recursive lambdas need to be treated as callables
			if (func_type.is_function_pointer()) {
				// This is an indirect call through a function pointer
				// Generate IndirectCall IR: [result_var, func_ptr_var, arg1, arg2, ...]
				TempVar ret_var = var_counter.next();
				
				// Mark function return value as prvalue (Option 2: Value Category Tracking)
				setTempVarMetadata(ret_var, TempVarMetadata::makePRValue());
				
				// Generate IR for function arguments
				std::vector<TypedValue> arguments;
				functionCallNode.arguments().visit([&](ASTNode argument) {
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

				// Add the indirect call instruction
				IndirectCallOp op{
					.result = ret_var,
					.function_pointer = StringTable::getOrInternStringHandle(func_name_view),
					.arguments = std::move(arguments)
				};
				ir_.addInstruction(IrOpcode::IndirectCall, std::move(op), functionCallNode.called_from());

				// Return the result variable with the return type from the function signature
				if (func_type.has_function_signature()) {
					const auto& sig = func_type.function_signature();
					return { sig.return_type, 64, ret_var, 0ULL };  // 64 bits for return value
				} else {
					// For auto types or missing signature, default to int
					return { Type::Int, 32, ret_var, 0ULL };
				}
			}
			
			// Handle auto-typed callable (e.g., recursive lambda pattern: self(self, n-1))
			// When an auto&& parameter is called like a function, it's a callable object
			// We need to generate a member function call to its operator()
			if (func_type.type() == Type::Auto) {
				// This is likely a recursive lambda call pattern where 'self' is a lambda passed as auto&&
				// We need to find the lambda's closure type and call its operator()
				
				// Look up the deduced type for this auto parameter
				// First, check if we're inside a lambda context
				if (current_lambda_context_.isActive()) {
					// We're inside a lambda - this could be a recursive call through an auto&& parameter
					// The pattern is: auto factorial = [](auto&& self, int n) { ... self(self, n-1); }
					
					// Get the current lambda's closure type name to construct the operator() call
					std::string_view closure_type_name = StringTable::getStringView(current_lambda_context_.closure_type);
					
					// Generate a member function call to operator()
					TempVar ret_var = var_counter.next();
					setTempVarMetadata(ret_var, TempVarMetadata::makePRValue());
					
					// Build the call operands
					CallOp call_op;
					call_op.result = ret_var;
					call_op.return_type = Type::Int;  // Default, will be refined
					call_op.return_size_in_bits = 32;
					call_op.is_variadic = false;
					
					// Add the object (self) as the first argument (this pointer)
					call_op.args.push_back(TypedValue{
						.type = Type::Struct,
						.size_in_bits = 64,  // Pointer size
						.value = IrValue(StringTable::getOrInternStringHandle(func_name_view))
					});
					
					// Generate IR for the remaining arguments and collect types for mangling
					std::vector<TypeSpecifierNode> arg_types;
					
					// Look up the closure type to get the proper type_index
					TypeIndex closure_type_index = 0;
					auto it = gTypesByName.find(current_lambda_context_.closure_type);
					if (it != gTypesByName.end()) {
						closure_type_index = it->second->type_index_;
					}
					
					functionCallNode.arguments().visit([&](ASTNode argument) {
						// Check if this argument is the same as the callee (recursive lambda pattern)
						// In that case, we should pass the reference directly without dereferencing
						bool is_self_arg = false;
						const ExpressionNode& arg_expr = argument.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(arg_expr)) {
							const auto& id = std::get<IdentifierNode>(arg_expr);
							if (id.name() == func_name_view) {
								is_self_arg = true;
							}
						}
						
						if (is_self_arg) {
							// For the self argument in recursive lambda calls, pass the reference directly
							// Don't call visitExpressionNode which would dereference it
							call_op.args.push_back(TypedValue{
								.type = Type::Struct,
								.size_in_bits = 64,  // Reference/pointer size
								.value = IrValue(StringTable::getOrInternStringHandle(func_name_view))
							});
							
							// Type for mangling is rvalue reference to closure type
							TypeSpecifierNode self_type(Type::Struct, closure_type_index, 8, Token());
							self_type.set_reference(true);
							arg_types.push_back(self_type);
						} else {
							// Normal argument - visit the expression
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
							call_op.args.push_back(TypedValue{arg_type, arg_size, arg_value});
							
							// Type for mangling
							TypeSpecifierNode type_node(arg_type, 0, arg_size, Token());
							arg_types.push_back(type_node);
						}
					});
					
					// Generate mangled name for operator() call
					TypeSpecifierNode return_type_node(Type::Int, 0, 32, Token());
					std::string_view mangled_name = generateMangledNameForCall(
						"operator()",
						return_type_node,
						arg_types,
						false,
						closure_type_name
					);
					call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
					
					ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), functionCallNode.called_from()));
					
					return { Type::Int, 32, ret_var, 0ULL };
				}
			}
		}

		// Get the function declaration to extract parameter types for mangling
		std::string_view function_name = func_name_view;
		
		// Remap compiler builtins to their libc equivalents
		// __builtin_strlen -> strlen (libc function)
		if (func_name_view == "__builtin_strlen") {
			function_name = "strlen";
		}
		
		bool has_precomputed_mangled = functionCallNode.has_mangled_name();
		const FunctionDeclarationNode* matched_func_decl = nullptr;
		
		// Check if FunctionCallNode has a pre-computed mangled name (for namespace-scoped functions)
		// If so, use it directly and skip the lookup logic
		if (has_precomputed_mangled) {
			function_name = functionCallNode.mangled_name();
			FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name from FunctionCallNode: {}", function_name);
			// We don't need to find matched_func_decl since we already have the mangled name
			// The mangled name is sufficient for generating the call instruction
		}

		// Look up the function in the global symbol table to get all overloads
		// Use global_symbol_table_ if available, otherwise fall back to local symbol_table
		auto scoped_overloads = global_symbol_table_
			? global_symbol_table_->lookup_all(decl_node.identifier_token().value())
			: symbol_table.lookup_all(decl_node.identifier_token().value());

		// Also try looking up in gSymbolTable directly for comparison
		extern SymbolTable gSymbolTable;
		auto gSymbolTable_overloads = gSymbolTable.lookup_all(decl_node.identifier_token().value());

		// Find the matching overload by comparing the DeclarationNode address
		// This works because the FunctionCallNode holds a reference to the specific
		// DeclarationNode that was selected by overload resolution
		FLASH_LOG_FORMAT(Codegen, Debug, "Looking for function: {}, all_overloads size: {}, gSymbolTable_overloads size: {}", 
			func_name_view, scoped_overloads.size(), gSymbolTable_overloads.size());
		for (const auto& overload : scoped_overloads) {
			const FunctionDeclarationNode* overload_func_decl = nullptr;
			if (overload.is<FunctionDeclarationNode>()) {
				overload_func_decl = &overload.as<FunctionDeclarationNode>();
			} else if (overload.is<TemplateFunctionDeclarationNode>()) {
				overload_func_decl = &overload.as<TemplateFunctionDeclarationNode>().function_decl_node();
			}

			if (overload_func_decl) {
				const DeclarationNode* overload_decl = &overload_func_decl->decl_node();
				FLASH_LOG_FORMAT(Codegen, Debug, "  Checking overload at {}, looking for {}", 
					(void*)overload_decl, (void*)&decl_node);
				if (overload_decl == &decl_node) {
					// Found the matching overload
					matched_func_decl = overload_func_decl;

					// Use pre-computed mangled name if available, otherwise generate it
					if (!has_precomputed_mangled) {
						if (matched_func_decl->has_mangled_name()) {
							function_name = matched_func_decl->mangled_name();
							FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name: {}", function_name);
						} else if (matched_func_decl->linkage() != Linkage::C) {
							function_name = generateMangledNameForCall(*matched_func_decl, "", current_namespace_stack_);
							FLASH_LOG_FORMAT(Codegen, Debug, "Generated mangled name (no pre-computed): {}", function_name);
						}
					}
					break;
				}
			}
		}
	
		// Fallback: if pointer comparison failed (e.g., for template instantiations),
		// try to find the function by checking if there's only one overload with this name
		if (!matched_func_decl && scoped_overloads.size() == 1 &&
		    (scoped_overloads[0].is<FunctionDeclarationNode>() || scoped_overloads[0].is<TemplateFunctionDeclarationNode>())) {
			matched_func_decl = scoped_overloads[0].is<FunctionDeclarationNode>()
				? &scoped_overloads[0].as<FunctionDeclarationNode>()
				: &scoped_overloads[0].as<TemplateFunctionDeclarationNode>().function_decl_node();
	
			// Use pre-computed mangled name if available, otherwise generate it
			if (!has_precomputed_mangled) {
				if (matched_func_decl->has_mangled_name()) {
					function_name = matched_func_decl->mangled_name();
					FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name (fallback 1): {}", function_name);
				} else if (matched_func_decl->linkage() != Linkage::C) {
					function_name = generateMangledNameForCall(*matched_func_decl, "", current_namespace_stack_);
				}
			}
		}

		// Additional fallback: check gSymbolTable directly (for member functions added during delayed parsing)
		if (!matched_func_decl && gSymbolTable_overloads.size() == 1 &&
		    (gSymbolTable_overloads[0].is<FunctionDeclarationNode>() || gSymbolTable_overloads[0].is<TemplateFunctionDeclarationNode>())) {
			matched_func_decl = gSymbolTable_overloads[0].is<FunctionDeclarationNode>()
				? &gSymbolTable_overloads[0].as<FunctionDeclarationNode>()
				: &gSymbolTable_overloads[0].as<TemplateFunctionDeclarationNode>().function_decl_node();
	
			// Use pre-computed mangled name if available, otherwise generate it
			if (!has_precomputed_mangled) {
				if (matched_func_decl->has_mangled_name()) {
					function_name = matched_func_decl->mangled_name();
					FLASH_LOG_FORMAT(Codegen, Debug, "Using pre-computed mangled name (fallback 2): {}", function_name);
				} else if (matched_func_decl->linkage() != Linkage::C) {
					function_name = generateMangledNameForCall(*matched_func_decl, "", current_namespace_stack_);
				}
			}
		}

		// Final fallback: if we're in a member function, check the current struct's member functions
		if (!matched_func_decl && current_struct_name_.isValid()) {
			auto type_it = gTypesByName.find(current_struct_name_);
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					for (const auto& member_func : struct_info->member_functions) {
						if (member_func.function_decl.is<FunctionDeclarationNode>()) {
							const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
							if (func_decl.decl_node().identifier_token().value() == func_name_view) {
								// Found matching member function
								matched_func_decl = &func_decl;
							
								// Use pre-computed mangled name if available, otherwise generate it
								if (!has_precomputed_mangled) {
									if (matched_func_decl->has_mangled_name()) {
										function_name = matched_func_decl->mangled_name();
									} else if (matched_func_decl->linkage() != Linkage::C) {
										function_name = generateMangledNameForCall(*matched_func_decl, StringTable::getStringView(current_struct_name_));
									}
								}
								break;
							}
						}
					}
				}
			
				// If not found in current struct, check base classes
				if (!matched_func_decl && struct_info) {
					// Search through base classes recursively
					std::function<void(const StructTypeInfo*)> searchBaseClasses = [&](const StructTypeInfo* current_struct) {
						for (const auto& base_spec : current_struct->base_classes) {
							// Look up base class in gTypeInfo
							if (base_spec.type_index < gTypeInfo.size()) {
								const TypeInfo& base_type_info = gTypeInfo[base_spec.type_index];
								if (base_type_info.isStruct()) {
									const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
									if (base_struct_info) {
										// Check member functions in base class
										for (const auto& member_func : base_struct_info->member_functions) {
											if (member_func.function_decl.is<FunctionDeclarationNode>()) {
												const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
												if (func_decl.decl_node().identifier_token().value() == func_name_view) {
													// Found matching member function in base class
													matched_func_decl = &func_decl;
												
													// Use pre-computed mangled name if available
													if (!has_precomputed_mangled) {
														if (matched_func_decl->has_mangled_name()) {
															function_name = matched_func_decl->mangled_name();
														} else if (matched_func_decl->linkage() != Linkage::C) {
															// Generate mangled name with base class name
															function_name = generateMangledNameForCall(*matched_func_decl, StringTable::getStringView(base_struct_info->getName()));
														}
													}
													return; // Stop searching once found
												}
											}
										}
										// Recursively search base classes of this base class
										if (!matched_func_decl) {
											searchBaseClasses(base_struct_info);
										}
									}
								}
							}
						}
					};
					searchBaseClasses(struct_info);
				}
			}
		}

		// Fallback: if the function is a qualified static member call (ClassName::method),
		// look up the struct by iterating over known types and matching the function.
		// Note: We match by function name AND parameter count to avoid false positives
		// from identically named functions on different structs.
		if (!matched_func_decl && !has_precomputed_mangled) {
			size_t expected_param_count = 0;
			functionCallNode.arguments().visit([&](ASTNode) { ++expected_param_count; });
			
			for (const auto& [name_handle, type_info_ptr] : gTypesByName) {
				if (!type_info_ptr->isStruct()) continue;
				const StructTypeInfo* struct_info = type_info_ptr->getStructInfo();
				if (!struct_info) continue;
				// Skip pattern structs (templates) - they shouldn't be used for code generation
				if (gTemplateRegistry.isPatternStructName(name_handle)) continue;
				if (type_info_ptr->is_incomplete_instantiation_) continue;
				// Skip uninstantiated class template patterns — if the struct was registered
				// as a class template but is NOT a template instantiation, it is an
				// uninstantiated pattern and must not be used for codegen.
				// Template instantiations (isTemplateInstantiation) are concrete types
				// and should NOT be skipped.
				if (!type_info_ptr->isTemplateInstantiation()) {
					if (gTemplateRegistry.isClassTemplate(name_handle)) {
						continue;
					}
				}
				
				std::string_view struct_type_name = StringTable::getStringView(name_handle);
				for (const auto& member_func : struct_info->member_functions) {
					if (member_func.function_decl.is<FunctionDeclarationNode>()) {
						const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
						if (func_decl.decl_node().identifier_token().value() == func_name_view
						    && func_decl.parameter_nodes().size() == expected_param_count) {
							matched_func_decl = &func_decl;
							// Use the struct type name for mangling (not parent_struct_name which
							// may reference a template pattern)
							std::string_view parent_for_mangling = func_decl.parent_struct_name();
							if (gTemplateRegistry.isPatternStructName(StringTable::getOrInternStringHandle(parent_for_mangling))) {
								parent_for_mangling = struct_type_name;
							}
							if (func_decl.has_mangled_name()) {
								function_name = func_decl.mangled_name();
							} else if (func_decl.linkage() != Linkage::C) {
								function_name = generateMangledNameForCall(func_decl, parent_for_mangling);
							}
							FLASH_LOG_FORMAT(Codegen, Debug, "Resolved static member function via struct search: {} -> {}", func_name_view, function_name);
							
							// Queue all member functions of this struct for deferred generation
							// since the matched function may call other members (e.g., lowest() calls min()).
							// Derive namespace from the matched function's parent struct first (authoritative),
							// then fall back to the resolved type name when needed.
							std::vector<std::string> ns_stack;
							auto parse_namespace_into_stack = [&](std::string_view qualified_name) {
								size_t ns_end = qualified_name.rfind("::");
								if (ns_end == std::string_view::npos) {
									return;
								}
								std::string_view ns_part = qualified_name.substr(0, ns_end);
								size_t start = 0;
								while (start < ns_part.size()) {
									size_t pos = ns_part.find("::", start);
									if (pos == std::string_view::npos) {
										ns_stack.emplace_back(ns_part.substr(start));
										break;
									}
									ns_stack.emplace_back(ns_part.substr(start, pos - start));
									start = pos + 2;
								}
							};

							parse_namespace_into_stack(parent_for_mangling);
							if (ns_stack.empty()) {
								parse_namespace_into_stack(struct_type_name);
							}
							if (ns_stack.empty()) {
								parse_namespace_into_stack(StringTable::getStringView(type_info_ptr->name()));
							}
							for (const auto& mf : struct_info->member_functions) {
								DeferredMemberFunctionInfo deferred_info;
								deferred_info.struct_name = type_info_ptr->name();
								deferred_info.function_node = mf.function_decl;
								deferred_info.namespace_stack = ns_stack;
								deferred_member_functions_.push_back(std::move(deferred_info));
							}
							
							break;
						}
					}
				}
				if (matched_func_decl) break;
			}
		}

		// Handle dependent qualified function names: Base$dependentHash::member
		// These occur when a template body contains Base<T>::member() and T is substituted
		// but the hash was computed with the dependent type, not the concrete type.
		if (!matched_func_decl) {
			size_t scope_pos = func_name_view.find("::");
			std::string_view base_template_name;
			if (scope_pos != std::string_view::npos) {
				base_template_name = extractBaseTemplateName(func_name_view.substr(0, scope_pos));
			}
			if (!base_template_name.empty() && scope_pos != std::string_view::npos) {
				std::string_view member_name = func_name_view.substr(scope_pos + 2);
				
				FLASH_LOG_FORMAT(Codegen, Debug, "Resolving dependent qualified call: base_template='{}', member='{}'", base_template_name, member_name);
				
				// Search current struct's base classes for a matching template instantiation
				if (current_struct_name_.isValid()) {
					auto type_it = gTypesByName.find(current_struct_name_);
					if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
						const StructTypeInfo* curr_struct = type_it->second->getStructInfo();
						if (curr_struct) {
							for (const auto& base_spec : curr_struct->base_classes) {
								if (base_spec.type_index < gTypeInfo.size()) {
									const TypeInfo& base_type_info = gTypeInfo[base_spec.type_index];
									if (base_type_info.isTemplateInstantiation() && 
									    StringTable::getStringView(base_type_info.baseTemplateName()) == base_template_name &&
									    base_type_info.isStruct()) {
										const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
										if (base_struct_info) {
											for (const auto& member_func : base_struct_info->member_functions) {
												if (member_func.function_decl.is<FunctionDeclarationNode>()) {
													const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
													std::string_view func_id = func_decl.decl_node().identifier_token().value();
													if (func_id == member_name) {
														matched_func_decl = &func_decl;
														if (!has_precomputed_mangled) {
															if (matched_func_decl->has_mangled_name()) {
																function_name = matched_func_decl->mangled_name();
															} else if (matched_func_decl->linkage() != Linkage::C) {
																function_name = generateMangledNameForCall(*matched_func_decl, StringTable::getStringView(base_struct_info->getName()));
															}
														}
														break;
													}
												}
											}
											if (matched_func_decl) break;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	
		// Always add the return variable and function name (mangled for overload resolution)
		FLASH_LOG_FORMAT(Codegen, Debug, "Final function_name for call: '{}'", function_name);
		TempVar ret_var = var_counter.next();
		
		// Mark function return value as prvalue (Option 2: Value Category Tracking)
		// Function returns (by value) produce temporaries with no persistent identity
		setTempVarMetadata(ret_var, TempVarMetadata::makePRValue());
		
		irOperands.emplace_back(ret_var);
		irOperands.emplace_back(StringTable::getOrInternStringHandle(function_name));

		const std::vector<CachedParamInfo>* cached_param_list = nullptr;
		{
			StringHandle cache_key = functionCallNode.has_mangled_name()
				? functionCallNode.mangled_name_handle()
				: StringTable::getOrInternStringHandle(function_name);
			auto cache_it = function_param_cache_.find(cache_key);
			if (cache_it != function_param_cache_.end()) {
				cached_param_list = &cache_it->second;
			}
		}

		// Process arguments - match them with parameter types
		size_t arg_index = 0;
		const auto& func_decl_node = functionCallNode.function_declaration();
		
		// Get parameters from the function declaration
		std::vector<ASTNode> param_nodes;
		if (matched_func_decl) {
			param_nodes = matched_func_decl->parameter_nodes();
		} else {
			// Try to get from the function declaration stored in FunctionCallNode
			// Look up the function in symbol table to get full declaration with parameters
			auto local_func_symbol = symbol_table.lookup(func_decl_node.identifier_token().value());
			if (!local_func_symbol.has_value() && global_symbol_table_) {
				local_func_symbol = global_symbol_table_->lookup(func_decl_node.identifier_token().value());
			}
			if (local_func_symbol.has_value() && local_func_symbol->is<FunctionDeclarationNode>()) {
				const auto& resolved_func_decl = local_func_symbol->as<FunctionDeclarationNode>();
				param_nodes = resolved_func_decl.parameter_nodes();
			}
		}
		
		functionCallNode.arguments().visit([&](ASTNode argument) {
			// Get the parameter type for this argument (if it exists)
			const TypeSpecifierNode* param_type = nullptr;
			const DeclarationNode* param_decl = nullptr;
			if (arg_index < param_nodes.size() && param_nodes[arg_index].is<DeclarationNode>()) {
				param_decl = &param_nodes[arg_index].as<DeclarationNode>();
			} else if (!param_nodes.empty() && param_nodes.back().is<DeclarationNode>()) {
				const auto& last_param = param_nodes.back().as<DeclarationNode>();
				if (last_param.is_parameter_pack()) {
					param_decl = &last_param;
				}
			}
			if (param_decl) param_type = &param_decl->type_node().as<TypeSpecifierNode>();
			
			const CachedParamInfo* cached_param = nullptr;
			if (cached_param_list && !cached_param_list->empty()) {
				if (arg_index < cached_param_list->size()) {
					cached_param = &(*cached_param_list)[arg_index];
				} else if (cached_param_list->back().is_parameter_pack) {
					cached_param = &cached_param_list->back();
				}
			}

			bool param_is_ref_like = false;
			[[maybe_unused]] bool param_is_rvalue_ref = false;
			[[maybe_unused]] bool param_is_pack = param_decl && param_decl->is_parameter_pack();
			if (param_type) {
				param_is_ref_like = param_type->is_reference() || param_type->is_rvalue_reference();
				param_is_rvalue_ref = param_type->is_rvalue_reference();
			} else if (cached_param) {
				param_is_ref_like = cached_param->is_reference || cached_param->is_rvalue_reference;
				param_is_rvalue_ref = cached_param->is_rvalue_reference;
				param_is_pack = cached_param->is_parameter_pack;
			}
			
			// Special case: if argument is a reference identifier being passed to a reference parameter,
			// handle it directly without visiting the expression. This prevents the Load context from
			// generating a Dereference operation (which would give us the value, not the address).
			// For reference-to-reference passing, we just want to pass the variable name directly,
			// and let the IRConverter use MOV to load the address stored in the reference.
			if (param_is_ref_like &&
			    std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier.name());
				}
				if (symbol.has_value()) {
					const DeclarationNode* decl_ptr = nullptr;
					if (symbol->is<DeclarationNode>()) {
						decl_ptr = &symbol->as<DeclarationNode>();
					} else if (symbol->is<VariableDeclarationNode>()) {
						decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
					}
					if (decl_ptr) {
						const auto& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
						if (type_node.is_reference() || type_node.is_rvalue_reference()) {
							// Argument is a reference variable being passed to a reference parameter
							// Pass the identifier name directly - the IRConverter will use MOV to
							// load the address stored in the reference variable
							irOperands.emplace_back(type_node.type());
							irOperands.emplace_back(64);  // References are stored as 64-bit pointers
							irOperands.emplace_back(StringTable::getOrInternStringHandle(identifier.name()));
							arg_index++;
							return;  // Skip the rest of the processing
						}
					}
				}
			}
			
			// Determine expression context for the argument
			// Default to Load context, which reads values
			ExpressionContext arg_context = ExpressionContext::Load;
			
			// If the parameter expects a reference, use LValueAddress context to avoid dereferencing
			// This is needed for non-reference arguments being passed to reference parameters
			if (param_is_ref_like) {
				arg_context = ExpressionContext::LValueAddress;
			}
			
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>(), arg_context);
			arg_index++;
			
			// Check if we need to call a conversion operator for this argument
			// This handles cases like: func(myStruct) where func expects int and myStruct has operator int()
			if (param_type && argumentIrOperands.size() >= 3) {
				Type arg_type = std::get<Type>(argumentIrOperands[0]);
				int arg_size = std::get<int>(argumentIrOperands[1]);
				Type param_base_type = param_type->type();
				
				// Check if argument type doesn't match parameter type and parameter expects struct
				// This handles implicit conversions via converting constructors
				if (arg_type != param_base_type && param_base_type == Type::Struct && param_type->pointer_depth() == 0) {
					TypeIndex param_type_index = param_type->type_index();
					
					if (param_type_index > 0 && param_type_index < gTypeInfo.size()) {
						const TypeInfo& target_type_info = gTypeInfo[param_type_index];
						const StructTypeInfo* target_struct_info = target_type_info.getStructInfo();
						
						// Look for a converting constructor that takes the argument type
						if (target_struct_info) {
							const ConstructorDeclarationNode* converting_ctor = nullptr;
							for (const auto& func : target_struct_info->member_functions) {
								if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
									const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
									const auto& params = ctor_node.parameter_nodes();
									
									// Check for single-parameter constructor (or multi-parameter with defaults)
									if (params.size() >= 1) {
										if (params[0].is<DeclarationNode>()) {
											const auto& ctor_param_decl = params[0].as<DeclarationNode>();
											const auto& ctor_param_type = ctor_param_decl.type_node().as<TypeSpecifierNode>();
											
											// Match if types are compatible
											bool param_matches = false;
											if (ctor_param_type.type() == arg_type) {
												param_matches = true;
											}
											
											if (param_matches) {
												// Check if remaining parameters have defaults
												bool all_have_defaults = true;
												for (size_t i = 1; i < params.size(); ++i) {
													if (!params[i].is<DeclarationNode>() || 
													    !params[i].as<DeclarationNode>().has_default_value()) {
														all_have_defaults = false;
														break;
													}
												}
												
												if (all_have_defaults) {
													converting_ctor = &ctor_node;
													break;
												}
											}
										}
									}
								}
							}
							
							// If found a converting constructor and it's explicit, emit error
							if (converting_ctor && converting_ctor->is_explicit()) {
								FLASH_LOG(General, Error, "Cannot use implicit conversion with explicit constructor for type '",
									StringTable::getStringView(target_type_info.name()), "'");
								FLASH_LOG(General, Error, "  In function call at argument ", arg_index);
								FLASH_LOG(General, Error, "  Use explicit construction: ", 
									StringTable::getStringView(target_type_info.name()), "(value)");
								throw std::runtime_error("Cannot use implicit conversion with explicit constructor in function argument");
							}
						}
					}
				}
				
				// Check if argument is struct type and parameter expects different type
				if (arg_type == Type::Struct && arg_type != param_base_type && param_type->pointer_depth() == 0) {
					TypeIndex arg_type_index = 0;
					if (argumentIrOperands.size() >= 4 && std::holds_alternative<unsigned long long>(argumentIrOperands[3])) {
						arg_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(argumentIrOperands[3]));
					}
					
					if (arg_type_index > 0 && arg_type_index < gTypeInfo.size()) {
						const TypeInfo& source_type_info = gTypeInfo[arg_type_index];
						const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
						
						// Look for a conversion operator to the parameter type
						const StructMemberFunction* conv_op = findConversionOperator(
							source_struct_info, param_base_type, param_type->type_index());
						
						if (conv_op) {
							FLASH_LOG(Codegen, Debug, "Found conversion operator for function argument from ",
								StringTable::getStringView(source_type_info.name()),
								" to parameter type");
							
							// Generate call to the conversion operator
							TempVar result_var = var_counter.next();
							
							// Get the source value
							IrValue source_value = std::visit([](auto&& arg) -> IrValue {
								using T = std::decay_t<decltype(arg)>;
								if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
								              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
									return arg;
								} else {
									return 0ULL;
								}
							}, argumentIrOperands[2]);
							
							// Generate the call to conversion operator
							if (conv_op->function_decl.is<FunctionDeclarationNode>()) {
								const auto& func_decl = conv_op->function_decl.as<FunctionDeclarationNode>();
								std::string_view mangled_name;
								if (func_decl.has_mangled_name()) {
									mangled_name = func_decl.mangled_name();
								} else {
									StringHandle struct_name_handle = source_type_info.name();
									std::string_view struct_name = StringTable::getStringView(struct_name_handle);
									// Use the function's parent struct name, not the source type name,
									// because the conversion operator may be inherited from a base class
									// and we need to call the version defined in the base class.
									std::string_view operator_struct_name = func_decl.parent_struct_name();
									if (operator_struct_name.empty()) {
										operator_struct_name = struct_name;
									}
									mangled_name = generateMangledNameForCall(func_decl, operator_struct_name);
								}
								
								CallOp call_op;
								call_op.result = result_var;
								call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
								call_op.return_type = param_base_type;
								call_op.return_size_in_bits = param_type->pointer_depth() > 0 ? 64 : static_cast<int>(param_type->size_in_bits());
								call_op.return_type_index = param_type->type_index();
								call_op.is_member_function = true;
								call_op.is_variadic = false;
								
								// For member function calls, first argument is 'this' pointer
								if (std::holds_alternative<StringHandle>(source_value)) {
									// It's a variable - take its address
									TempVar this_ptr = var_counter.next();
									AddressOfOp addr_op;
									addr_op.result = this_ptr;
									addr_op.operand.type = arg_type;
									addr_op.operand.size_in_bits = arg_size;
									addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
									addr_op.operand.value = std::get<StringHandle>(source_value);
									ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
									
									// Add 'this' as first argument
									TypedValue this_arg;
									this_arg.type = arg_type;
									this_arg.size_in_bits = 64;  // Pointer size
									this_arg.value = this_ptr;
									this_arg.type_index = arg_type_index;
									call_op.args.push_back(std::move(this_arg));
								} else if (std::holds_alternative<TempVar>(source_value)) {
									// It's already a temporary
									TypedValue this_arg;
									this_arg.type = arg_type;
									this_arg.size_in_bits = 64;  // Pointer size for 'this'
									this_arg.value = std::get<TempVar>(source_value);
									this_arg.type_index = arg_type_index;
									call_op.args.push_back(std::move(this_arg));
								}
								
								ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), Token()));
								
								// Replace argumentIrOperands with the result of the conversion
								argumentIrOperands.clear();
								argumentIrOperands.emplace_back(param_base_type);
								argumentIrOperands.emplace_back(param_type->pointer_depth() > 0 ? 64 : static_cast<int>(param_type->size_in_bits()));
								argumentIrOperands.emplace_back(result_var);
							}
						}
					}
				}
			}
			
			// Check if visitExpressionNode returned a TempVar - this means the value was computed
			// (e.g., global load, expression result, etc.) and we should use the TempVar directly
			bool use_computed_result = (argumentIrOperands.size() >= 3 && 
			                            std::holds_alternative<TempVar>(argumentIrOperands[2]));
			
			// For identifiers that returned local variable references (string_view), handle specially
			if (!use_computed_result && std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier.name());
				}
				if (!symbol.has_value()) {
					FLASH_LOG(Codegen, Error, "Symbol '", identifier.name(), "' not found for function argument");
					FLASH_LOG(Codegen, Error, "  Current function: ", current_function_name_);
					throw std::runtime_error("Missing symbol for function argument");
				}

				const DeclarationNode* decl_ptr = nullptr;
				if (symbol->is<DeclarationNode>()) {
					decl_ptr = &symbol->as<DeclarationNode>();
				} else if (symbol->is<VariableDeclarationNode>()) {
					decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
				}

				if (!decl_ptr) {
					FLASH_LOG(Codegen, Error, "Function argument '", identifier.name(), "' is not a DeclarationNode");
					throw std::runtime_error("Unexpected symbol type for function argument");
				}

				const auto& decl_node = *decl_ptr;
				const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

				// Check if this is an enumerator constant (not a variable of enum type)
				// Enumerator constants should be passed as immediate values, not variable references
				if (type_node.type() == Type::Enum && !type_node.is_reference() && type_node.pointer_depth() == 0) {
					size_t enum_type_index = type_node.type_index();
					if (enum_type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[enum_type_index];
						const EnumTypeInfo* enum_info = type_info.getEnumInfo();
						if (enum_info) {
							const Enumerator* enumerator = enum_info->findEnumerator(StringTable::getOrInternStringHandle(identifier.name()));
							if (enumerator) {
								// Pass enumerator value as immediate constant
								irOperands.emplace_back(enum_info->underlying_type);
								irOperands.emplace_back(static_cast<int>(enum_info->underlying_size));
								irOperands.emplace_back(static_cast<unsigned long long>(enumerator->value));
								return;
							}
						}
					}
				}

				// Check if this is an array - arrays decay to pointers when passed to functions
				if (decl_node.is_array()) {
					// For arrays, we need to pass the address of the first element
					// Create a temporary for the address
					TempVar addr_var = var_counter.next();

					// Generate AddressOf IR instruction to get the address of the array
					AddressOfOp addr_op;
					addr_op.result = addr_var;
					addr_op.operand.type = type_node.type();
					addr_op.operand.size_in_bits = static_cast<int>(type_node.size_in_bits());
					addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
					addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
					ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));

					// Add the pointer (address) to the function call operands
					// For now, we use the element type with 64-bit size to indicate it's a pointer
					// TODO: Add proper pointer type support to the Type enum
					irOperands.emplace_back(type_node.type());  // Element type (e.g., Char for char[])
					irOperands.emplace_back(64);  // Pointer size is 64 bits on x64
					irOperands.emplace_back(addr_var);
				} else if (param_is_ref_like) {
					// Parameter expects a reference - pass the address of the argument
					if (type_node.is_reference() || type_node.is_rvalue_reference()) {
						// Argument is already a reference - just pass it through
						// References are stored as pointers (64 bits), not the pointee size
						irOperands.emplace_back(type_node.type());
						irOperands.emplace_back(64);  // Pointer size, not pointee size
						irOperands.emplace_back(StringTable::getOrInternStringHandle(identifier.name()));
					} else {
						// Argument is a value - take its address
						TempVar addr_var = var_counter.next();

						AddressOfOp addr_op;
						addr_op.result = addr_var;
						addr_op.operand.type = type_node.type();
						addr_op.operand.size_in_bits = static_cast<int>(type_node.size_in_bits());
						addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
						addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));

						// Pass the address
						irOperands.emplace_back(type_node.type());
						irOperands.emplace_back(64);  // Pointer size
						irOperands.emplace_back(addr_var);
					}
				} else if (type_node.is_reference() || type_node.is_rvalue_reference()) {
					// Argument is a reference but parameter expects a value - dereference
					TempVar deref_var = var_counter.next();

					DereferenceOp deref_op;
					deref_op.result = deref_var;
					deref_op.pointer.type = type_node.type();
					deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
					deref_op.pointer.pointer_depth = 1;  // TODO: Verify pointer depth
					deref_op.pointer.value = StringTable::getOrInternStringHandle(identifier.name());
					ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), Token()));
					
					// Pass the dereferenced value
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
					irOperands.emplace_back(deref_var);
				} else {
					// Regular variable - pass by value
					// For pointer types, size is always 64 bits regardless of pointee type
					int arg_size = (type_node.pointer_depth() > 0) ? 64 : static_cast<int>(type_node.size_in_bits());
					irOperands.emplace_back(type_node.type());
					irOperands.emplace_back(arg_size);
					irOperands.emplace_back(StringTable::getOrInternStringHandle(identifier.name()));
				}
			} else {
				// Not an identifier - could be a literal, expression result, etc.
				// Check if parameter expects a reference and argument is a literal
				if (param_is_ref_like) {
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
						TempVar addr_var = var_counter.next();
						AddressOfOp addr_op;
						addr_op.result = addr_var;
						addr_op.operand.type = literal_type;
						addr_op.operand.size_in_bits = literal_size;
						addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
						addr_op.operand.value = temp_var;
						ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
						
						// Pass the address
						irOperands.emplace_back(literal_type);
						irOperands.emplace_back(64);  // Pointer size
						irOperands.emplace_back(addr_var);
					} else {
						// Not a literal (expression result in a TempVar) - check if it needs address taken
						if (argumentIrOperands.size() >= 3 && std::holds_alternative<TempVar>(argumentIrOperands[2])) {
							Type expr_type = std::get<Type>(argumentIrOperands[0]);
							int expr_size = std::get<int>(argumentIrOperands[1]);
							TempVar expr_var = std::get<TempVar>(argumentIrOperands[2]);
							
							// Check if the TempVar already holds an address
							// This can happen when:
							// 1. It's the result of a cast to reference (xvalue/lvalue)
							// 2. It's a 64-bit struct (pointer to struct)
							// 3. It has lvalue/xvalue metadata indicating it's already an address
							bool is_already_address = false;
							
							// Check for xvalue/lvalue metadata (from reference casts)
							auto& metadata_storage = GlobalTempVarMetadataStorage::instance();
							if (metadata_storage.hasMetadata(expr_var)) {
								TempVarMetadata metadata = metadata_storage.getMetadata(expr_var);
								if (metadata.category == ValueCategory::LValue ||
								    metadata.category == ValueCategory::XValue) {
									is_already_address = true;
								}
							}
							
							// Fallback heuristic: 64-bit struct type likely holds an address
							if (!is_already_address && expr_size == 64 && expr_type == Type::Struct) {
								is_already_address = true;
							}
							
							if (is_already_address) {
								// Already an address - pass through
								irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
							} else {
								// Need to take address of the value
								TempVar addr_var = var_counter.next();
								AddressOfOp addr_op;
								addr_op.result = addr_var;
								addr_op.operand.type = expr_type;
								addr_op.operand.size_in_bits = expr_size;
								// pointer_depth is 0 because we're taking the address of a value (not a pointer)
								// The TempVar holds a direct value (e.g., constructed object), not a pointer
								addr_op.operand.pointer_depth = 0;
								addr_op.operand.value = expr_var;
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
								
								irOperands.emplace_back(expr_type);
								irOperands.emplace_back(64);  // Pointer size
								irOperands.emplace_back(addr_var);
							}
						} else {
							// Fallback - just pass through
							irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
						}
					}
				} else {
					// Parameter doesn't expect a reference - pass through as-is
					irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
				}
			}
		});

		// Create CallOp structure
		CallOp call_op;
		call_op.result = ret_var;
		call_op.function_name = StringTable::getOrInternStringHandle(function_name);
		
		// Check if this is an indirect call (function pointer/reference)
		call_op.is_indirect_call = functionCallNode.is_indirect_call();
		
		// Get return type information
		// Prefer the matched function declaration's return type over the original call's,
		// since template instantiation may have resolved dependent types (e.g., Tp* → int*)
		// But DON'T use it if the return type is still unresolved (UserDefined = template param)
		const TypeSpecifierNode* best_return_type = nullptr;
		if (matched_func_decl) {
			const auto& mrt = matched_func_decl->decl_node().type_node().as<TypeSpecifierNode>();
			if (mrt.type() != Type::UserDefined) {
				best_return_type = &mrt;
			}
		}
		if (!best_return_type) {
			best_return_type = &decl_node.type_node().as<TypeSpecifierNode>();
		}
		const auto& return_type = *best_return_type;
		call_op.return_type = return_type.type();
		// For pointers and references, use 64-bit size (pointer size on x64)
		// References are represented as addresses at the IR level
		call_op.return_size_in_bits = (return_type.pointer_depth() > 0 || return_type.is_reference()) 
			? 64 
			: static_cast<int>(return_type.size_in_bits());
		call_op.return_type_index = return_type.type_index();
		call_op.is_member_function = false;
		call_op.returns_rvalue_reference = return_type.is_rvalue_reference();
		
		// Detect if calling a function that returns struct by value (needs hidden return parameter for RVO)
		// Exclude references - they return a pointer, not a struct by value
		bool returns_struct = returnsStructByValue(return_type.type(), return_type.pointer_depth(), return_type.is_reference());
		bool needs_hidden_ret = needsHiddenReturnParam(return_type.type(), return_type.pointer_depth(), return_type.is_reference(), return_type.size_in_bits(), context_->isLLP64());
		if (needs_hidden_ret) {
			call_op.return_slot = ret_var;  // The result temp var serves as the return slot
			
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Function call {} returns struct by value (size={} bits) - using return slot (temp_{})",
				function_name, return_type.size_in_bits(), ret_var.var_number);
		} else if (returns_struct) {
			FLASH_LOG_FORMAT(Codegen, Debug,
				"Function call {} returns small struct by value (size={} bits) - will return in RAX",
				function_name, return_type.size_in_bits());
		}
		
		// Set is_variadic based on function declaration (if available)
		if (matched_func_decl) {
			call_op.is_variadic = matched_func_decl->is_variadic();
		}
		
		// Convert operands to TypedValue arguments (skip first 2: result and function_name)
		// Operands come in groups of 3 (type, size, value) or 4 (type, size, value, type_index)
		// toTypedValue handles both cases
		size_t arg_idx = 0;
		for (size_t i = 2; i < irOperands.size(); ) {
			// Peek ahead to determine operand group size
			// If there are at least 4 more operands and the 4th is an integer, assume it's type_index
			size_t group_size = 3;
			if (i + 3 < irOperands.size() && std::holds_alternative<unsigned long long>(irOperands[i + 3])) {
				// Check if this looks like a type_index by seeing if i+4 would be start of next group
				// or end of operands
				bool next_is_type = (i + 4 >= irOperands.size() || std::holds_alternative<Type>(irOperands[i + 4]));
				if (next_is_type) {
					group_size = 4;
				}
			}
			
			TypedValue arg = toTypedValue(std::span<const IrOperand>(&irOperands[i], group_size));
			
			// Check if this parameter is a reference type
			ReferenceQualifier arg_ref_qual = ReferenceQualifier::None;
			if (matched_func_decl && arg_idx < param_nodes.size() && param_nodes[arg_idx].is<DeclarationNode>()) {
				const TypeSpecifierNode& param_type = param_nodes[arg_idx].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
				if (param_type.is_rvalue_reference()) {
					arg_ref_qual = ReferenceQualifier::RValueReference;
				} else if (param_type.is_reference()) {
					arg_ref_qual = ReferenceQualifier::LValueReference;
				}
			} else if (cached_param_list && !cached_param_list->empty()) {
				if (arg_idx < cached_param_list->size()) {
					const auto& cached = (*cached_param_list)[arg_idx];
					if (cached.is_rvalue_reference) {
						arg_ref_qual = ReferenceQualifier::RValueReference;
					} else if (cached.is_reference) {
						arg_ref_qual = ReferenceQualifier::LValueReference;
					}
				} else if (cached_param_list->back().is_parameter_pack) {
					const auto& cached = cached_param_list->back();
					if (cached.is_rvalue_reference) {
						arg_ref_qual = ReferenceQualifier::RValueReference;
					} else if (cached.is_reference) {
						arg_ref_qual = ReferenceQualifier::LValueReference;
					}
				}
			}
			if (arg_ref_qual != ReferenceQualifier::None) {
				arg.ref_qualifier = arg_ref_qual;
			}
			
			call_op.args.push_back(arg);
			i += group_size;
			arg_idx++;
		}

		// Add the function call instruction with typed payload
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), functionCallNode.called_from()));

		// For functions returning rvalue references, mark the result as an xvalue
		// This prevents taking the address of the result when passing to another function
		if (return_type.is_rvalue_reference()) {
			// Create lvalue info indicating this is a Direct value (the function returns the address directly)
			LValueInfo lvalue_info(LValueInfo::Kind::Direct, ret_var, 0);
			setTempVarMetadata(ret_var, TempVarMetadata::makeXValue(lvalue_info, return_type.type(), static_cast<int>(return_type.size_in_bits())));
		}

		// Return the result variable with its type and size
		// For references, return 64-bit size (address size)
		int result_size = (return_type.pointer_depth() > 0 || return_type.is_reference())
			? 64
			: static_cast<int>(return_type.size_in_bits());
		// Return type_index for struct types so structured bindings can decompose the result
		unsigned long long type_index_result = (return_type.type() == Type::Struct) 
			? static_cast<unsigned long long>(return_type.type_index())
			: 0ULL;
		return { return_type.type(), result_size, ret_var, type_index_result };
	}

	std::vector<IrOperand> generateMemberFunctionCallIr(const MemberFunctionCallNode& memberFunctionCallNode) {
		std::vector<IrOperand> irOperands;

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
								if (param_type.is_rvalue_reference()) {
									deduced_type.set_reference(true);  // rvalue reference (&&)
								} else if (param_type.is_reference()) {
									deduced_type.set_reference(false);  // lvalue reference (&)
								}
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
								object_type.set_reference(true);
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
									member_load.is_reference = member.is_reference;
									member_load.is_rvalue_reference = member.is_rvalue_reference;
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
							member_load.is_reference = member.is_reference;
							member_load.is_rvalue_reference = member.is_rvalue_reference;
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
													type_node.set_reference(true);
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
									if (param_type.is_rvalue_reference()) {
										deduced_type.set_reference(true);  // rvalue reference (&&)
									} else if (param_type.is_reference()) {
										deduced_type.set_reference(false);  // lvalue reference (&)
									}
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
								TempVar addr_var = var_counter.next();
						
								AddressOfOp addr_op;
								addr_op.result = addr_var;
								addr_op.operand.type = type_node.type();
								addr_op.operand.size_in_bits = static_cast<int>(type_node.size_in_bits());
								addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
								addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
						
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
								TempVar addr_var = var_counter.next();
						
								AddressOfOp addr_op;
								addr_op.result = addr_var;
								addr_op.operand.type = type_node.type();
								addr_op.operand.size_in_bits = static_cast<int>(type_node.size_in_bits());
								addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
								addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
						
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
							TempVar addr_var = var_counter.next();
							AddressOfOp addr_op;
							addr_op.result = addr_var;
							addr_op.operand.type = literal_type;
							addr_op.operand.size_in_bits = literal_size;
							addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
							addr_op.operand.value = temp_var;
							ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
							
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
								
								TempVar addr_var = var_counter.next();
								AddressOfOp addr_op;
								addr_op.result = addr_var;
								addr_op.operand.type = expr_type;
								addr_op.operand.size_in_bits = expr_size;
								addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
								addr_op.operand.value = expr_var;
								ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
								
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

	// Helper struct for multidimensional array access
	struct MultiDimArrayAccess {
		std::string_view base_array_name;
		std::vector<ASTNode> indices;  // Indices from outermost to innermost
		const DeclarationNode* base_decl = nullptr;
		bool is_valid = false;
	};

	// Helper struct for multidimensional member array access  
	struct MultiDimMemberArrayAccess {
		std::string_view object_name;
		std::string_view member_name;
		std::vector<ASTNode> indices;  // Indices from outermost to innermost
		const StructMember* member_info = nullptr;
		bool is_valid = false;
	};

	// Helper function to collect all indices from a chain of ArraySubscriptNodes for member arrays
	// For obj.arr[i][j][k], returns {object="obj", member="arr", indices=[i, j, k]}
	MultiDimMemberArrayAccess collectMultiDimMemberArrayIndices(const ArraySubscriptNode& subscript) {
		MultiDimMemberArrayAccess result;
		std::vector<ASTNode> indices_reversed;
		const ExpressionNode* current = &subscript.array_expr().as<ExpressionNode>();
		
		// Collect the outermost index first
		indices_reversed.push_back(subscript.index_expr());
		
		// Walk down the chain of ArraySubscriptNodes
		while (std::holds_alternative<ArraySubscriptNode>(*current)) {
			const ArraySubscriptNode& inner = std::get<ArraySubscriptNode>(*current);
			indices_reversed.push_back(inner.index_expr());
			current = &inner.array_expr().as<ExpressionNode>();
		}
		
		FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: Collected {} indices", indices_reversed.size());
		
		// The base should be a member access (obj.member)
		if (std::holds_alternative<MemberAccessNode>(*current)) {
			const MemberAccessNode& base_member = std::get<MemberAccessNode>(*current);
			result.member_name = base_member.member_name();
			FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: Found MemberAccessNode, member_name={}", 
				std::string(result.member_name));
			
			// Get the object
			if (base_member.object().is<ExpressionNode>()) {
				const ExpressionNode& obj_expr = base_member.object().as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(obj_expr)) {
					const IdentifierNode& object_ident = std::get<IdentifierNode>(obj_expr);
					result.object_name = object_ident.name();
					FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: object_name={}", std::string(result.object_name));
					
					// Look up the object to get struct type
					std::optional<ASTNode> symbol = symbol_table.lookup(result.object_name);
					FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: symbol.has_value()={}", symbol.has_value());
					if (symbol.has_value()) {
						FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: symbol->is<DeclarationNode>()={}", symbol->is<DeclarationNode>());
						FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: symbol->is<VariableDeclarationNode>()={}", symbol->is<VariableDeclarationNode>());
					}
					// Try both DeclarationNode and VariableDeclarationNode
					const DeclarationNode* decl_node = nullptr;
					if (symbol.has_value()) {
						if (symbol->is<DeclarationNode>()) {
							decl_node = &symbol->as<DeclarationNode>();
						} else if (symbol->is<VariableDeclarationNode>()) {
							decl_node = &symbol->as<VariableDeclarationNode>().declaration();
						}
					}
					
					if (decl_node) {
						const auto& type_node = decl_node->type_node().as<TypeSpecifierNode>();
						
						FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: Found decl, is_struct={}, type_index={}", 
							is_struct_type(type_node.type()), type_node.type_index());
						
						if (is_struct_type(type_node.type()) && type_node.type_index() < gTypeInfo.size()) {
							TypeIndex type_index = type_node.type_index();
							auto member_result = FlashCpp::gLazyMemberResolver.resolve(
								type_index,
								StringTable::getOrInternStringHandle(std::string(result.member_name)));
							
							FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: gLazyMemberResolver.resolve returned {}", static_cast<bool>(member_result));
							
							if (member_result) {
								const StructMember* member = member_result.member;
								result.member_info = member;
								
								FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: member->is_array={}, array_dimensions.size()={}", 
									member->is_array, member->array_dimensions.size());
								
								// Reverse the indices so they're in order from outermost to innermost
								result.indices.reserve(indices_reversed.size());
								for (auto it = indices_reversed.rbegin(); it != indices_reversed.rend(); ++it) {
									result.indices.push_back(*it);
								}
								
								// Valid if member is a multidimensional array with matching indices
								result.is_valid = member->is_array && 
								                  !member->array_dimensions.empty() &&
								                  (member->array_dimensions.size() == result.indices.size()) &&
								                  (result.indices.size() > 1);
								
								FLASH_LOG_FORMAT(Codegen, Debug, "collectMultiDim: is_valid={} (is_array={}, dim_size={}, indices_size={}, indices>1={})",
									result.is_valid, member->is_array, member->array_dimensions.size(), 
									result.indices.size(), (result.indices.size() > 1));
							}
						}
					}
				}
			}
		}
		
		return result;
	}

	// Helper function to collect all indices from a chain of ArraySubscriptNodes
	// For arr[i][j][k], returns {base="arr", indices=[i, j, k]}
	MultiDimArrayAccess collectMultiDimArrayIndices(const ArraySubscriptNode& subscript) {
		MultiDimArrayAccess result;
		std::vector<ASTNode> indices_reversed;
		const ExpressionNode* current = &subscript.array_expr().as<ExpressionNode>();
		
		// Collect the outermost index first (the one in the current subscript)
		indices_reversed.push_back(subscript.index_expr());
		
		// Walk down the chain of ArraySubscriptNodes
		while (std::holds_alternative<ArraySubscriptNode>(*current)) {
			const ArraySubscriptNode& inner = std::get<ArraySubscriptNode>(*current);
			indices_reversed.push_back(inner.index_expr());
			current = &inner.array_expr().as<ExpressionNode>();
		}
		
		// The base should be an identifier
		if (std::holds_alternative<IdentifierNode>(*current)) {
			const IdentifierNode& base_ident = std::get<IdentifierNode>(*current);
			result.base_array_name = base_ident.name();
			
			// Look up the declaration
			std::optional<ASTNode> symbol = symbol_table.lookup(result.base_array_name);
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(result.base_array_name);
			}
			if (symbol.has_value()) {
				if (symbol->is<DeclarationNode>()) {
					result.base_decl = &symbol->as<DeclarationNode>();
				} else if (symbol->is<VariableDeclarationNode>()) {
					result.base_decl = &symbol->as<VariableDeclarationNode>().declaration();
				}
			}
			
			// Reverse the indices so they're in order from outermost to innermost
			// For arr[i][j], we collected [j, i], now reverse to [i, j]
			result.indices.reserve(indices_reversed.size());
			for (auto it = indices_reversed.rbegin(); it != indices_reversed.rend(); ++it) {
				result.indices.push_back(*it);
			}
			
			result.is_valid = (result.base_decl != nullptr) && 
			                  (result.base_decl->array_dimension_count() == result.indices.size()) &&
			                  (result.indices.size() > 1);  // Only valid for multidimensional
		}
		
		return result;
	}

	std::vector<IrOperand> generateArraySubscriptIr(const ArraySubscriptNode& arraySubscriptNode,
	                                                 ExpressionContext context = ExpressionContext::Load) {
		// Generate IR for array[index] expression
		// This computes the address: base_address + (index * element_size)

		// Check for multidimensional array access pattern (arr[i][j])
		// If the array expression is itself an ArraySubscriptNode, we have a multidimensional access
		const ExpressionNode& array_expr = arraySubscriptNode.array_expr().as<ExpressionNode>();
		FLASH_LOG_FORMAT(Codegen, Debug, "generateArraySubscriptIr: array_expr is ArraySubscriptNode = {}",
			std::holds_alternative<ArraySubscriptNode>(array_expr));
		if (std::holds_alternative<ArraySubscriptNode>(array_expr)) {
			// First check if this is a multidimensional member array access (obj.arr[i][j])
			auto member_multi_dim = collectMultiDimMemberArrayIndices(arraySubscriptNode);
			FLASH_LOG_FORMAT(Codegen, Debug, "Member multidim check: is_valid={}", member_multi_dim.is_valid);
			
			if (member_multi_dim.is_valid && member_multi_dim.member_info) {
				FLASH_LOG(Codegen, Debug, "Flattening multidimensional member array access!");
				// We have a valid multidimensional member array access
				// For obj.arr[M][N] accessed as obj.arr[i][j], compute flat_index = i*N + j
				
				const StructMember* member = member_multi_dim.member_info;
				Type element_type = member->type;
				int base_element_size = get_type_size_bits(element_type);
				
				// Get all dimension sizes
				const std::vector<size_t>& dim_sizes = member->array_dimensions;
				
				// Compute strides: stride[k] = product of dimensions after k
				std::vector<size_t> strides(dim_sizes.size());
				strides.back() = 1;
				for (int k = static_cast<int>(dim_sizes.size()) - 2; k >= 0; --k) {
					strides[k] = strides[k + 1] * dim_sizes[k + 1];
				}
				
				// Generate code to compute flat index
				auto idx0_operands = visitExpressionNode(member_multi_dim.indices[0].as<ExpressionNode>());
				TempVar flat_index = var_counter.next();
				
				if (strides[0] == 1) {
					BinaryOp add_op;
					add_op.lhs = toTypedValue(idx0_operands);
					add_op.rhs = TypedValue{Type::Int, 32, 0ULL};
					add_op.result = IrValue{flat_index};
					ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
				} else {
					BinaryOp mul_op;
					mul_op.lhs = toTypedValue(idx0_operands);
					mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[0])};
					mul_op.result = IrValue{flat_index};
					ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
				}
				
				// Add remaining indices
				for (size_t k = 1; k < member_multi_dim.indices.size(); ++k) {
					auto idx_operands = visitExpressionNode(member_multi_dim.indices[k].as<ExpressionNode>());
					
					if (strides[k] == 1) {
						TempVar new_flat = var_counter.next();
						BinaryOp add_op;
						add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
						add_op.rhs = toTypedValue(idx_operands);
						add_op.result = IrValue{new_flat};
						ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
						flat_index = new_flat;
					} else {
						TempVar temp_prod = var_counter.next();
						BinaryOp mul_op;
						mul_op.lhs = toTypedValue(idx_operands);
						mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[k])};
						mul_op.result = IrValue{temp_prod};
						ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
						
						TempVar new_flat = var_counter.next();
						BinaryOp add_op;
						add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
						add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, temp_prod};
						add_op.result = IrValue{new_flat};
						ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
						flat_index = new_flat;
					}
				}
				
				// Generate single array access with flat index
				TempVar result_var = var_counter.next();
				StringHandle qualified_name = StringTable::getOrInternStringHandle(
					StringBuilder().append(member_multi_dim.object_name).append(".").append(member_multi_dim.member_name));
				
				LValueInfo lvalue_info(
					LValueInfo::Kind::ArrayElement,
					qualified_name,
					static_cast<int64_t>(member->offset)
				);
				lvalue_info.array_index = IrValue{flat_index};
				lvalue_info.is_pointer_to_array = false;
				setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));
				
				ArrayAccessOp payload;
				payload.result = result_var;
				payload.element_type = element_type;
				payload.element_size_in_bits = base_element_size;
				payload.array = qualified_name;
				payload.member_offset = static_cast<int64_t>(member->offset);
				payload.is_pointer_to_array = false;
				payload.index.type = Type::UnsignedLongLong;
				payload.index.size_in_bits = 64;
				payload.index.value = flat_index;
				
				if (context == ExpressionContext::LValueAddress) {
					return { element_type, base_element_size, result_var, 0ULL };
				}
				
				ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));
				return { element_type, base_element_size, result_var, 0ULL };
			}
			
			// This could be a multidimensional array access
			auto multi_dim = collectMultiDimArrayIndices(arraySubscriptNode);
			
			if (multi_dim.is_valid && multi_dim.base_decl) {
				// We have a valid multidimensional array access
				// For arr[M][N][P] accessed as arr[i][j][k], compute flat_index = i*N*P + j*P + k
				
				const auto& type_node = multi_dim.base_decl->type_node().as<TypeSpecifierNode>();
				Type element_type = type_node.type();
				int element_size_bits = static_cast<int>(type_node.size_in_bits());
				size_t element_type_index = (element_type == Type::Struct) ? type_node.type_index() : 0;
				
				// Get element size for struct types
				if (element_size_bits == 0 && element_type == Type::Struct && element_type_index > 0) {
					const TypeInfo& type_info = gTypeInfo[element_type_index];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						element_size_bits = static_cast<int>(struct_info->total_size * 8);
					}
				}
				
				// Get all dimension sizes
				std::vector<size_t> dim_sizes;
				const auto& dims = multi_dim.base_decl->array_dimensions();
				for (const auto& dim_expr : dims) {
					ConstExpr::EvaluationContext ctx(symbol_table);
					auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
					if (eval_result.success() && eval_result.as_int() > 0) {
						dim_sizes.push_back(static_cast<size_t>(eval_result.as_int()));
					} else {
						// Can't evaluate dimension at compile time, fall back to regular handling
						break;
					}
				}
				
				if (dim_sizes.size() == multi_dim.indices.size()) {
					// All dimensions evaluated successfully, compute flat index
					// For arr[D0][D1][D2] accessed as arr[i0][i1][i2]:
					// flat_index = i0 * (D1*D2) + i1 * D2 + i2
					
					// First, compute strides: stride[k] = product of dimensions after k
					std::vector<size_t> strides(dim_sizes.size());
					strides.back() = 1;
					for (int k = static_cast<int>(dim_sizes.size()) - 2; k >= 0; --k) {
						strides[k] = strides[k + 1] * dim_sizes[k + 1];
					}
					
					// Generate code to compute flat index
					// Start with the first index times its stride
					auto idx0_operands = visitExpressionNode(multi_dim.indices[0].as<ExpressionNode>());
					TempVar flat_index = var_counter.next();
					
					if (strides[0] == 1) {
						// Simple case: stride is 1, just copy the index
						// Use Add with 0 to effectively copy
						BinaryOp add_op;
						add_op.lhs = toTypedValue(idx0_operands);
						add_op.rhs = TypedValue{Type::Int, 32, 0ULL};
						add_op.result = IrValue{flat_index};
						ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
					} else {
						// flat_index = indices[0] * strides[0]
						BinaryOp mul_op;
						mul_op.lhs = toTypedValue(idx0_operands);
						mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[0])};
						mul_op.result = IrValue{flat_index};
						ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
					}
					
					// Add remaining indices: flat_index += indices[k] * strides[k]
					for (size_t k = 1; k < multi_dim.indices.size(); ++k) {
						auto idx_operands = visitExpressionNode(multi_dim.indices[k].as<ExpressionNode>());
						
						if (strides[k] == 1) {
							// flat_index += indices[k]
							TempVar new_flat = var_counter.next();
							BinaryOp add_op;
							add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
							add_op.rhs = toTypedValue(idx_operands);
							add_op.result = IrValue{new_flat};
							ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
							flat_index = new_flat;
						} else {
							// temp = indices[k] * strides[k]
							TempVar temp_prod = var_counter.next();
							BinaryOp mul_op;
							mul_op.lhs = toTypedValue(idx_operands);
							mul_op.rhs = TypedValue{Type::UnsignedLongLong, 64, static_cast<unsigned long long>(strides[k])};
							mul_op.result = IrValue{temp_prod};
							ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(mul_op), Token()));
							
							// flat_index += temp
							TempVar new_flat = var_counter.next();
							BinaryOp add_op;
							add_op.lhs = TypedValue{Type::UnsignedLongLong, 64, flat_index};
							add_op.rhs = TypedValue{Type::UnsignedLongLong, 64, temp_prod};
							add_op.result = IrValue{new_flat};
							ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(add_op), Token()));
							flat_index = new_flat;
						}
					}
					
					// Now generate the array access using the flat index
					TempVar result_var = var_counter.next();
					
					// Mark array element access as lvalue using metadata system
					LValueInfo lvalue_info(
						LValueInfo::Kind::ArrayElement,
						StringTable::getOrInternStringHandle(multi_dim.base_array_name),
						0  // offset computed dynamically by index
					);
					lvalue_info.array_index = IrValue{flat_index};
					lvalue_info.is_pointer_to_array = false;  // This is a real array, not a pointer
					setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));
					
					// Create ArrayAccessOp with the flat index
					ArrayAccessOp payload;
					payload.result = result_var;
					payload.element_type = element_type;
					payload.element_size_in_bits = element_size_bits;
					payload.member_offset = 0;
					payload.is_pointer_to_array = false;
					payload.array = StringTable::getOrInternStringHandle(multi_dim.base_array_name);
					payload.index.type = Type::UnsignedLongLong;
					payload.index.size_in_bits = 64;
					payload.index.value = flat_index;
					
					if (context == ExpressionContext::LValueAddress) {
						// Don't emit ArrayAccess instruction (no load)
						return { element_type, element_size_bits, result_var, static_cast<unsigned long long>(element_type_index) };
					}
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));
					
					return { element_type, element_size_bits, result_var, static_cast<unsigned long long>(element_type_index) };
				}
			}
		}

		// Check if the array expression is a member access (e.g., obj.array[index])
		if (std::holds_alternative<MemberAccessNode>(array_expr)) {
			const MemberAccessNode& member_access = std::get<MemberAccessNode>(array_expr);
			const ASTNode& object_node = member_access.object();
			std::string_view member_name = member_access.member_name();

			// Handle simple case: obj.array[index]
			if (object_node.is<ExpressionNode>()) {
				const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(obj_expr)) {
					const IdentifierNode& object_ident = std::get<IdentifierNode>(obj_expr);
					std::string_view object_name = object_ident.name();

					// Look up the object to get struct type
					const std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
					if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();

						if (is_struct_type(type_node.type())) {
							TypeIndex struct_type_index = type_node.type_index();
							if (struct_type_index < gTypeInfo.size()) {
								auto member_result = FlashCpp::gLazyMemberResolver.resolve(
									struct_type_index,
									StringTable::getOrInternStringHandle(std::string(member_name)));
								
								if (member_result) {
									const StructMember* member = member_result.member;
									// Get index expression
									auto index_operands = visitExpressionNode(arraySubscriptNode.index_expr().as<ExpressionNode>());

									// Get element type and size from the member
									Type element_type = member->type;
									int element_size_bits = static_cast<int>(member->size * 8);
									
									// For array members, member->size is the total size, we need element size
									// This is a simplified assumption - we need better array type info
									// For now, assume arrays of primitives and compute element size
									// TODO: Get actual array length from type info
									// For now, use a heuristic: if size is larger than element type, it's an array
									int base_element_size = get_type_size_bits(element_type);  // Use existing helper
									
									if (base_element_size > 0 && element_size_bits > base_element_size) {
										// It's an array
										element_size_bits = base_element_size;
									}

									// Create a temporary variable for the result
									TempVar result_var = var_counter.next();
									
									// Mark array element access as lvalue (Option 2: Value Category Tracking)
									StringHandle qualified_name = StringTable::getOrInternStringHandle(
										StringBuilder().append(object_name).append(".").append(member_name));
									LValueInfo lvalue_info(
										LValueInfo::Kind::ArrayElement,
										qualified_name,
										static_cast<int64_t>(member_result.adjusted_offset)  // member offset in struct
									);
									// Store index information for unified assignment handler
									lvalue_info.array_index = toIrValue(index_operands[2]);
									lvalue_info.is_pointer_to_array = false;  // Member arrays are actual arrays, not pointers
									setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

									// Create typed payload for ArrayAccess with qualified member name
									ArrayAccessOp payload;
									payload.result = result_var;
									payload.element_type = element_type;
									payload.element_size_in_bits = element_size_bits;
									payload.array = StringTable::getOrInternStringHandle(StringBuilder().append(object_name).append(".").append(member_name));
									payload.member_offset = static_cast<int64_t>(member_result.adjusted_offset);
									payload.is_pointer_to_array = false;  // Member arrays are actual arrays, not pointers
									
									// Set index as TypedValue
									payload.index.type = std::get<Type>(index_operands[0]);
									payload.index.size_in_bits = std::get<int>(index_operands[1]);
									if (std::holds_alternative<unsigned long long>(index_operands[2])) {
										payload.index.value = std::get<unsigned long long>(index_operands[2]);
									} else if (std::holds_alternative<TempVar>(index_operands[2])) {
										payload.index.value = std::get<TempVar>(index_operands[2]);
									} else if (std::holds_alternative<StringHandle>(index_operands[2])) {
										payload.index.value = std::get<StringHandle>(index_operands[2]);
									}

									// When context is LValueAddress, skip the load and return address/metadata only
									if (context == ExpressionContext::LValueAddress) {
										// Don't emit ArrayAccess instruction (no load)
										// Just return the metadata with the result temp var
										return { element_type, element_size_bits, result_var, 0ULL };
									}

									// Create instruction with typed payload (Load context - default)
									ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));

									// Return the result with the element type
									return { element_type, element_size_bits, result_var, 0ULL };
								}
							}
						}
					}
				}
			}
		}

		// Fall back to default handling for regular arrays
		// Get the array expression (should be an identifier for now)
		auto array_operands = visitExpressionNode(arraySubscriptNode.array_expr().as<ExpressionNode>());

		// Get the index expression
		auto index_operands = visitExpressionNode(arraySubscriptNode.index_expr().as<ExpressionNode>());

		// Get array type information
		Type element_type = std::get<Type>(array_operands[0]);
		int element_size_bits = std::get<int>(array_operands[1]);

		// Check if this is a pointer type (e.g., int* arr)
		// If so, we need to get the base type size, not the pointer size (64)
		// Look up the identifier to get the actual type info
		bool is_pointer_to_array = false;
		size_t element_type_index = 0;  // Track type_index for struct elements
		int element_pointer_depth = 0;  // Track pointer depth for pointer array elements
		const ExpressionNode& arr_expr = arraySubscriptNode.array_expr().as<ExpressionNode>();
		if (std::holds_alternative<IdentifierNode>(arr_expr)) {
			const IdentifierNode& arr_ident = std::get<IdentifierNode>(arr_expr);
			std::optional<ASTNode> symbol = symbol_table.lookup(arr_ident.name());
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(arr_ident.name());
			}
			if (symbol.has_value()) {
				const DeclarationNode* decl_ptr = nullptr;
				if (symbol->is<DeclarationNode>()) {
					decl_ptr = &symbol->as<DeclarationNode>();
				} else if (symbol->is<VariableDeclarationNode>()) {
					decl_ptr = &symbol->as<VariableDeclarationNode>().declaration();
				}
				
				if (decl_ptr) {
					const auto& type_node = decl_ptr->type_node().as<TypeSpecifierNode>();
					
					// Capture type_index for struct types (important for member access on array elements)
					if (type_node.type() == Type::Struct) {
						element_type_index = type_node.type_index();
					}
					
					// For array types, ALWAYS get the element size from type_node, not from array_operands
					// array_operands[1] contains 64 (pointer size) for arrays, not the element size
					if (decl_ptr->is_array() || type_node.is_array()) {
						// Check if this is an array of pointers (e.g., int* ptrs[3])
						// In this case, the element size should be the pointer size (64 bits), not the base type size
						if (type_node.pointer_depth() > 0) {
							// Array of pointers: element size is always 64 bits (pointer size)
							element_size_bits = 64;
							// Track pointer depth for the array element (e.g., for int* arr[3], element has pointer_depth=1)
							element_pointer_depth = type_node.pointer_depth();
						} else {
							// Get the element size from type_node
							element_size_bits = static_cast<int>(type_node.size_in_bits());
							// If still 0, compute from type info for struct types
							if (element_size_bits == 0 && type_node.type() == Type::Struct && element_type_index > 0) {
								const TypeInfo& type_info = gTypeInfo[element_type_index];
								const StructTypeInfo* struct_info = type_info.getStructInfo();
								if (struct_info) {
									element_size_bits = static_cast<int>(struct_info->total_size * 8);
								}
							}
						}
					}
					// For array parameters with explicit size (e.g., reference-to-array params),
					// we need pointer indirection
					// NOTE: Local arrays with explicit size (e.g., int arr[3]) are NOT pointers
					// EXCEPTION: Reference-to-array parameters (e.g., int (&arr)[3]) ARE pointers
					if (type_node.is_array() && decl_ptr->array_size().has_value()) {
						// Check if this is a reference to an array (parameter)
						// References to arrays need pointer indirection
						if (type_node.is_reference() || type_node.is_rvalue_reference()) {
							is_pointer_to_array = true;
						}
						// Local arrays with explicit size are NOT pointers (they're actual arrays on stack)
						// We don't set is_pointer_to_array for non-reference arrays
					}
					// For pointer types or reference types (not arrays), get the pointee size
					// BUT: Skip this if we already handled an array of pointers above (decl_ptr->is_array() case)
					else if (!decl_ptr->is_array() && (type_node.pointer_depth() > 0 || type_node.is_reference() || type_node.is_rvalue_reference())) {
						// Get the base type size (what the pointer points to)
						element_size_bits = static_cast<int>(type_node.size_in_bits());
						is_pointer_to_array = true;  // This is a pointer or reference, not an actual array
					}
				}
			}
		}
		
		// Fix element size for array members accessed through TempVar (e.g., vls.values[i])
		// When array comes from member_access, element_size_bits is the TOTAL array size (e.g., 640 bits for int[20])
		// We need to derive the actual element size from the element type
		if (std::holds_alternative<TempVar>(array_operands[2]) && !is_pointer_to_array) {
			// Check if element_size_bits is much larger than expected for element_type
			int base_element_size = get_type_size_bits(element_type);
			if (base_element_size > 0 && element_size_bits > base_element_size) {
				// This is likely an array where we got the total size instead of element size
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Array subscript on TempVar: fixing element_size from {} bits (total) to {} bits (element)",
					element_size_bits, base_element_size);
				element_size_bits = base_element_size;
			}
		}

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();
		
		// If the array expression resolved to a TempVar that actually refers to a member,
		// recover the qualified name and offset from its lvalue metadata so we don't lose
		// struct/offset information (important for member arrays).
		std::variant<StringHandle, TempVar> base_variant;
		int base_member_offset = 0;
		bool base_is_pointer_to_member = false;
		// Fast-path: if the array expression is a member access, rebuild qualified name directly
		if (std::holds_alternative<MemberAccessNode>(array_expr)) {
			const auto& member_access = std::get<MemberAccessNode>(array_expr);
			if (member_access.object().is<ExpressionNode>()) {
				const ExpressionNode& obj_expr = member_access.object().as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(obj_expr)) {
					const auto& object_ident = std::get<IdentifierNode>(obj_expr);
					std::string_view object_name = object_ident.name();
					auto symbol = symbol_table.lookup(object_name);
					if (symbol.has_value() && symbol->is<DeclarationNode>()) {
						const auto& decl_node = symbol->as<DeclarationNode>();
						const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
						if (is_struct_type(type_node.type()) && type_node.type_index() < gTypeInfo.size()) {
							auto member_result = FlashCpp::gLazyMemberResolver.resolve(
								type_node.type_index(),
								StringTable::getOrInternStringHandle(std::string(member_access.member_name())));
							if (member_result) {
								base_variant = StringTable::getOrInternStringHandle(
									StringBuilder().append(object_name).append(".").append(member_access.member_name()));
								base_member_offset = static_cast<int>(member_result.adjusted_offset);
								// Member access via '.' is not a pointer access for locals
							}
						}
					}
				}
			}
			// If object isn't a simple identifier (e.g., arr[i].member), fall back to using the
			// computed operands to keep a valid base (TempVar or StringHandle) instead of
			// leaving an empty StringHandle that leads to invalid offsets.
			if (base_variant.valueless_by_exception()) {
				if (std::holds_alternative<TempVar>(array_operands[2])) {
					base_variant = std::get<TempVar>(array_operands[2]);
				} else if (std::holds_alternative<StringHandle>(array_operands[2])) {
					base_variant = std::get<StringHandle>(array_operands[2]);
				}
			}
		}
		// Simple identifier array (non-member)
		else if (std::holds_alternative<IdentifierNode>(array_expr)) {
			const auto& ident = std::get<IdentifierNode>(array_expr);
			base_variant = StringTable::getOrInternStringHandle(ident.name());
		}
		if (std::holds_alternative<TempVar>(array_operands[2])) {
			TempVar base_temp = std::get<TempVar>(array_operands[2]);
			if (auto base_lv = getTempVarLValueInfo(base_temp)) {
				if (base_lv->kind == LValueInfo::Kind::Member && base_lv->member_name.has_value()) {
					// Build qualified name: object.member
					if (std::holds_alternative<StringHandle>(base_lv->base)) {
						auto obj_name = std::get<StringHandle>(base_lv->base);
						base_variant = StringTable::getOrInternStringHandle(
							StringBuilder().append(StringTable::getStringView(obj_name))
							                .append(".")
							                .append(StringTable::getStringView(base_lv->member_name.value())));
						base_member_offset = base_lv->offset;
						base_is_pointer_to_member = base_lv->is_pointer_to_member;
					}
				}
			}
		}
		if (!std::holds_alternative<StringHandle>(base_variant)) {
			if (std::holds_alternative<StringHandle>(array_operands[2])) {
				base_variant = std::get<StringHandle>(array_operands[2]);
			}
		}
		// Prefer keeping TempVar base when available to preserve stack offsets for nested accesses
		if (!std::holds_alternative<TempVar>(base_variant) && std::holds_alternative<TempVar>(array_operands[2])) {
			base_variant = std::get<TempVar>(array_operands[2]);
		}
		
		// Mark array element access as lvalue (Option 2: Value Category Tracking)
		// arr[i] is an lvalue - it designates an object with a stable address
		LValueInfo lvalue_info(
			LValueInfo::Kind::ArrayElement,
			base_variant,
			base_member_offset  // offset for member arrays (otherwise 0)
		);
		// Store index information for unified assignment handler
		// Support both constant and variable indices
		lvalue_info.array_index = toIrValue(index_operands[2]);
		FLASH_LOG(Codegen, Debug, "Array index stored in metadata (supports constants and variables)");
		lvalue_info.is_pointer_to_array = is_pointer_to_array || base_is_pointer_to_member;
		setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

		// Create typed payload for ArrayAccess
		ArrayAccessOp payload;
		payload.result = result_var;
		payload.element_type = element_type;
		payload.element_size_in_bits = element_size_bits;
		payload.member_offset = 0;  // Not a member array
		payload.is_pointer_to_array = is_pointer_to_array;
		
		// Set array (either variable name or temp)
		if (std::holds_alternative<StringHandle>(array_operands[2])) {
			payload.array = std::get<StringHandle>(array_operands[2]);
		} else if (std::holds_alternative<TempVar>(array_operands[2])) {
			payload.array = std::get<TempVar>(array_operands[2]);
		}
		
		// Set index as TypedValue
		Type index_type = std::get<Type>(index_operands[0]);
		int index_size = std::get<int>(index_operands[1]);
		payload.index.type = index_type;
		payload.index.size_in_bits = index_size;
		
		if (std::holds_alternative<unsigned long long>(index_operands[2])) {
			payload.index.value = std::get<unsigned long long>(index_operands[2]);
		} else if (std::holds_alternative<TempVar>(index_operands[2])) {
			payload.index.value = std::get<TempVar>(index_operands[2]);
		} else if (std::holds_alternative<StringHandle>(index_operands[2])) {
			payload.index.value = std::get<StringHandle>(index_operands[2]);
		}

		// When context is LValueAddress, skip the load and return address/metadata only
		if (context == ExpressionContext::LValueAddress) {
			// Don't emit ArrayAccess instruction (no load)
			// Just return the metadata with the result temp var
			// The metadata contains all information needed for store operations
			// For the 4th element: 
			// - For struct types, return type_index
			// - For pointer array elements, return pointer_depth
			// - Otherwise return 0
			unsigned long long fourth_element = (element_type == Type::Struct)
				? static_cast<unsigned long long>(element_type_index)
				: ((element_pointer_depth > 0) ? static_cast<unsigned long long>(element_pointer_depth) : 0ULL);
			return { element_type, element_size_bits, result_var, fourth_element };
		}

		// Create instruction with typed payload (Load context - default)
		ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(payload), arraySubscriptNode.bracket_token()));

		// Return [element_type, element_size_bits, result_var, struct_type_index or pointer_depth]
		// For the 4th element: 
		// - For struct types, return type_index
		// - For pointer array elements, return pointer_depth
		// - Otherwise return 0
		unsigned long long fourth_element = (element_type == Type::Struct)
			? static_cast<unsigned long long>(element_type_index)
			: ((element_pointer_depth > 0) ? static_cast<unsigned long long>(element_pointer_depth) : 0ULL);
		return { element_type, element_size_bits, result_var, fourth_element };
	}

	// Helper function to validate and setup identifier-based member access
	// Returns true on success, false on error
	bool validateAndSetupIdentifierMemberAccess(
		std::string_view object_name,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		bool& is_pointer_dereference) {
		
		// Look up the object in the symbol table (local first, then global)
		std::optional<ASTNode> symbol = symbol_table.lookup(object_name);
		
		// If not found locally, try global symbol table (for global struct variables)
		if (!symbol.has_value() && global_symbol_table_) {
			symbol = global_symbol_table_->lookup(object_name);
		}
		
		// If not found in symbol tables, check if it's a type name (for static member access like ClassName::member)
		if (!symbol.has_value()) {
			FLASH_LOG(Codegen, Debug, "validateAndSetupIdentifierMemberAccess: object_name='", object_name, "' not in symbol table, checking gTypesByName");
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(object_name));
			if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
				// This is a type name - set up for static member access
				FLASH_LOG(Codegen, Debug, "Found type '", object_name, "' in gTypesByName with type_index=", type_it->second->type_index_);
				base_object = StringTable::getOrInternStringHandle(object_name);
				base_type = Type::Struct;
				base_type_index = type_it->second->type_index_;
				is_pointer_dereference = false;  // Type names don't need dereferencing
				return true;
			}
			
			FLASH_LOG(Codegen, Error, "object '", object_name, "' not found in symbol table or type registry");
			return false;
		}

		// Use helper to get DeclarationNode from either DeclarationNode or VariableDeclarationNode
		const DeclarationNode* object_decl_ptr = get_decl_from_symbol(*symbol);
		if (!object_decl_ptr) {
			FLASH_LOG(Codegen, Error, "object '", object_name, "' is not a declaration");
			return false;
		}
		const DeclarationNode& object_decl = *object_decl_ptr;
		const TypeSpecifierNode& object_type = object_decl.type_node().as<TypeSpecifierNode>();

		// Verify this is a struct type (or a pointer/reference to a struct type)
		// References and pointers are automatically dereferenced for member access
		// Note: Type can be either Struct or UserDefined (for user-defined types like Point)
		// For pointers, the type might be Void with pointer_depth > 0 and type_index pointing to struct
		bool is_valid_for_member_access = is_struct_type(object_type.type()) || 
		                                  (object_type.pointer_depth() > 0 && object_type.type_index() > 0);
		if (!is_valid_for_member_access) {
			FLASH_LOG(Codegen, Error, "member access '.' on non-struct type '", object_name, "'");
			return false;
		}

		base_object = StringTable::getOrInternStringHandle(object_name);
		base_type = object_type.type();
		base_type_index = object_type.type_index();
		
		// Check if this is a pointer to struct (e.g., P* pp) or a reference to struct (e.g., P& pr)
		// In this case, member access like pp->member or pr.member should be treated as pointer dereference
		// References are implemented as pointers internally, so they need the same treatment
		if (object_type.pointer_depth() > 0 || object_type.is_reference() || object_type.is_rvalue_reference()) {
			is_pointer_dereference = true;
		}
		
		return true;
	}

	// Helper: extract base_type, base_object, and base_type_index from IR operands [type, size_bits, value, type_index?]
	bool extractBaseFromOperands(
		const std::vector<IrOperand>& operands,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		std::string_view error_context) {

		if (operands.size() < 3) {
			FLASH_LOG(Codegen, Error, "Failed to evaluate ", error_context, " for member access");
			return false;
		}
		base_type = std::get<Type>(operands[0]);
		if (std::holds_alternative<TempVar>(operands[2])) {
			base_object = std::get<TempVar>(operands[2]);
		} else if (std::holds_alternative<StringHandle>(operands[2])) {
			base_object = std::get<StringHandle>(operands[2]);
		} else {
			FLASH_LOG(Codegen, Error, error_context, " result has unsupported value type");
			return false;
		}
		if (operands.size() >= 4 && std::holds_alternative<unsigned long long>(operands[3])) {
			base_type_index = static_cast<size_t>(std::get<unsigned long long>(operands[3]));
		}
		return true;
	}

	// Helper: build return vector for member access results — [type, size_bits, temp_var] or [type, size_bits, temp_var, type_index]
	// type_index is only included in the result when type == Type::Struct (ignored otherwise)
	static std::vector<IrOperand> makeMemberResult(Type type, int size_bits, TempVar result_var, size_t type_index = 0) {
		if (type == Type::Struct) {
			return { type, size_bits, result_var, static_cast<unsigned long long>(type_index) };
		}
		return { type, size_bits, result_var };
	}

	// Helper: set up base object from an identifier, handling 'this' in lambdas and normal identifiers
	bool setupBaseFromIdentifier(
		std::string_view object_name,
		const Token& member_token,
		std::variant<StringHandle, TempVar>& base_object,
		Type& base_type,
		size_t& base_type_index,
		bool& is_pointer_dereference) {

		if (object_name == "this") {
			// First try [*this] capture - returns copy of the object
			if (auto copy_this_temp = emitLoadCopyThis(member_token)) {
				base_object = *copy_this_temp;
				base_type = Type::Struct;
				base_type_index = current_lambda_context_.enclosing_struct_type_index;
				return true;
			}
			// Then try [this] capture - returns pointer to the object
			if (auto this_ptr_temp = emitLoadThisPointer(member_token)) {
				base_object = *this_ptr_temp;
				base_type = Type::Struct;
				base_type_index = current_lambda_context_.enclosing_struct_type_index;
				is_pointer_dereference = true;
				return true;
			}
		}
		return validateAndSetupIdentifierMemberAccess(object_name, base_object, base_type, base_type_index, is_pointer_dereference);
	}

	std::vector<IrOperand> generateMemberAccessIr(const MemberAccessNode& memberAccessNode,
	                                               ExpressionContext context = ExpressionContext::Load) {
		std::vector<IrOperand> irOperands;

		// Get the object being accessed
		ASTNode object_node = memberAccessNode.object();
		std::string_view member_name = memberAccessNode.member_name();
		bool is_arrow = memberAccessNode.is_arrow();

		// Variables to hold the base object info
		std::variant<StringHandle, TempVar> base_object;
		Type base_type = Type::Void;
		size_t base_type_index = 0;
		bool is_pointer_dereference = false;  // Track if we're accessing through pointer (ptr->member)
		bool base_setup_complete = false;

		// Normalize: unwrap ExpressionNode to get the concrete variant pointer for unified dispatch
		const ExpressionNode* expr = object_node.is<ExpressionNode>() ? &object_node.as<ExpressionNode>() : nullptr;

		// Helper lambdas to check node types across both ExpressionNode variant and top-level ASTNode
		auto get_identifier = [&]() -> const IdentifierNode* {
			if (expr && std::holds_alternative<IdentifierNode>(*expr)) return &std::get<IdentifierNode>(*expr);
			if (object_node.is<IdentifierNode>()) return &object_node.as<IdentifierNode>();
			return nullptr;
		};
		auto get_member_func_call = [&]() -> const MemberFunctionCallNode* {
			if (expr && std::holds_alternative<MemberFunctionCallNode>(*expr)) return &std::get<MemberFunctionCallNode>(*expr);
			if (object_node.is<MemberFunctionCallNode>()) return &object_node.as<MemberFunctionCallNode>();
			return nullptr;
		};

		// OPERATOR-> OVERLOAD RESOLUTION
		// If this is arrow access (obj->member), check if the object has operator->() overload
		if (const IdentifierNode* ident = is_arrow ? get_identifier() : nullptr) {
			StringHandle identifier_handle = StringTable::getOrInternStringHandle(ident->name());
			
			std::optional<ASTNode> symbol = symbol_table.lookup(identifier_handle);
			if (!symbol.has_value() && global_symbol_table_) {
				symbol = global_symbol_table_->lookup(identifier_handle);
			}
			
			if (symbol.has_value()) {
				const TypeSpecifierNode* type_node = nullptr;
				if (symbol->is<DeclarationNode>()) {
					type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
				} else if (symbol->is<VariableDeclarationNode>()) {
					type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
				}
				
				// Check if it's a struct with operator-> overload
				if (type_node && type_node->type() == Type::Struct && type_node->pointer_depth() == 0) {
					auto overload_result = findUnaryOperatorOverload(type_node->type_index(), "->");
					
					if (overload_result.has_overload) {
						// Found an overload! Call operator->() to get pointer, then access member
						FLASH_LOG_FORMAT(Codegen, Debug, "Resolving operator-> overload for type index {}", 
						         type_node->type_index());
						
						const StructMemberFunction& member_func = *overload_result.member_overload;
						const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
						
						// Get struct name for mangling
						std::string_view struct_name = StringTable::getStringView(gTypeInfo[type_node->type_index()].name());
						
						// Get the return type from the function declaration (should be a pointer)
						const TypeSpecifierNode& return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
						
						// Generate mangled name for operator->
						std::string_view operator_func_name = "operator->";
						std::vector<TypeSpecifierNode> empty_params;
						std::vector<std::string_view> empty_namespace;
						auto mangled_name = NameMangling::generateMangledName(
							operator_func_name,
							return_type,
							empty_params,
							false,
							struct_name,
							empty_namespace,
							Linkage::CPlusPlus
						);
						
						// Generate the call to operator->()
						TempVar ptr_result = var_counter.next();
						
						CallOp call_op;
						call_op.result = ptr_result;
						call_op.return_type = return_type.type();
						call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
						if (call_op.return_size_in_bits == 0) {
							call_op.return_size_in_bits = get_type_size_bits(return_type.type());
						}
						call_op.function_name = mangled_name;
						call_op.is_variadic = false;
						call_op.is_member_function = true;
						
						// Add 'this' pointer as first argument
						call_op.args.push_back(TypedValue{
							.type = type_node->type(),
							.size_in_bits = 64,  // Pointer size
							.value = IrValue(identifier_handle)
						});
						
						// Add the function call instruction
						ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), memberAccessNode.member_token()));
						
						// operator-> should return a pointer, so we treat ptr_result as pointing to the actual object
						if (return_type.pointer_depth() > 0) {
							base_object = ptr_result;
							base_type = return_type.type();
							base_type_index = return_type.type_index();
							is_pointer_dereference = true;
							base_setup_complete = true;
						}
					}
				}
			}
		}

		// Resolve the base object — single dispatch chain regardless of ExpressionNode wrapping
		if (!base_setup_complete) {
			if (const IdentifierNode* ident = get_identifier()) {
				if (!setupBaseFromIdentifier(ident->name(), memberAccessNode.member_token(),
				                             base_object, base_type, base_type_index, is_pointer_dereference)) {
					return {};
				}
			}
			else if (const MemberFunctionCallNode* call = get_member_func_call()) {
				auto call_result = generateMemberFunctionCallIr(*call);
				if (!extractBaseFromOperands(call_result, base_object, base_type, base_type_index, "member function call")) {
					return {};
				}
				if (is_arrow) {
					is_pointer_dereference = true;
				}
			}
			else if (expr && std::holds_alternative<MemberAccessNode>(*expr)) {
				auto nested_result = generateMemberAccessIr(std::get<MemberAccessNode>(*expr), context);
				if (!extractBaseFromOperands(nested_result, base_object, base_type, base_type_index, "nested member access")) {
					return {};
				}
				if (base_type != Type::Struct) {
					FLASH_LOG(Codegen, Error, "nested member access on non-struct type");
					return {};
				}
				if (is_arrow) {
					is_pointer_dereference = true;
				}
			}
			else if (expr && std::holds_alternative<UnaryOperatorNode>(*expr)) {
				const UnaryOperatorNode& unary_op = std::get<UnaryOperatorNode>(*expr);

				if (unary_op.op() != "*") {
					FLASH_LOG(Codegen, Error, "member access on non-dereference unary operator");
					return {};
				}

				const ASTNode& operand_node = unary_op.get_operand();
				if (!operand_node.is<ExpressionNode>()) {
					FLASH_LOG(Codegen, Error, "dereference operand is not an expression");
					return {};
				}
				const ExpressionNode& operand_expr = operand_node.as<ExpressionNode>();
				
				// Special handling for 'this' in lambdas with [this] or [*this] capture
				bool is_lambda_this = false;
				if (std::holds_alternative<IdentifierNode>(operand_expr)) {
					const IdentifierNode& ptr_ident = std::get<IdentifierNode>(operand_expr);
					std::string_view ptr_name = ptr_ident.name();
					
					if (ptr_name == "this" && current_lambda_context_.isActive() && 
					    current_lambda_context_.captures.find(StringTable::getOrInternStringHandle("this"sv)) != current_lambda_context_.captures.end()) {
						is_lambda_this = true;
						auto capture_kind_it = current_lambda_context_.capture_kinds.find(StringTable::getOrInternStringHandle("this"sv));
						if (capture_kind_it != current_lambda_context_.capture_kinds.end() && 
						    capture_kind_it->second == LambdaCaptureNode::CaptureKind::CopyThis) {
							// [*this] capture: load from the copied object in __copy_this
							const StructTypeInfo* closure_struct = getCurrentClosureStruct();
							const StructMember* copy_this_member = closure_struct ? closure_struct->findMember("__copy_this") : nullptr;
							int copy_this_offset = copy_this_member ? static_cast<int>(copy_this_member->offset) : 0;
							int copy_this_size_bits = copy_this_member ? static_cast<int>(copy_this_member->size * 8) : 64;
							
							TempVar copy_this_ref = var_counter.next();
							MemberLoadOp load_copy_this;
							load_copy_this.result.value = copy_this_ref;
							load_copy_this.result.type = Type::Struct;
							load_copy_this.result.size_in_bits = copy_this_size_bits;
							load_copy_this.object = StringTable::getOrInternStringHandle("this"sv);
							load_copy_this.member_name = StringTable::getOrInternStringHandle("__copy_this");
							load_copy_this.offset = copy_this_offset;
							load_copy_this.is_reference = false;
							load_copy_this.is_rvalue_reference = false;
							load_copy_this.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_copy_this), memberAccessNode.member_token()));
							
							LValueInfo lvalue_info(
								LValueInfo::Kind::Member,
								StringTable::getOrInternStringHandle("this"sv),
								copy_this_offset
							);
							lvalue_info.member_name = StringTable::getOrInternStringHandle("__copy_this");
							lvalue_info.is_pointer_to_member = true;
							setTempVarMetadata(copy_this_ref, TempVarMetadata::makeLValue(lvalue_info));
							
							base_object = copy_this_ref;
							base_type = Type::Struct;
							base_type_index = current_lambda_context_.enclosing_struct_type_index;
						} else {
							// [this] capture: load the pointer from __this
							int this_member_offset = getClosureMemberOffset("__this");
							
							TempVar this_ptr = var_counter.next();
							MemberLoadOp load_this;
							load_this.result.value = this_ptr;
							load_this.result.type = Type::Void;
							load_this.result.size_in_bits = 64;
							load_this.object = StringTable::getOrInternStringHandle("this"sv);
							load_this.member_name = StringTable::getOrInternStringHandle("__this");
							load_this.offset = this_member_offset;
							load_this.is_reference = false;
							load_this.is_rvalue_reference = false;
							load_this.struct_type_info = nullptr;
							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_this), memberAccessNode.member_token()));
							
							base_object = this_ptr;
							base_type = Type::Struct;
							base_type_index = current_lambda_context_.enclosing_struct_type_index;
						}
					}
				}
				
				if (!is_lambda_this) {
					auto pointer_operands = visitExpressionNode(operand_expr);
					if (!extractBaseFromOperands(pointer_operands, base_object, base_type, base_type_index, "pointer expression")) {
						return {};
					}
					is_pointer_dereference = true;
				}
			}
			else if (expr && std::holds_alternative<ArraySubscriptNode>(*expr)) {
				auto array_operands = generateArraySubscriptIr(std::get<ArraySubscriptNode>(*expr));
				if (!extractBaseFromOperands(array_operands, base_object, base_type, base_type_index, "array subscript")) {
					return {};
				}
			}
			else if (expr && std::holds_alternative<FunctionCallNode>(*expr)) {
				auto call_result = generateFunctionCallIr(std::get<FunctionCallNode>(*expr));
				if (!extractBaseFromOperands(call_result, base_object, base_type, base_type_index, "function call")) {
					return {};
				}
				if (is_arrow) {
					is_pointer_dereference = true;
				}
			}
			else {
				FLASH_LOG(Codegen, Error, "member access on unsupported object type");
				return {};
			}
		}

		// Now we have the base object (either a name or a temp var) and its type
		// Get the struct type info
		const TypeInfo* type_info = nullptr;

		// Try to find by direct index lookup
		if (base_type_index < gTypeInfo.size()) {
			const TypeInfo& ti = gTypeInfo[base_type_index];
			if (ti.type_ == Type::Struct && ti.getStructInfo()) {
				type_info = &ti;
			}
		}

		// If not found by index, search through all type info entries
		// This handles cases where type_index might not be set correctly
		if (!type_info) {
			for (const auto& ti : gTypeInfo) {
				if (ti.type_index_ == base_type_index && ti.type_ == Type::Struct && ti.getStructInfo()) {
					type_info = &ti;
					break;
				}
			}
		}

		if (!type_info || !type_info->getStructInfo()) {
			std::cerr << "Error: Struct type info not found for type_index=" << base_type_index << "\n";
			if (std::holds_alternative<StringHandle>(base_object)) {
				std::cerr << "  Object name: " << std::get<StringHandle>(base_object) << "\n";
			}
			std::cerr << "  Available struct types in gTypeInfo:\n";
			for (const auto& ti : gTypeInfo) {
				if (ti.type_ == Type::Struct && ti.getStructInfo()) {
					std::cerr << "    - " << ti.name() << " (type_index=" << ti.type_index_ << ")\n";
				}
			}
			std::cerr << "  Available types in gTypesByName:\n";
			for (const auto& [name, ti] : gTypesByName) {
				if (ti->type_ == Type::Struct) {
					std::cerr << "    - " << name << " (type_index=" << ti->type_index_ << ")\n";
				}
			}
			std::cerr << "error: struct type info not found\n";
			return {};
		}

		const StructTypeInfo* struct_info = type_info->getStructInfo();
		
		// FIRST check if this is a static member (can be accessed via instance in C++)
		auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(StringTable::getOrInternStringHandle(member_name));
		if (static_member) {
			// This is a static member! Access it via GlobalLoad instead of MemberLoad
			// Static members are accessed using qualified names (OwnerClassName::memberName)
			// Use the owner_struct name, not the current struct, to get the correct qualified name
			StringBuilder qualified_name_sb;
			qualified_name_sb.append(StringTable::getStringView(owner_struct->getName()));
			qualified_name_sb.append("::"sv);
			qualified_name_sb.append(member_name);
			std::string_view qualified_name = qualified_name_sb.commit();
			
			FLASH_LOG(Codegen, Debug, "Static member access: ", member_name, " in struct ", type_info->name(), " owned by ", owner_struct->getName(), " -> qualified_name: ", qualified_name);
			
			// Create a temporary variable for the result
			TempVar result_var = var_counter.next();
			
			int sm_size_bits = static_cast<int>(static_member->size * 8);
			// If size is 0 for struct types, look up from type info
			if (sm_size_bits == 0 && static_member->type_index > 0 && static_member->type_index < gTypeInfo.size()) {
				const StructTypeInfo* sm_si = gTypeInfo[static_member->type_index].getStructInfo();
				if (sm_si) {
					sm_size_bits = static_cast<int>(sm_si->total_size * 8);
				}
			}
			
			// Build GlobalLoadOp for the static member
			GlobalLoadOp global_load;
			global_load.result.value = result_var;
			global_load.result.type = static_member->type;
			global_load.result.size_in_bits = sm_size_bits;
			global_load.global_name = StringTable::getOrInternStringHandle(qualified_name);
			
			ir_.addInstruction(IrInstruction(IrOpcode::GlobalLoad, std::move(global_load), Token()));
			
			return makeMemberResult(static_member->type, sm_size_bits, result_var, static_member->type_index);
		}
		
		// Use recursive lookup to find instance members in base classes as well
		auto member_result = FlashCpp::gLazyMemberResolver.resolve(base_type_index, StringTable::getOrInternStringHandle(member_name));

		if (!member_result) {
			std::cerr << "error: member '" << member_name << "' not found in struct '" << type_info->name() << "'\n";
			std::cerr << "  available members:\n";
			for (const auto& m : struct_info->members) {
				std::cerr << "    - " << StringTable::getStringView(m.getName()) << "\n";
			}
			throw std::runtime_error("Member not found in struct");
		}
		
		const StructMember* member = member_result.member;

		// Check access control
		const StructTypeInfo* current_context = getCurrentStructContext();
		std::string_view current_function = getCurrentFunctionName();
		if (!checkMemberAccess(member, struct_info, current_context, nullptr, current_function)) {
			std::cerr << "Error: Cannot access ";
			if (member->access == AccessSpecifier::Private) {
				std::cerr << "private";
			} else if (member->access == AccessSpecifier::Protected) {
				std::cerr << "protected";
			}
			std::cerr << " member '" << member_name << "' of '" << StringTable::getStringView(struct_info->getName()) << "'";
			if (current_context) {
				std::cerr << " from '" << StringTable::getStringView(current_context->getName()) << "'";
			}
			std::cerr << "\n";
			throw std::runtime_error("Access control violation");
		}

		// Check if base_object is a TempVar with lvalue metadata
		// If so, we can unwrap it to get the ultimate base and combine offsets
		// This optimization is ONLY applied in LValueAddress context (for stores)
		// In Load context, we keep the chain of member_access instructions
		int accumulated_offset = static_cast<int>(member_result.adjusted_offset);
		std::variant<StringHandle, TempVar> ultimate_base = base_object;
		StringHandle ultimate_member_name = StringTable::getOrInternStringHandle(member_name);
		bool did_unwrap = false;
		
		if (context == ExpressionContext::LValueAddress && std::holds_alternative<TempVar>(base_object)) {
			TempVar base_temp = std::get<TempVar>(base_object);
			auto base_lvalue_info = getTempVarLValueInfo(base_temp);
			
			if (base_lvalue_info.has_value() && base_lvalue_info->kind == LValueInfo::Kind::Member) {
				// The base is itself a member access
				// Combine the offsets and use the ultimate base (LValueAddress context only)
				accumulated_offset += base_lvalue_info->offset;
				ultimate_base = base_lvalue_info->base;
				is_pointer_dereference = base_lvalue_info->is_pointer_to_member;
				// When unwrapping nested member access, use the first-level member name
				// For example: obj.inner.value -> use "inner" (member of obj), not "value"
				if (base_lvalue_info->member_name.has_value()) {
					ultimate_member_name = base_lvalue_info->member_name.value();
				}
				did_unwrap = true;
			}
		}

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();
		
		// Mark member access as lvalue (Option 2: Value Category Tracking)
		// obj.member is an lvalue - it designates a specific object member
		// Use adjusted_offset from member_result to handle inheritance correctly
		LValueInfo lvalue_info(
			LValueInfo::Kind::Member,
			did_unwrap ? ultimate_base : base_object,
			did_unwrap ? accumulated_offset : static_cast<int>(member_result.adjusted_offset)
		);
		// Store member name for unified assignment handler
		lvalue_info.member_name = ultimate_member_name;
		lvalue_info.is_pointer_to_member = is_pointer_dereference;  // Mark if accessing through pointer
		lvalue_info.bitfield_width = member->bitfield_width;
		lvalue_info.bitfield_bit_offset = member->bitfield_bit_offset;
		setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info));

		// Build MemberLoadOp
		MemberLoadOp member_load;
		member_load.result.value = result_var;
		member_load.result.type = member->type;
		member_load.result.size_in_bits = static_cast<int>(member->size * 8);  // Convert bytes to bits

		// Set base object, member name, and offset — using unwrapped values when applicable
		auto& effective_base = did_unwrap ? ultimate_base : base_object;
		std::visit([&](auto& base_value) { member_load.object = base_value; }, effective_base);
		member_load.member_name = did_unwrap ? ultimate_member_name : StringTable::getOrInternStringHandle(member_name);
		member_load.offset = did_unwrap ? accumulated_offset : static_cast<int>(member_result.adjusted_offset);
	
		// Add reference metadata (required for proper handling of reference members)
		member_load.is_reference = member->is_reference;
		member_load.is_rvalue_reference = member->is_rvalue_reference;
		member_load.struct_type_info = nullptr;
		member_load.is_pointer_to_member = is_pointer_dereference;  // Mark if accessing through pointer
		member_load.bitfield_width = member->bitfield_width;
		member_load.bitfield_bit_offset = member->bitfield_bit_offset;

		int member_size_bits = static_cast<int>(member->size * 8);

		// When context is LValueAddress, skip the load and return address/metadata only
		// EXCEPTION: For reference members, we must emit MemberAccess to load the stored address
		// because references store a pointer value that needs to be returned
		if (context == ExpressionContext::LValueAddress && !member->is_reference) {
			return makeMemberResult(member->type, member_size_bits, result_var, member->type_index);
		}

		// Add the member access instruction (Load context - default)
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), Token()));

		// For reference members in LValueAddress context, the result_var now holds the
		// pointer value loaded from the member slot. Update the LValueInfo to be Kind::Indirect
		// so that assignment goes THROUGH the pointer (dereference store), not to the member slot.
		if (context == ExpressionContext::LValueAddress && member->is_reference) {
			LValueInfo ref_lvalue_info(
				LValueInfo::Kind::Indirect,
				result_var,  // The TempVar holding the loaded pointer
				0            // No offset - the pointer points directly to the target
			);
			setTempVarMetadata(result_var, TempVarMetadata::makeLValue(ref_lvalue_info));
		}

		return makeMemberResult(member->type, member_size_bits, result_var, member->type_index);
	}

	// Helper function to calculate array size from a DeclarationNode
	// Returns the total size in bytes, or 0 if the array size cannot be determined
	std::optional<size_t> calculateArraySize(const DeclarationNode& decl) {
		if (!decl.is_array()) {
			return std::nullopt;
		}

		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
		size_t element_size = type_spec.size_in_bits() / 8;
		
		// For struct types, get size from gTypeInfo instead of size_in_bits()
		if (element_size == 0 && type_spec.type() == Type::Struct) {
			size_t type_index = type_spec.type_index();
			if (type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					element_size = struct_info->total_size;
				}
			}
		}
		
		if (element_size == 0) {
			return std::nullopt;
		}
		
		// Get array size - support multidimensional arrays
		const auto& dims = decl.array_dimensions();
		if (dims.empty()) {
			return std::nullopt;
		}

		// Evaluate all dimension size expressions and compute total element count
		size_t array_count = 1;
		ConstExpr::EvaluationContext ctx(symbol_table);
		
		for (const auto& dim_expr : dims) {
			auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
			if (!eval_result.success()) {
				return std::nullopt;
			}
			
			long long dim_size = eval_result.as_int();
			if (dim_size <= 0) {
				return std::nullopt;
			}
			
			// Check for potential overflow in multiplication
			size_t dim_size_u = static_cast<size_t>(dim_size);
			if (array_count > SIZE_MAX / dim_size_u) {
				FLASH_LOG(Codegen, Warning, "Array dimension count calculation would overflow");
				return std::nullopt;
			}
			array_count *= dim_size_u;
		}
		
		// Check for potential overflow in multiplication with element size
		if (array_count > SIZE_MAX / element_size) {
			FLASH_LOG(Codegen, Warning, "Array size calculation would overflow: ", array_count, " * ", element_size);
			return std::nullopt;
		}
		
		return element_size * array_count;
	}

	std::vector<IrOperand> generateSizeofIr(const SizeofExprNode& sizeofNode) {
		size_t size_in_bytes = 0;

		if (sizeofNode.is_type()) {
			// sizeof(type)
			const ASTNode& type_node = sizeofNode.type_or_expr();
			if (!type_node.is<TypeSpecifierNode>()) {
				throw std::runtime_error("sizeof type argument must be TypeSpecifierNode");
				return {};
			}

			const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
			Type type = type_spec.type();

			// Workaround for parser limitation: when sizeof(arr) is parsed where arr is an
			// array variable, the parser may incorrectly parse it as a type.
			// If size_in_bits is 0, try looking up the identifier in the symbol table.
			if (type_spec.size_in_bits() == 0 && type_spec.token().type() == Token::Type::Identifier) {
				std::string_view identifier = type_spec.token().value();
				
				// Look up the identifier in the symbol table
				std::optional<ASTNode> symbol = symbol_table.lookup(identifier);
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(identifier);
				}
				
				if (symbol.has_value()) {
					const DeclarationNode* decl = get_decl_from_symbol(*symbol);
					if (decl) {
						auto array_size = calculateArraySize(*decl);
						if (array_size.has_value()) {
							// Return sizeof result for array
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(*array_size) };
						}
					}
				}
				
				// Handle template parameters in member functions with trailing requires clauses
				// When sizeof(T) is used in a template class member function, T is a template parameter
				// that should be resolved from the instantiated class's template arguments
				if (!symbol.has_value() && current_struct_name_.isValid()) {
					// We're in a member function - try to resolve the template parameter
					std::string_view struct_name = StringTable::getStringView(current_struct_name_);
					size_t param_size_bytes = resolveTemplateSizeFromStructName(struct_name);
					
					if (param_size_bytes > 0) {
						return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(param_size_bytes) };
					}
				}
			}

			// Handle array types: sizeof(int[10]) 
			if (type_spec.is_array()) {
				size_t element_size = type_spec.size_in_bits() / 8;
				size_t array_count = 0;
				
				if (type_spec.array_size().has_value()) {
					array_count = *type_spec.array_size();
				}
				
				if (array_count > 0) {
					size_in_bytes = element_size * array_count;
				} else {
					size_in_bytes = element_size; // Fallback: just element size
				}
			}
			// Handle struct types
			else if (type == Type::Struct) {
				size_t type_index = type_spec.type_index();
				if (type_index >= gTypeInfo.size()) {
					throw std::runtime_error("Invalid type index for struct");
					return {};
				}

				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (!struct_info) {
					throw std::runtime_error("Struct type info not found");
					return {};
				}

				size_in_bytes = struct_info->total_size;
			}
			else {
				// For primitive types, convert bits to bytes
				size_in_bytes = type_spec.size_in_bits() / 8;
			}
		}
		else {
			// sizeof(expression) - evaluate the type of the expression
			const ASTNode& expr_node = sizeofNode.type_or_expr();
			if (!expr_node.is<ExpressionNode>()) {
				throw std::runtime_error("sizeof expression argument must be ExpressionNode");
				return {};
			}

			// Special handling for identifiers: sizeof(x) where x is a variable
			// This path handles cases where the parser correctly identifies x as an expression
			const ExpressionNode& expr = expr_node.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
				
				// Look up the identifier in the symbol table
				std::optional<ASTNode> symbol = symbol_table.lookup(id_node.name());
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(id_node.name());
				}
				
				if (symbol.has_value()) {
					const DeclarationNode* decl = get_decl_from_symbol(*symbol);
					if (decl) {
						// Check if it's an array
						auto array_size = calculateArraySize(*decl);
						if (array_size.has_value()) {
							// Return sizeof result for array
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(*array_size) };
						}
						
						// For regular variables, get the type size from the declaration
						const TypeSpecifierNode& var_type = decl->type_node().as<TypeSpecifierNode>();
						if (var_type.type() == Type::Struct) {
							size_t type_index = var_type.type_index();
							if (type_index < gTypeInfo.size()) {
								const TypeInfo& type_info = gTypeInfo[type_index];
								const StructTypeInfo* struct_info = type_info.getStructInfo();
								if (struct_info && struct_info->total_size > 0) {
									return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(struct_info->total_size) };
								}
								// Fallback: use type_size_ from TypeInfo (works for template instantiations at global scope)
								if (type_info.type_size_ > 0) {
									return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(type_info.type_size_) };
								}
							}
							// Fallback: use size_in_bits from the type specifier node
							if (var_type.size_in_bits() > 0) {
								return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(var_type.size_in_bits() / 8) };
							}
						} else {
							// Primitive type - use get_type_size_bits to handle cases where size_in_bits wasn't set
							int size_bits = var_type.size_in_bits();
							if (size_bits == 0) {
								size_bits = get_type_size_bits(var_type.type());
							}
							size_in_bytes = size_bits / 8;
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(size_in_bytes) };
						}
					}
				}
			}
			// Special handling for member access: sizeof(s.member) where member is an array
			else if (std::holds_alternative<MemberAccessNode>(expr)) {
				const MemberAccessNode& member_access = std::get<MemberAccessNode>(expr);
				std::string_view member_name = member_access.member_name();
				FLASH_LOG(Codegen, Debug, "sizeof(member_access): member_name=", member_name);
				
				// Get the object's type to find the struct info
				const ASTNode& object_node = member_access.object();
				if (object_node.is<ExpressionNode>()) {
					const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(obj_expr)) {
						const IdentifierNode& id_node = std::get<IdentifierNode>(obj_expr);
						FLASH_LOG(Codegen, Debug, "sizeof(member_access): object_name=", id_node.name());
						
						// Look up the identifier to get its type
						std::optional<ASTNode> symbol = symbol_table.lookup(id_node.name());
						if (!symbol.has_value() && global_symbol_table_) {
							symbol = global_symbol_table_->lookup(id_node.name());
						}
						
						if (symbol.has_value()) {
							const DeclarationNode* decl = get_decl_from_symbol(*symbol);
							if (decl) {
								const TypeSpecifierNode& obj_type = decl->type_node().as<TypeSpecifierNode>();
								FLASH_LOG(Codegen, Debug, "sizeof(member_access): obj_type=", (int)obj_type.type(), " type_index=", obj_type.type_index());
								if (obj_type.type() == Type::Struct) {
									size_t type_index = obj_type.type_index();
									if (type_index < gTypeInfo.size()) {
										const TypeInfo& type_info = gTypeInfo[type_index];
										std::string_view base_type_name = StringTable::getStringView(type_info.name());
										FLASH_LOG(Codegen, Debug, "sizeof(member_access): type_info name=", base_type_name);
										const StructTypeInfo* struct_info = type_info.getStructInfo();
										
										// First try the direct struct_info
										size_t direct_member_size = 0;
										if (struct_info && !struct_info->members.empty()) {
											FLASH_LOG(Codegen, Debug, "sizeof(member_access): struct found, members=", struct_info->members.size());
											// Find the member in the struct
											for (const auto& member : struct_info->members) {
												FLASH_LOG(Codegen, Debug, "  checking member: ", StringTable::getStringView(member.getName()), " size=", member.size);
												if (StringTable::getStringView(member.getName()) == member_name) {
													direct_member_size = member.size;
													break;
												}
											}
										}
										
										// If direct lookup found a member with size > 1, use it
										// Otherwise, search for instantiated types (template vs instantiation mismatch)
										if (direct_member_size > 1) {
											FLASH_LOG(Codegen, Debug, "sizeof(member_access): FOUND member size=", direct_member_size);
											return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(direct_member_size) };
										}
										
										// Fallback: If direct lookup failed or found size <= 1 (could be unsubstituted template),
										// search for instantiated types that match this base template name
										// This handles cases like test<int> where type_index points to 'test' 
										// but we need 'test$hash' for the correct member size
										for (const auto& ti : gTypeInfo) {
											std::string_view ti_name = StringTable::getStringView(ti.name());
											// Check if this is an instantiation of the base template
											// Instantiated names start with base_name followed by '_' or '$'
											if (ti_name.size() > base_type_name.size() && 
											    ti_name.substr(0, base_type_name.size()) == base_type_name &&
											    (ti_name[base_type_name.size()] == '_' || ti_name[base_type_name.size()] == '$')) {
												const StructTypeInfo* inst_struct_info = ti.getStructInfo();
												if (inst_struct_info && !inst_struct_info->members.empty()) {
													for (const auto& member : inst_struct_info->members) {
														if (StringTable::getStringView(member.getName()) == member_name) {
															FLASH_LOG(Codegen, Debug, "sizeof(member_access): Found in instantiated type '", ti_name, "' member size=", member.size);
															return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(member.size) };
														}
													}
												}
											}
										}
										
										// If no instantiation found but direct lookup had a result, use that
										if (direct_member_size > 0) {
											FLASH_LOG(Codegen, Debug, "sizeof(member_access): Using direct lookup member size=", direct_member_size);
											return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(direct_member_size) };
										}
									}
								}
							}
						}
					}
				}
			}
			// Special handling for array subscript: sizeof(arr[0]) 
			// This should not generate runtime code - just get the element type
			else if (std::holds_alternative<ArraySubscriptNode>(expr)) {
				const ArraySubscriptNode& array_subscript = std::get<ArraySubscriptNode>(expr);
				const ASTNode& array_expr_node = array_subscript.array_expr();
				
				// Check if the array expression is an identifier
				if (array_expr_node.is<ExpressionNode>()) {
					const ExpressionNode& array_expr = array_expr_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(array_expr)) {
						const IdentifierNode& id_node = std::get<IdentifierNode>(array_expr);
						
						// Look up the array identifier in the symbol table
						std::optional<ASTNode> symbol = symbol_table.lookup(id_node.name());
						if (!symbol.has_value() && global_symbol_table_) {
							symbol = global_symbol_table_->lookup(id_node.name());
						}
						
						if (symbol.has_value()) {
							const DeclarationNode* decl = get_decl_from_symbol(*symbol);
							if (decl) {
								const TypeSpecifierNode& var_type = decl->type_node().as<TypeSpecifierNode>();
								
								// Get the base element type size
								size_t element_size = var_type.size_in_bits() / 8;
								if (element_size == 0) {
									element_size = get_type_size_bits(var_type.type()) / 8;
								}
								
								// Handle struct element types
								if (element_size == 0 && var_type.type() == Type::Struct) {
									size_t type_index = var_type.type_index();
									if (type_index < gTypeInfo.size()) {
										const TypeInfo& type_info = gTypeInfo[type_index];
										const StructTypeInfo* struct_info = type_info.getStructInfo();
										if (struct_info) {
											element_size = struct_info->total_size;
										}
									}
								}
								
								// For multidimensional arrays, arr[0] should return size of the sub-array
								// e.g., for int arr[3][4], sizeof(arr[0]) = sizeof(int[4]) = 16
								const auto& dims = decl->array_dimensions();
								if (dims.size() > 1) {
									// Calculate sub-array size: element_size * product of all dims except first
									size_t sub_array_count = 1;
									ConstExpr::EvaluationContext ctx(symbol_table);
									
									for (size_t i = 1; i < dims.size(); ++i) {
										auto eval_result = ConstExpr::Evaluator::evaluate(dims[i], ctx);
										if (!eval_result.success()) {
											// Can't evaluate dimension at compile time, fall through to IR generation
											FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): Could not evaluate dimension ", i, 
											          " for '", id_node.name(), "', falling back to IR generation");
											goto fallback_to_ir;
										}
										
										long long dim_size = eval_result.as_int();
										if (dim_size <= 0) {
											FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): Invalid dimension size ", dim_size, 
											          " for '", id_node.name(), "'");
											goto fallback_to_ir;
										}
										
										sub_array_count *= static_cast<size_t>(dim_size);
									}
									
									size_in_bytes = element_size * sub_array_count;
									FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): multidim array=", id_node.name(), 
									          " element_size=", element_size, " sub_array_count=", sub_array_count,
									          " total=", size_in_bytes);
								} else {
									// Single dimension or non-array, just return element size
									size_in_bytes = element_size;
									FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): array=", id_node.name(), 
									          " element_size=", size_in_bytes);
								}
								
								// Return the size without generating runtime IR
								return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(size_in_bytes) };
							}
						}
						
						fallback_to_ir:
						
						// If we couldn't resolve compile-time, log and fall through
						FLASH_LOG(Codegen, Debug, "sizeof(arr[index]): Could not resolve '", id_node.name(), 
						          "' at compile-time, falling back to IR generation");
					}
				}
			}

			// Fall back to default expression handling
			// Generate IR for the expression to get its type
			auto expr_operands = visitExpressionNode(expr_node.as<ExpressionNode>());
			if (expr_operands.empty()) {
				return {};
			}

			// Extract type and size from the expression result
			Type expr_type = std::get<Type>(expr_operands[0]);
			int size_in_bits = std::get<int>(expr_operands[1]);

			// Handle struct types
			if (expr_type == Type::Struct) {
				// For struct expressions, we need to look up the type index
				// This is a simplification - in a full implementation we'd track type_index through expressions
				throw std::runtime_error("sizeof(struct_expression) not fully implemented yet");
				return {};
			}
			else {
				size_in_bytes = size_in_bits / 8;
			}
		}

		// Safety check: if size_in_bytes is still 0, something went wrong
		// This shouldn't happen, but add a fallback just in case
		if (size_in_bytes == 0) {
			FLASH_LOG(Codegen, Warning, "sizeof returned 0, this indicates a bug in type size tracking");
		}

		// Return sizeof result as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(size_in_bytes) };
	}

	std::vector<IrOperand> generateAlignofIr(const AlignofExprNode& alignofNode) {
		size_t alignment = 0;

		if (alignofNode.is_type()) {
			// alignof(type)
			const ASTNode& type_node = alignofNode.type_or_expr();
			if (!type_node.is<TypeSpecifierNode>()) {
				throw std::runtime_error("alignof type argument must be TypeSpecifierNode");
				return {};
			}

			const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
			Type type = type_spec.type();

			// Handle struct types
			if (type == Type::Struct) {
				size_t type_index = type_spec.type_index();
				if (type_index >= gTypeInfo.size()) {
					throw std::runtime_error("Invalid type index for struct");
					return {};
				}

				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (!struct_info) {
					throw std::runtime_error("Struct type info not found");
					return {};
				}

				alignment = struct_info->alignment;
			}
			else {
				// For primitive types, use standard alignment calculation
				size_t size_in_bytes = type_spec.size_in_bits() / 8;
				alignment = calculate_alignment_from_size(size_in_bytes, type);
			}
		}
		else {
			// alignof(expression) - determine the alignment of the expression's type
			const ASTNode& expr_node = alignofNode.type_or_expr();
			if (!expr_node.is<ExpressionNode>()) {
				throw std::runtime_error("alignof expression argument must be ExpressionNode");
				return {};
			}

			// Special handling for identifiers: alignof(x) where x is a variable
			const ExpressionNode& expr = expr_node.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
				
				// Look up the identifier in the symbol table
				std::optional<ASTNode> symbol = symbol_table.lookup(id_node.name());
				if (!symbol.has_value() && global_symbol_table_) {
					symbol = global_symbol_table_->lookup(id_node.name());
				}
				
				if (symbol.has_value()) {
					const DeclarationNode* decl = get_decl_from_symbol(*symbol);
					if (decl) {
						// Get the type alignment from the declaration
						const TypeSpecifierNode& var_type = decl->type_node().as<TypeSpecifierNode>();
						if (var_type.type() == Type::Struct) {
							size_t type_index = var_type.type_index();
							if (type_index < gTypeInfo.size()) {
								const TypeInfo& type_info = gTypeInfo[type_index];
								const StructTypeInfo* struct_info = type_info.getStructInfo();
								if (struct_info) {
									return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(struct_info->alignment) };
								}
							}
						} else {
							// Primitive type - use get_type_size_bits to handle cases where size_in_bits wasn't set
							int size_bits = var_type.size_in_bits();
							if (size_bits == 0) {
								size_bits = get_type_size_bits(var_type.type());
							}
							size_t size_in_bytes = size_bits / 8;
							alignment = calculate_alignment_from_size(size_in_bytes, var_type.type());
							return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(alignment) };
						}
					}
				}
			}

			// Fall back to default expression handling
			// Generate IR for the expression to get its type
			auto expr_operands = visitExpressionNode(expr_node.as<ExpressionNode>());
			if (expr_operands.empty()) {
				return {};
			}

			// Extract type and size from the expression result
			Type expr_type = std::get<Type>(expr_operands[0]);
			int size_in_bits = std::get<int>(expr_operands[1]);

			// Handle struct types
			if (expr_type == Type::Struct) {
				// For struct expressions, we need to look up the type index
				// This is a simplification - in a full implementation we'd track type_index through expressions
				throw std::runtime_error("alignof(struct_expression) not fully implemented yet");
				return {};
			}
			else {
				// For primitive types
				size_t size_in_bytes = size_in_bits / 8;
				alignment = calculate_alignment_from_size(size_in_bytes, expr_type);
			}
		}

		// Safety check: alignment should never be 0 for valid types
		assert(alignment != 0 && "alignof returned 0, this indicates a bug in type alignment tracking");

		// Return alignof result as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(alignment) };
	}

	std::vector<IrOperand> generateOffsetofIr(const OffsetofExprNode& offsetofNode) {
		// offsetof(struct_type, member)
		const ASTNode& type_node = offsetofNode.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			throw std::runtime_error("offsetof type argument must be TypeSpecifierNode");
			return {};
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		if (type_spec.type() != Type::Struct) {
			throw std::runtime_error("offsetof requires a struct type");
			return {};
		}

		// Get the struct type info
		size_t type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			throw std::runtime_error("Invalid type index for struct");
			return {};
		}

		// Find the member
		std::string_view member_name = offsetofNode.member_name();
		auto member_result = FlashCpp::gLazyMemberResolver.resolve(
			static_cast<TypeIndex>(type_index),
			StringTable::getOrInternStringHandle(std::string(member_name)));
		if (!member_result) {
			throw std::runtime_error("Member not found in struct");
			return {};
		}

		// Return offset as a constant unsigned long long (size_t equivalent)
		// Format: [type, size_bits, value]
		return { Type::UnsignedLongLong, 64, static_cast<unsigned long long>(member_result.adjusted_offset) };
	}

	// Helper function to check if a type is a scalar type (arithmetic, enum, pointer, member pointer, nullptr_t)
	bool isScalarType(Type type, bool is_reference, size_t pointer_depth) const {
		if (is_reference) return false;
		if (pointer_depth > 0) return true;  // Pointers are scalar
		return (type == Type::Bool || type == Type::Char || type == Type::Short ||
		        type == Type::Int || type == Type::Long || type == Type::LongLong ||
		        type == Type::UnsignedChar || type == Type::UnsignedShort ||
		        type == Type::UnsignedInt || type == Type::UnsignedLong ||
		        type == Type::UnsignedLongLong || type == Type::Float ||
		        type == Type::Double || type == Type::LongDouble || type == Type::Enum ||
		        type == Type::Nullptr || type == Type::MemberObjectPointer ||
		        type == Type::MemberFunctionPointer);
	}

	bool isArithmeticType(Type type) const {
		// Branchless: arithmetic types are Bool(1) through LongDouble(14)
		// Using range check instead of multiple comparisons
		return (static_cast<int_fast16_t>(type) >= static_cast<int_fast16_t>(Type::Bool)) &
		       (static_cast<int_fast16_t>(type) <= static_cast<int_fast16_t>(Type::LongDouble));
	}

	bool isFundamentalType(Type type) const {
		// Branchless: fundamental types are Void(0), Nullptr(28), or arithmetic types Bool(1) through LongDouble(14)
		// Using bitwise OR of conditions for branchless evaluation
		return (type == Type::Void) | (type == Type::Nullptr) | isArithmeticType(type);
	}

	std::vector<IrOperand> generateTypeTraitIr(const TypeTraitExprNode& traitNode) {
		// Type traits evaluate to a compile-time boolean constant
		bool result = false;

		// Handle no-argument traits first (like __is_constant_evaluated)
		if (traitNode.is_no_arg_trait()) {
			switch (traitNode.kind()) {
				case TypeTraitKind::IsConstantEvaluated:
					// __is_constant_evaluated() - returns true if being evaluated at compile time
					// In runtime code, this always returns false
					// In constexpr context, this would return true
					// For now, return false (runtime context)
					result = false;
					break;
				default:
					result = false;
					break;
			}
			// Return result as a bool constant
			return { Type::Bool, 8, static_cast<unsigned long long>(result ? 1 : 0) };
		}

		// For traits that require type arguments, extract the type information
		const ASTNode& type_node = traitNode.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			throw std::runtime_error("Type trait argument must be TypeSpecifierNode");
			return {};
		}

		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		Type type = type_spec.type();
		bool is_reference = type_spec.is_reference();
		bool is_rvalue_reference = type_spec.is_rvalue_reference();
		size_t pointer_depth = type_spec.pointer_depth();
		
		// Get TypeInfo and StructTypeInfo for use by shared evaluator and binary traits
		[[maybe_unused]] const TypeInfo* outer_type_info = (type_spec.type_index() < gTypeInfo.size()) ? &gTypeInfo[type_spec.type_index()] : nullptr;
		[[maybe_unused]] const StructTypeInfo* outer_struct_info = outer_type_info ? outer_type_info->getStructInfo() : nullptr;

		// Handle binary traits that require a second type argument
		switch (traitNode.kind()) {
			case TypeTraitKind::IsBaseOf:
				// __is_base_of(Base, Derived) - Check if Base is a base class of Derived
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& derived_spec = second_type_node.as<TypeSpecifierNode>();
						
						// Both types must be class types (not references, not pointers)
						if (type == Type::Struct && derived_spec.type() == Type::Struct &&
						    !is_reference && pointer_depth == 0 &&
						    !derived_spec.is_reference() && derived_spec.pointer_depth() == 0 &&
						    type_spec.type_index() < gTypeInfo.size() &&
						    derived_spec.type_index() < gTypeInfo.size()) {
							
							const TypeInfo& base_info = gTypeInfo[type_spec.type_index()];
							const TypeInfo& derived_info = gTypeInfo[derived_spec.type_index()];
							const StructTypeInfo* base_struct = base_info.getStructInfo();
							const StructTypeInfo* derived_struct = derived_info.getStructInfo();
							
							if (base_struct && derived_struct) {
								// Same type is considered base of itself
								if (type_spec.type_index() == derived_spec.type_index()) {
									result = true;
								} else {
									// Check if base_struct is in derived_struct's base classes
									for (const auto& base_class : derived_struct->base_classes) {
										if (base_class.type_index == type_spec.type_index()) {
											result = true;
											break;
										}
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsSame:
				// __is_same(T, U) - Check if T and U are the same type (exactly the same)
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& second_spec = second_type_node.as<TypeSpecifierNode>();
						
						// Check if all properties match exactly
						result = (type == second_spec.type() &&
						         is_reference == second_spec.is_reference() &&
						         is_rvalue_reference == second_spec.is_rvalue_reference() &&
						         pointer_depth == second_spec.pointer_depth() &&
						         type_spec.type_index() == second_spec.type_index() &&
						         type_spec.is_array() == second_spec.is_array() &&
						         type_spec.is_const() == second_spec.is_const() &&
						         type_spec.is_volatile() == second_spec.is_volatile());
					}
				}
				break;

			case TypeTraitKind::IsConvertible:
				// __is_convertible(From, To) - Check if From can be converted to To
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& to_spec = second_type_node.as<TypeSpecifierNode>();
						const TypeSpecifierNode& from_spec = type_spec;
						
						Type from_type = from_spec.type();
						Type to_type = to_spec.type();
						bool from_is_ref = from_spec.is_reference();
						bool to_is_ref = to_spec.is_reference();
						size_t from_ptr_depth = from_spec.pointer_depth();
						size_t to_ptr_depth = to_spec.pointer_depth();
						
						// Same type is always convertible
						if (from_type == to_type && from_is_ref == to_is_ref && 
						    from_ptr_depth == to_ptr_depth && 
						    from_spec.type_index() == to_spec.type_index()) {
							result = true;
						}
						// Arithmetic types are generally convertible to each other
						else if (isArithmeticType(from_type) && isArithmeticType(to_type) &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0) {
							result = true;
						}
						// Pointers with same depth and compatible types
						else if (from_ptr_depth > 0 && to_ptr_depth > 0 && 
						         from_ptr_depth == to_ptr_depth && !from_is_ref && !to_is_ref) {
							// Pointer convertibility (same type or derived-to-base)
							result = (from_type == to_type || from_spec.type_index() == to_spec.type_index());
						}
						// nullptr_t is convertible to any pointer type
						else if (from_type == Type::Nullptr && to_ptr_depth > 0 && !to_is_ref) {
							result = true;
						}
						// Derived to base conversion for class types
						else if (from_type == Type::Struct && to_type == Type::Struct &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0 &&
						         from_spec.type_index() < gTypeInfo.size() &&
						         to_spec.type_index() < gTypeInfo.size()) {
							// Check if from_type is derived from to_type
							const TypeInfo& from_info = gTypeInfo[from_spec.type_index()];
							const StructTypeInfo* from_struct = from_info.getStructInfo();
							if (from_struct) {
								for (const auto& base_class : from_struct->base_classes) {
									if (base_class.type_index == to_spec.type_index()) {
										result = true;
										break;
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsNothrowConvertible:
				// __is_nothrow_convertible(From, To) - Same as IsConvertible but for nothrow conversions
				// For now, use the same logic as IsConvertible (conservative approximation)
				if (traitNode.has_second_type()) {
					const ASTNode& second_type_node = traitNode.second_type_node();
					if (second_type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& to_spec = second_type_node.as<TypeSpecifierNode>();
						const TypeSpecifierNode& from_spec = type_spec;
						
						Type from_type = from_spec.type();
						Type to_type = to_spec.type();
						bool from_is_ref = from_spec.is_reference();
						bool to_is_ref = to_spec.is_reference();
						size_t from_ptr_depth = from_spec.pointer_depth();
						size_t to_ptr_depth = to_spec.pointer_depth();
						
						// Same type is always nothrow convertible
						if (from_type == to_type && from_is_ref == to_is_ref && 
						    from_ptr_depth == to_ptr_depth && 
						    from_spec.type_index() == to_spec.type_index()) {
							result = true;
						}
						// Arithmetic types are nothrow convertible to each other
						else if (isArithmeticType(from_type) && isArithmeticType(to_type) &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0) {
							result = true;
						}
						// Pointers with same depth and compatible types
						else if (from_ptr_depth > 0 && to_ptr_depth > 0 && 
						         from_ptr_depth == to_ptr_depth && !from_is_ref && !to_is_ref) {
							result = (from_type == to_type || from_spec.type_index() == to_spec.type_index());
						}
						// nullptr_t is nothrow convertible to any pointer type
						else if (from_type == Type::Nullptr && to_ptr_depth > 0 && !to_is_ref) {
							result = true;
						}
						// Derived to base conversion for class types (nothrow if no virtual base)
						else if (from_type == Type::Struct && to_type == Type::Struct &&
						         !from_is_ref && !to_is_ref && 
						         from_ptr_depth == 0 && to_ptr_depth == 0 &&
						         from_spec.type_index() < gTypeInfo.size() &&
						         to_spec.type_index() < gTypeInfo.size()) {
							// Check if from_type is derived from to_type
							const TypeInfo& from_info = gTypeInfo[from_spec.type_index()];
							const StructTypeInfo* from_struct = from_info.getStructInfo();
							if (from_struct) {
								for (const auto& base_class : from_struct->base_classes) {
									if (base_class.type_index == to_spec.type_index()) {
										// Base class found - nothrow if not virtual
										result = !base_class.is_virtual;
										break;
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsPolymorphic:
				// A polymorphic class has at least one virtual function
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					result = struct_info && struct_info->has_vtable;
				}
				break;

			case TypeTraitKind::IsFinal:
				// A final class cannot be derived from
				// Note: This requires tracking 'final' keyword on classes
				// For now, check if any member function is marked final
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Check if any virtual function is marked final
						for (const auto& func : struct_info->member_functions) {
							if (func.is_final) {
								result = true;
								break;
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsAbstract:
				// An abstract class has at least one pure virtual function
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					result = struct_info && struct_info->is_abstract;
				}
				break;

			case TypeTraitKind::IsEmpty:
				// An empty class has no non-static data members (excluding empty base classes)
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Check if there are no non-static data members
						// and no virtual functions (vtable pointer would be a member)
						result = struct_info->members.empty() && !struct_info->has_vtable;
					}
				}
				break;

			case TypeTraitKind::IsAggregate:
				// An aggregate is:
				// - An array type, or
				// - A class type (struct/class/union) with:
				//   - No user-declared or inherited constructors
				//   - No private or protected non-static data members
				//   - No virtual functions
				//   - No virtual, private, or protected base classes
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Check aggregate conditions:
						// 1. No user-declared constructors (check member_functions for non-implicit constructors)
						// 2. No private or protected members (all members are public)
						// 3. No virtual functions (has_vtable flag)
						bool has_user_constructors = false;
						for (const auto& func : struct_info->member_functions) {
							if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
								const ConstructorDeclarationNode& ctor = func.function_decl.as<ConstructorDeclarationNode>();
								if (!ctor.is_implicit()) {
									has_user_constructors = true;
									break;
								}
							}
						}
						
						bool no_virtual = !struct_info->has_vtable;
						bool all_public = true;
						
						for (const auto& member : struct_info->members) {
							if (member.access == AccessSpecifier::Private || 
							    member.access == AccessSpecifier::Protected) {
								all_public = false;
								break;
							}
						}
						
						result = !has_user_constructors && no_virtual && all_public;
					}
				}
				// Arrays are aggregates
				else if (pointer_depth == 0 && !is_reference && type_spec.is_array()) {
					result = true;
				}
				break;

			case TypeTraitKind::IsStandardLayout:
				// A standard-layout class has specific requirements:
				// - No virtual functions or virtual base classes
				// - All non-static data members have same access control
				// - No base classes with non-static data members
				// - No base classes of the same type as first non-static data member
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Basic check: no virtual functions
						result = !struct_info->has_vtable;
						// If all members have the same access specifier, it's a simple standard layout
						if (result && struct_info->members.size() > 1) {
							AccessSpecifier first_access = struct_info->members[0].access;
							for (const auto& member : struct_info->members) {
								if (member.access != first_access) {
									result = false;
									break;
								}
							}
						}
					}
				}
				// Scalar types are standard layout
				else if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				break;

			case TypeTraitKind::HasUniqueObjectRepresentations:
				// Types with no padding bits have unique object representations
				// Integral types (except bool), and trivially copyable types without padding
				if ((type == Type::Char || type == Type::Short || type == Type::Int ||
				     type == Type::Long || type == Type::LongLong || type == Type::UnsignedChar ||
				     type == Type::UnsignedShort || type == Type::UnsignedInt ||
				     type == Type::UnsignedLong || type == Type::UnsignedLongLong)
				    && !is_reference && pointer_depth == 0) {
					result = true;
				}
				// Note: float/double may have padding or non-unique representations
				break;

			case TypeTraitKind::IsTriviallyCopyable:
				// A trivially copyable type can be copied with memcpy
				// - Scalar types (arithmetic, pointers, enums)
				// - Classes with trivial copy/move constructors and destructors, no virtual
				// TODO: Implement proper checking of copy/move constructors and assignment operators
				//       for full C++ standard compliance
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Classes: need to check for trivial special members and no virtual
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Simple heuristic: no virtual functions means likely trivially copyable
						// TODO: A more complete check would verify copy/move ctors are trivial
						result = !struct_info->has_vtable;
					}
				}
				break;

			case TypeTraitKind::IsTrivial:
				// A trivial type is trivially copyable and has a trivial default constructor
				// TODO: Full compliance requires checking that:
				//       - Has trivial default constructor
				//       - Has trivial copy constructor
				//       - Has trivial move constructor
				//       - Has trivial copy assignment operator
				//       - Has trivial move assignment operator
				//       - Has trivial destructor
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Simple heuristic: no virtual functions and no user-defined constructors
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsPod:
				// POD (Plain Old Data) = trivial + standard layout (C++03 compatible)
				// In C++11+, this is deprecated but still useful
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// POD: no virtual functions, no user-defined ctors, all members same access
						bool is_pod = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
						if (is_pod && struct_info->members.size() > 1) {
							AccessSpecifier first_access = struct_info->members[0].access;
							for (const auto& member : struct_info->members) {
								if (member.access != first_access) {
									is_pod = false;
									break;
								}
							}
						}
						result = is_pod;
					}
				}
				break;

			case TypeTraitKind::IsLiteralType:
				// __is_literal_type - deprecated in C++17, removed in C++20
				FLASH_LOG(Codegen, Warning, "__is_literal_type is deprecated in C++17 and removed in C++20. "
				          "This trait is likely being invoked from a standard library header (e.g., <type_traits>) "
				          "that hasn't been fully updated for C++20. In modern C++, use std::is_constant_evaluated() "
				          "to check for compile-time contexts, or use other appropriate type traits.");
				// A literal type is one that can be used in constexpr context:
				// - Scalar types
				// - References
				// - Arrays of literal types
				// - Class types that have all of:
				//   - Trivial destructor
				//   - Aggregate type OR has at least one constexpr constructor
				//   - All non-static data members are literal types
				if (isScalarType(type, is_reference, pointer_depth) || is_reference) {
					result = true;
				}
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Simplified check: assume literal if trivially copyable
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsConst:
				// __is_const - checks if type has const qualifier
				result = type_spec.is_const();
				break;

			case TypeTraitKind::IsVolatile:
				// __is_volatile - checks if type has volatile qualifier
				result = type_spec.is_volatile();
				break;

			case TypeTraitKind::IsSigned:
				// __is_signed - checks if integral type is signed
				result = ((type == Type::Char) |  // char is signed on most platforms
			          (type == Type::Short) | (type == Type::Int) |
			          (type == Type::Long) | (type == Type::LongLong))
			          & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsUnsigned:
				// __is_unsigned - checks if integral type is unsigned
				result = ((type == Type::Bool) |  // bool is considered unsigned
			          (type == Type::UnsignedChar) | (type == Type::UnsignedShort) |
			          (type == Type::UnsignedInt) | (type == Type::UnsignedLong) |
			          (type == Type::UnsignedLongLong))
			          & !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsBoundedArray:
				// __is_bounded_array - array with known bound (e.g., int[10])
				// Check if it's an array and the size is known
				result = type_spec.is_array() & int(type_spec.array_size() > 0) &
			         !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsUnboundedArray:
				// __is_unbounded_array - array with unknown bound (e.g., int[])
				// Check if it's an array and the size is unknown (0 or negative)
				result = type_spec.is_array() & int(type_spec.array_size() <= 0) &
			         !is_reference & (pointer_depth == 0);
				break;

			case TypeTraitKind::IsConstructible:
				// __is_constructible(T, Args...) - Check if T can be constructed with Args...
				// For scalar types, default constructible (no args) or copy constructible (same type arg)
				if (isScalarType(type, is_reference, pointer_depth)) {
					const auto& arg_types = traitNode.additional_type_nodes();
					if (arg_types.empty()) {
						// Default constructible - all scalars are default constructible
						result = true;
					} else if (arg_types.size() == 1 && arg_types[0].is<TypeSpecifierNode>()) {
						// Copy/conversion construction - check if types are compatible
						const TypeSpecifierNode& arg_spec = arg_types[0].as<TypeSpecifierNode>();
						// Same type or convertible arithmetic types
						result = (arg_spec.type() == type) || 
						         (isScalarType(arg_spec.type(), arg_spec.is_reference(), arg_spec.pointer_depth()) &&
						          !arg_spec.is_reference() && arg_spec.pointer_depth() == 0);
					}
				}
				// Class types: check for appropriate constructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						const auto& arg_types = traitNode.additional_type_nodes();
						if (arg_types.empty()) {
							// Default constructible - has default constructor or no user-defined ctors
							result = !struct_info->hasUserDefinedConstructor() || struct_info->hasConstructor();
						} else {
							// Check for matching constructor
							// Simple heuristic: if it has any user-defined constructor, assume constructible
							result = struct_info->hasUserDefinedConstructor();
						}
					}
				}
				break;

			case TypeTraitKind::IsTriviallyConstructible:
				// __is_trivially_constructible(T, Args...) - Check if T can be trivially constructed
				// Scalar types are trivially constructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: no virtual, no user-defined ctors
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsNothrowConstructible:
				// __is_nothrow_constructible(T, Args...) - Check if T can be constructed without throwing
				// Scalar types don't throw
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: similar to trivially constructible for now
				// TODO: Check for noexcept constructors
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedConstructor();
					}
				}
				break;

			case TypeTraitKind::IsAssignable:
				// __is_assignable(To, From) - Check if From can be assigned to To
				if (traitNode.has_second_type()) {
					const ASTNode& from_node = traitNode.second_type_node();
					if (from_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& from_spec = from_node.as<TypeSpecifierNode>();
						
						// For scalar types, check type compatibility
						if (isScalarType(type, is_reference, pointer_depth)) {
							// Scalars are assignable from compatible types
							result = isScalarType(from_spec.type(), from_spec.is_reference(), from_spec.pointer_depth());
						}
						// Class types: check for assignment operator
						else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && !struct_info->is_union) {
								// If has copy/move assignment or no user-defined, assume assignable
								result = struct_info->hasCopyAssignmentOperator() || 
								         struct_info->hasMoveAssignmentOperator() ||
								         !struct_info->hasUserDefinedConstructor();
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsTriviallyAssignable:
				// __is_trivially_assignable(To, From) - Check if From can be trivially assigned to To
				if (traitNode.has_second_type()) {
					const ASTNode& from_node = traitNode.second_type_node();
					if (from_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& from_spec = from_node.as<TypeSpecifierNode>();
						
						// Scalar types are trivially assignable
						if (isScalarType(type, is_reference, pointer_depth) &&
						    isScalarType(from_spec.type(), from_spec.is_reference(), from_spec.pointer_depth())) {
							result = true;
						}
						// Class types: no virtual, no user-defined assignment
						else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
						         !is_reference && pointer_depth == 0) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && !struct_info->is_union) {
								result = !struct_info->has_vtable && 
								         !struct_info->hasCopyAssignmentOperator() && 
								         !struct_info->hasMoveAssignmentOperator();
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsNothrowAssignable:
				// __is_nothrow_assignable(To, From) - Check if From can be assigned without throwing
				if (traitNode.has_second_type()) {
					const ASTNode& from_node = traitNode.second_type_node();
					if (from_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& from_spec = from_node.as<TypeSpecifierNode>();
						
						// Scalar types don't throw on assignment
						if (isScalarType(type, is_reference, pointer_depth) &&
						    isScalarType(from_spec.type(), from_spec.is_reference(), from_spec.pointer_depth())) {
							result = true;
						}
						// Class types: similar to trivially assignable for now
						// TODO: Check for noexcept assignment operators
						else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
						         !is_reference && pointer_depth == 0) {
							const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info && !struct_info->is_union) {
								result = !struct_info->has_vtable;
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsDestructible:
				// __is_destructible(T) - Check if T can be destroyed
				// All scalar types are destructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: check for accessible destructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Assume destructible unless we can prove otherwise
						// (no deleted destructor check available yet)
						result = true;
					}
				}
				break;

			case TypeTraitKind::IsTriviallyDestructible:
				// __is_trivially_destructible(T) - Check if T can be trivially destroyed
				// Scalar types are trivially destructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: no virtual, no user-defined destructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Trivially destructible if no vtable and no user-defined destructor
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedDestructor();
					} else if (struct_info && struct_info->is_union) {
						// Unions are trivially destructible if all members are
						result = true;
					}
				}
				break;

			case TypeTraitKind::IsNothrowDestructible:
				// __is_nothrow_destructible(T) - Check if T can be destroyed without throwing
				// Scalar types don't throw on destruction
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: assume noexcept destructor (most are in practice)
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Most destructors are noexcept by default since C++11
						result = true;
					}
				}
				break;

			case TypeTraitKind::HasTrivialDestructor:
				// __has_trivial_destructor(T) - GCC/Clang intrinsic, equivalent to IsTriviallyDestructible
				// Scalar types are trivially destructible
				if (isScalarType(type, is_reference, pointer_depth)) {
					result = true;
				}
				// Class types: no virtual, no user-defined destructor
				else if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				         !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Trivially destructible if no vtable and no user-defined destructor
						result = !struct_info->has_vtable && !struct_info->hasUserDefinedDestructor();
					} else if (struct_info && struct_info->is_union) {
						// Unions are trivially destructible if all members are
						result = true;
					}
				}
				break;

			case TypeTraitKind::HasVirtualDestructor:
				// __has_virtual_destructor(T) - Check if T has a virtual destructor
				// Only class types can have virtual destructors
				if (type == Type::Struct && type_spec.type_index() < gTypeInfo.size() &&
				    !is_reference && pointer_depth == 0) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->is_union) {
						// Check if the destructor is explicitly marked as virtual
						// A class has a virtual destructor if:
						// 1. Its destructor is declared virtual, or
						// 2. It inherits from a base class with a virtual destructor
						// For now, we check if the class has a vtable (which implies virtual methods)
						// and if it has a user-defined destructor
						result = struct_info->has_vtable && struct_info->hasUserDefinedDestructor();
						
						// If the class has a vtable but no explicit destructor, check base classes
						if (!result && struct_info->has_vtable && !struct_info->base_classes.empty()) {
							// Check if any base class has a virtual destructor
							for (const auto& base : struct_info->base_classes) {
								if (base.type_index < gTypeInfo.size()) {
									const TypeInfo& base_type_info = gTypeInfo[base.type_index];
									const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
									if (base_struct_info && base_struct_info->has_vtable) {
										// If base has vtable, it might have virtual destructor
										// For simplicity, we assume presence of vtable indicates virtual destructor
										result = true;
										break;
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::IsLayoutCompatible:
				// __is_layout_compatible(T, U) - Check if T and U have the same layout
				if (traitNode.has_second_type()) {
					const ASTNode& second_node = traitNode.second_type_node();
					if (second_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& second_spec = second_node.as<TypeSpecifierNode>();
						
						// Same type is always layout compatible with itself
						if (type == second_spec.type() && 
						    pointer_depth == second_spec.pointer_depth() &&
						    is_reference == second_spec.is_reference()) {
							if (type == Type::Struct) {
								result = (type_spec.type_index() == second_spec.type_index());
							} else {
								result = true;
							}
						}
						// Different standard layout types with same size
						else if (isScalarType(type, is_reference, pointer_depth) &&
						         isScalarType(second_spec.type(), second_spec.is_reference(), second_spec.pointer_depth())) {
							result = (type_spec.size_in_bits() == second_spec.size_in_bits());
						}
					}
				}
				break;

			case TypeTraitKind::IsPointerInterconvertibleBaseOf:
				// __is_pointer_interconvertible_base_of(Base, Derived)
				// Check if Base is pointer-interconvertible with Derived
				// According to C++20: requires both to be standard-layout types and
				// Base is either the first base class or shares address with Derived
				if (traitNode.has_second_type()) {
					const ASTNode& derived_node = traitNode.second_type_node();
					if (derived_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& derived_spec = derived_node.as<TypeSpecifierNode>();
						
						// Both must be class types (not references, not pointers)
						if (type == Type::Struct && derived_spec.type() == Type::Struct &&
						    !is_reference && pointer_depth == 0 &&
						    !derived_spec.is_reference() && derived_spec.pointer_depth() == 0 &&
						    type_spec.type_index() < gTypeInfo.size() &&
						    derived_spec.type_index() < gTypeInfo.size()) {
							
							const TypeInfo& base_info = gTypeInfo[type_spec.type_index()];
							const TypeInfo& derived_info = gTypeInfo[derived_spec.type_index()];
							const StructTypeInfo* base_struct = base_info.getStructInfo();
							const StructTypeInfo* derived_struct = derived_info.getStructInfo();
							
							if (base_struct && derived_struct) {
								// Same type is pointer interconvertible with itself
								if (type_spec.type_index() == derived_spec.type_index()) {
									result = true;
								} else {
									// Both types must be standard-layout for pointer interconvertibility
									bool base_is_standard_layout = base_struct->isStandardLayout();
									bool derived_is_standard_layout = derived_struct->isStandardLayout();
									
									if (base_is_standard_layout && derived_is_standard_layout) {
										// Check if Base is the first base class at offset 0
										for (size_t i = 0; i < derived_struct->base_classes.size(); ++i) {
											if (derived_struct->base_classes[i].type_index == type_spec.type_index()) {
												// First base class at offset 0 is pointer interconvertible
												result = (i == 0);
												break;
											}
										}
									}
								}
							}
						}
					}
				}
				break;

			case TypeTraitKind::UnderlyingType:
				// __underlying_type(T) returns the underlying type of an enum
				// This is a type query, not a bool result - handle specially
				if (type == Type::Enum && !is_reference && pointer_depth == 0 &&
				    type_spec.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					const EnumTypeInfo* enum_info = type_info.getEnumInfo();
					if (enum_info) {
						// Return the enum's declared underlying type
						return { enum_info->underlying_type, enum_info->underlying_size, 0ULL };
					}
					// Fallback to int if no enum info
					return { Type::Int, 32, 0ULL };
				}
				// For non-enums, this is an error - return false/0
				result = false;
				break;

			default:
				// For all other unary type traits, use the shared evaluator from TypeTraitEvaluator.h
				{
					TypeTraitResult eval_result = evaluateTypeTrait(traitNode.kind(), type_spec, outer_type_info, outer_struct_info);
					if (eval_result.success) {
						result = eval_result.value;
					} else {
						result = false;
					}
				}
				break;
		}

		// Return result as a bool constant
		// Format: [type, size_bits, value]
		return { Type::Bool, 8, static_cast<unsigned long long>(result ? 1 : 0) };
	}

	std::vector<IrOperand> generateNewExpressionIr(const NewExpressionNode& newExpr) {
		if (!newExpr.type_node().is<TypeSpecifierNode>()) {
			FLASH_LOG(Codegen, Error, "New expression type node is not a TypeSpecifierNode");
			return {};
		}
		
		const TypeSpecifierNode& type_spec = newExpr.type_node().as<TypeSpecifierNode>();
		Type type = type_spec.type();
		int size_in_bits = static_cast<int>(type_spec.size_in_bits());
		int pointer_depth = static_cast<int>(type_spec.pointer_depth());

		// Create a temporary variable for the result (pointer to allocated memory)
		TempVar result_var = var_counter.next();
		auto emit_scalar_new_initializer = [&](TempVar pointer_var) {
			constexpr size_t kInitOperandCount = 3;  // [type, size_in_bits, value]
			if (type == Type::Struct || newExpr.constructor_args().size() == 0) {
				return;
			}

			const auto& ctor_args = newExpr.constructor_args();
			if (ctor_args.size() > 1) {
				FLASH_LOG(Codegen, Warning, "Scalar new initializer has extra arguments; using first");
			}

			auto init_operands = visitExpressionNode(ctor_args[0].as<ExpressionNode>());
			if (init_operands.size() >= kInitOperandCount) {
				TypedValue init_value = toTypedValue(init_operands);
				emitDereferenceStore(init_value, type, size_in_bits, pointer_var, Token());
			} else {
				FLASH_LOG(Codegen, Warning, "Scalar new initializer returned insufficient operands");
			}
		};

		// Check if this is an array allocation (with or without placement)
		if (newExpr.is_array()) {
			// Array allocation: new Type[size] or placement array: new (addr) Type[size]
			// Evaluate the size expression
			if (!newExpr.size_expr().has_value()) {
				FLASH_LOG(Codegen, Error, "Array new without size expression");
				return {};
			}
			if (!newExpr.size_expr()->is<ExpressionNode>()) {
				FLASH_LOG(Codegen, Error, "Array size is not an ExpressionNode");
				return {};
			}
			
			auto size_operands = visitExpressionNode(newExpr.size_expr()->as<ExpressionNode>());

			// Check if this is placement array new
			if (newExpr.placement_address().has_value()) {
				// Placement array new: new (address) Type[size]
				// Check that placement_address is an ExpressionNode
				if (!newExpr.placement_address()->is<ExpressionNode>()) {
					FLASH_LOG(Codegen, Error, "Placement address is not an ExpressionNode");
					return {};
				}
				
				auto address_operands = visitExpressionNode(newExpr.placement_address()->as<ExpressionNode>());

				// Create PlacementNewOp for array
				PlacementNewOp op;
				op.result = result_var;
				op.type = type;
				op.size_in_bytes = size_in_bits / 8;
				op.pointer_depth = pointer_depth;
				// Convert IrOperand to IrValue
				if (address_operands.size() < 3) {
					FLASH_LOG(Codegen, Error, "Placement address operands insufficient (expected 3, got ", address_operands.size(), ")");
					return {};
				}
				if (std::holds_alternative<unsigned long long>(address_operands[2])) {
					op.address = std::get<unsigned long long>(address_operands[2]);
				} else if (std::holds_alternative<TempVar>(address_operands[2])) {
					op.address = std::get<TempVar>(address_operands[2]);
				} else if (std::holds_alternative<StringHandle>(address_operands[2])) {
					op.address = std::get<StringHandle>(address_operands[2]);
				} else if (std::holds_alternative<double>(address_operands[2])) {
					op.address = std::get<double>(address_operands[2]);
				}

				ir_.addInstruction(IrInstruction(IrOpcode::PlacementNew, std::move(op), Token()));
				
				// Handle array initializers for placement new arrays
				// Initialize each array element with the provided initializers
				const auto& array_inits = newExpr.constructor_args();
				if (array_inits.size() > 0) {
					// For struct types, call constructor for each element
					if (type == Type::Struct) {
						TypeIndex type_index = type_spec.type_index();
						if (type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index];
							if (type_info.struct_info_) {
								const StructTypeInfo* struct_info = type_info.struct_info_.get();
								size_t element_size = struct_info->total_size;
								
								// Generate initialization for each element
								for (size_t i = 0; i < array_inits.size(); ++i) {
									const ASTNode& init = array_inits[i];
									
									// Skip if the initializer is not supported
									if (!init.is<InitializerListNode>() && !init.is<ExpressionNode>()) {
										FLASH_LOG(Codegen, Warning, "Unsupported array initializer type, skipping element ", i);
										continue;
									}
									
									// Calculate offset for this element: base_pointer + i * element_size
									TempVar element_ptr = var_counter.next();
									
									// Generate: element_ptr = result_var + (i * element_size)
									BinaryOp offset_op{
										.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = result_var},
										.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(i * element_size)},
										.result = element_ptr,
									};
									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(offset_op), Token()));
									
									// Check if initializer is a brace initializer list
									if (init.is<InitializerListNode>()) {
										const InitializerListNode& init_list = init.as<InitializerListNode>();
										
										// If struct has a constructor, call it with initializer list elements
										if (struct_info->hasAnyConstructor()) {
											ConstructorCallOp ctor_op;
											ctor_op.struct_name = type_info.name();
											ctor_op.object = element_ptr;
											ctor_op.is_heap_allocated = true;
											
											// Add each initializer as a constructor argument
											for (const auto& elem_init : init_list.initializers()) {
												// Safety check: ensure elem_init is an ExpressionNode
												if (!elem_init.is<ExpressionNode>()) {
													FLASH_LOG(Codegen, Warning, "Element initializer is not an ExpressionNode, skipping");
													continue;
												}
												
												auto arg_operands = visitExpressionNode(elem_init.as<ExpressionNode>());
												if (arg_operands.size() >= 3) {
													TypedValue tv = toTypedValue(arg_operands);
													ctor_op.arguments.push_back(std::move(tv));
												}
											}
											
											ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), Token()));
										}
									} else if (init.is<ExpressionNode>()) {
										// Handle direct expression initializer
										FLASH_LOG(Codegen, Warning, "Array element initialized with expression, not initializer list");
									} else {
										FLASH_LOG(Codegen, Warning, "Unexpected array initializer type");
									}
								}
							}
						}
					} else {
						// For primitive types, initialize each element
						size_t element_size = size_in_bits / 8;
						
						for (size_t i = 0; i < array_inits.size(); ++i) {
							const ASTNode& init = array_inits[i];
							
							if (init.is<ExpressionNode>()) {
								// Calculate offset for this element
								TempVar element_ptr = var_counter.next();
								
								// Generate: element_ptr = result_var + (i * element_size)
								BinaryOp offset_op{
									.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = result_var},
									.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(i * element_size)},
									.result = element_ptr,
								};
								ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(offset_op), Token()));
								
								// Evaluate the initializer expression
								auto init_operands = visitExpressionNode(init.as<ExpressionNode>());
								if (init_operands.size() >= 3) {
									TypedValue init_value = toTypedValue(init_operands);
									emitDereferenceStore(init_value, type, size_in_bits, element_ptr, Token());
								}
							}
						}
					}
				}
			} else {
				// Regular heap-allocated array: new Type[size]
				// Create HeapAllocArrayOp
				HeapAllocArrayOp op;
				op.result = result_var;
				op.type = type;
				op.size_in_bytes = size_in_bits / 8;
				op.pointer_depth = pointer_depth;
				// Convert IrOperand to IrValue for count
				if (size_operands.size() < 3) {
					FLASH_LOG(Codegen, Error, "Array size operands insufficient (expected 3, got ", size_operands.size(), ")");
					return {};
				}
				if (std::holds_alternative<unsigned long long>(size_operands[2])) {
					op.count = std::get<unsigned long long>(size_operands[2]);
				} else if (std::holds_alternative<TempVar>(size_operands[2])) {
					op.count = std::get<TempVar>(size_operands[2]);
				} else if (std::holds_alternative<StringHandle>(size_operands[2])) {
					op.count = std::get<StringHandle>(size_operands[2]);
				} else if (std::holds_alternative<double>(size_operands[2])) {
					op.count = std::get<double>(size_operands[2]);
				}

				ir_.addInstruction(IrInstruction(IrOpcode::HeapAllocArray, std::move(op), Token()));
				
				// Handle array initializers for heap-allocated arrays
				const auto& array_inits = newExpr.constructor_args();
				if (array_inits.size() > 0) {
					// For struct types, call constructor for each element
					if (type == Type::Struct) {
						TypeIndex type_index = type_spec.type_index();
						if (type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index];
							if (type_info.struct_info_) {
								const StructTypeInfo* struct_info = type_info.struct_info_.get();
								size_t element_size = struct_info->total_size;
								
								for (size_t i = 0; i < array_inits.size(); ++i) {
									const ASTNode& init = array_inits[i];
									
									// Skip if the initializer is not supported
									if (!init.is<InitializerListNode>() && !init.is<ExpressionNode>()) {
										FLASH_LOG(Codegen, Warning, "Unsupported array initializer type in heap array, skipping element ", i);
										continue;
									}
									
									TempVar element_ptr = var_counter.next();
									BinaryOp offset_op{
										.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = result_var},
										.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(i * element_size)},
										.result = element_ptr,
									};
									ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(offset_op), Token()));
									
									if (init.is<InitializerListNode>() && struct_info->hasAnyConstructor()) {
										const InitializerListNode& init_list = init.as<InitializerListNode>();
										ConstructorCallOp ctor_op;
										ctor_op.struct_name = type_info.name();
										ctor_op.object = element_ptr;
										ctor_op.is_heap_allocated = true;
										
										for (const auto& elem_init : init_list.initializers()) {
											// Safety check: ensure elem_init is an ExpressionNode
											if (!elem_init.is<ExpressionNode>()) {
												FLASH_LOG(Codegen, Warning, "Element initializer in heap array is not an ExpressionNode, skipping");
												continue;
											}
											
											auto arg_operands = visitExpressionNode(elem_init.as<ExpressionNode>());
											if (arg_operands.size() >= 3) {
												TypedValue tv = toTypedValue(arg_operands);
												ctor_op.arguments.push_back(std::move(tv));
											}
										}
										
										ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), Token()));
									}
								}
							}
						}
					} else {
						// For primitive types, initialize each element
						size_t element_size = size_in_bits / 8;
						for (size_t i = 0; i < array_inits.size(); ++i) {
							const ASTNode& init = array_inits[i];
							if (init.is<ExpressionNode>()) {
								TempVar element_ptr = var_counter.next();
								BinaryOp offset_op{
									.lhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = result_var},
									.rhs = TypedValue{.type = Type::UnsignedLongLong, .size_in_bits = 64, .value = static_cast<unsigned long long>(i * element_size)},
									.result = element_ptr,
								};
								ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(offset_op), Token()));
								
								auto init_operands = visitExpressionNode(init.as<ExpressionNode>());
								if (init_operands.size() >= 3) {
									TypedValue init_value = toTypedValue(init_operands);
									emitDereferenceStore(init_value, type, size_in_bits, element_ptr, Token());
								}
							}
						}
					}
				}
			}
		} else if (newExpr.placement_address().has_value()) {
			// Single object placement new: new (address) Type or new (address) Type(args)
			// Evaluate the placement address expression
			auto address_operands = visitExpressionNode(newExpr.placement_address()->as<ExpressionNode>());

			// Create PlacementNewOp
			PlacementNewOp op;
			op.result = result_var;
			op.type = type;
			op.size_in_bytes = size_in_bits / 8;
			op.pointer_depth = pointer_depth;
			// Convert IrOperand to IrValue
			if (address_operands.size() < 3) {
				FLASH_LOG(Codegen, Error, "Placement address operands insufficient for single object (expected 3, got ", address_operands.size(), ")");
				return {};
			}
			if (std::holds_alternative<unsigned long long>(address_operands[2])) {
				op.address = std::get<unsigned long long>(address_operands[2]);
			} else if (std::holds_alternative<TempVar>(address_operands[2])) {
				op.address = std::get<TempVar>(address_operands[2]);
			} else if (std::holds_alternative<StringHandle>(address_operands[2])) {
				op.address = std::get<StringHandle>(address_operands[2]);
			} else if (std::holds_alternative<double>(address_operands[2])) {
				op.address = std::get<double>(address_operands[2]);
			}

			ir_.addInstruction(IrInstruction(IrOpcode::PlacementNew, std::move(op), Token()));

			// If this is a struct type with a constructor, generate constructor call
			if (type == Type::Struct) {
				TypeIndex type_index = type_spec.type_index();
				if (type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_index];
					if (type_info.struct_info_) {
						// Check if this is an abstract class
						if (type_info.struct_info_->is_abstract) {
							std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name() << "'\n";
							throw std::runtime_error("Cannot instantiate abstract class");
						}

						if (type_info.struct_info_->hasAnyConstructor()) {
							// Generate constructor call on the placement address
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name();
							ctor_op.object = result_var;
							ctor_op.is_heap_allocated = true;  // Object is at pointer location (placement new provides address)
							
							// Add constructor arguments
							const auto& ctor_args = newExpr.constructor_args();
							for (size_t i = 0; i < ctor_args.size(); ++i) {
								auto arg_operands = visitExpressionNode(ctor_args[i].as<ExpressionNode>());
								// arg_operands = [type, size, value]
								if (arg_operands.size() >= 3) {
									TypedValue tv = toTypedValue(arg_operands);
									ctor_op.arguments.push_back(std::move(tv));
								}
							}
							
							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), Token()));
						}
					}
				}
			}

			emit_scalar_new_initializer(result_var);
		} else {
			// Single object allocation: new Type or new Type(args)
			HeapAllocOp op;
			op.result = result_var;
			op.type = type;
			op.size_in_bytes = size_in_bits / 8;
			op.pointer_depth = pointer_depth;

			ir_.addInstruction(IrInstruction(IrOpcode::HeapAlloc, std::move(op), Token()));

			// If this is a struct type with a constructor, generate constructor call
			if (type == Type::Struct) {
				TypeIndex type_index = type_spec.type_index();
				if (type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_index];
					if (type_info.struct_info_) {
						// Check if this is an abstract class
						if (type_info.struct_info_->is_abstract) {
							std::cerr << "Error: Cannot instantiate abstract class '" << type_info.name() << "'\n";
							throw std::runtime_error("Cannot instantiate abstract class");
						}

						if (type_info.struct_info_->hasAnyConstructor()) {
							// Generate constructor call on the newly allocated object
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name();
							ctor_op.object = result_var;
							ctor_op.is_heap_allocated = true;  // Object is at pointer location (new allocates and returns pointer)

							// Add constructor arguments
							const auto& ctor_args = newExpr.constructor_args();
							for (size_t i = 0; i < ctor_args.size(); ++i) {
								auto arg_operands = visitExpressionNode(ctor_args[i].as<ExpressionNode>());
								// arg_operands = [type, size, value]
								if (arg_operands.size() >= 3) {
									TypedValue tv = toTypedValue(arg_operands);
									ctor_op.arguments.push_back(std::move(tv));
								}
							}
						
							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), Token()));
						}
					}
				}
			}

			emit_scalar_new_initializer(result_var);
		}
		
		// Return pointer to allocated memory
		// The result is a pointer, so we return it with pointer_depth + 1
		return { type, size_in_bits, result_var, 0ULL };
	}

	std::vector<IrOperand> generateDeleteExpressionIr(const DeleteExpressionNode& deleteExpr) {
		// Evaluate the expression to get the pointer to delete
		auto ptr_operands = visitExpressionNode(deleteExpr.expr().as<ExpressionNode>());

		// Get the pointer type
		Type ptr_type = std::get<Type>(ptr_operands[0]);

		// Check if we need to call destructor (for struct types)
		if (ptr_type == Type::Struct && !deleteExpr.is_array()) {
			// For single object deletion, call destructor before freeing
			// Note: For array deletion, we'd need to track the array size and call destructors for each element
			// This is a simplified implementation

			// We need the type index to get struct info
			// For now, we'll skip destructor calls for delete (can be enhanced later)
			// TODO: Track type information through pointer types to enable destructor calls
		}

		// Generate the appropriate free instruction
		// Convert IrOperand to IrValue
		IrValue ptr_value;
		if (std::holds_alternative<unsigned long long>(ptr_operands[2])) {
			ptr_value = std::get<unsigned long long>(ptr_operands[2]);
		} else if (std::holds_alternative<TempVar>(ptr_operands[2])) {
			ptr_value = std::get<TempVar>(ptr_operands[2]);
		} else if (std::holds_alternative<StringHandle>(ptr_operands[2])) {
			ptr_value = std::get<StringHandle>(ptr_operands[2]);
		} else if (std::holds_alternative<double>(ptr_operands[2])) {
			ptr_value = std::get<double>(ptr_operands[2]);
		}

		if (deleteExpr.is_array()) {
			HeapFreeArrayOp op;
			op.pointer = ptr_value;
			ir_.addInstruction(IrInstruction(IrOpcode::HeapFreeArray, std::move(op), Token()));
		} else {
			HeapFreeOp op;
			op.pointer = ptr_value;
			ir_.addInstruction(IrInstruction(IrOpcode::HeapFree, std::move(op), Token()));
		}

		// delete is a statement, not an expression, so return empty
		return {};
	}

	// Helper function to extract base operand from expression operands
	std::variant<StringHandle, TempVar> extractBaseOperand(
		const std::vector<IrOperand>& expr_operands,
		TempVar fallback_var,
		const char* cast_name = "cast") {
		
		std::variant<StringHandle, TempVar> base;
		if (std::holds_alternative<StringHandle>(expr_operands[2])) {
			base = std::get<StringHandle>(expr_operands[2]);
		} else if (std::holds_alternative<TempVar>(expr_operands[2])) {
			base = std::get<TempVar>(expr_operands[2]);
		} else {
			FLASH_LOG_FORMAT(Codegen, Warning, "{}:  unexpected value type in expr_operands[2]", cast_name);
			base = fallback_var;
		}
		return base;
	}

	// Helper function to mark reference with appropriate value category metadata
	void markReferenceMetadata(
		const std::vector<IrOperand>& expr_operands,
		TempVar result_var,
		Type target_type,
		int target_size,
		bool is_rvalue_ref,
		const char* cast_name = "cast") {
		
		auto base = extractBaseOperand(expr_operands, result_var, cast_name);
		LValueInfo lvalue_info(LValueInfo::Kind::Direct, base, 0);
		
		if (is_rvalue_ref) {
			FLASH_LOG_FORMAT(Codegen, Debug, "{} to rvalue reference: marking as xvalue", cast_name);
			setTempVarMetadata(result_var, TempVarMetadata::makeXValue(lvalue_info, target_type, target_size));
		} else {
			FLASH_LOG_FORMAT(Codegen, Debug, "{} to lvalue reference: marking as lvalue", cast_name);
			setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info, target_type, target_size));
		}
	}

	// Helper function to generate AddressOf operation for reference casts
	void generateAddressOfForReference(
		const std::variant<StringHandle, TempVar>& base,
		TempVar result_var,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast") {
		
		if (std::holds_alternative<StringHandle>(base)) {
			AddressOfOp addr_op;
			addr_op.result = result_var;
			addr_op.operand.type = target_type;
			addr_op.operand.size_in_bits = target_size;
			addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
			addr_op.operand.value = std::get<StringHandle>(base);
			ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), token));
		} else {
			// source is TempVar - it already holds an address, copy it to result_var
			FLASH_LOG_FORMAT(Codegen, Debug, "{}: source is TempVar (address already computed), copying to result", cast_name);
			const TempVar& source_var = std::get<TempVar>(base);
			AssignmentOp assign_op;
			assign_op.result = result_var;
			assign_op.lhs = TypedValue{target_type, 64, result_var};  // 64-bit pointer dest
			assign_op.rhs = TypedValue{target_type, 64, source_var};  // 64-bit pointer source
			assign_op.is_pointer_store = false;
			assign_op.dereference_rhs_references = false;  // Don't dereference - just copy the pointer!
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), token));
		}
	}

	// Helper function to handle rvalue reference casts (produces xvalue)
	std::vector<IrOperand> handleRValueReferenceCast(
		const std::vector<IrOperand>& expr_operands,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast") {
		
		// Create a new TempVar to hold the xvalue result
		TempVar result_var = var_counter.next();
		
		// Extract base operand and mark as xvalue
		auto base = extractBaseOperand(expr_operands, result_var, cast_name);
		LValueInfo lvalue_info(LValueInfo::Kind::Direct, base, 0);
		FLASH_LOG_FORMAT(Codegen, Debug, "{} to rvalue reference: marking as xvalue", cast_name);
		setTempVarMetadata(result_var, TempVarMetadata::makeXValue(lvalue_info, target_type, target_size));
		
		// Generate AddressOf operation if needed
		generateAddressOfForReference(base, result_var, target_type, target_size, token, cast_name);
		
		// Return the xvalue with reference semantics (64-bit pointer size)
		return { target_type, 64, result_var, 0ULL };
	}

	// Helper function to handle lvalue reference casts (produces lvalue)
	std::vector<IrOperand> handleLValueReferenceCast(
		const std::vector<IrOperand>& expr_operands,
		Type target_type,
		int target_size,
		const Token& token,
		const char* cast_name = "cast") {
		
		// Create a new TempVar to hold the lvalue result
		TempVar result_var = var_counter.next();
		
		// Extract base operand and mark as lvalue
		auto base = extractBaseOperand(expr_operands, result_var, cast_name);
		LValueInfo lvalue_info(LValueInfo::Kind::Direct, base, 0);
		FLASH_LOG_FORMAT(Codegen, Debug, "{} to lvalue reference", cast_name);
		setTempVarMetadata(result_var, TempVarMetadata::makeLValue(lvalue_info, target_type, target_size));
		
		// Generate AddressOf operation if needed
		generateAddressOfForReference(base, result_var, target_type, target_size, token, cast_name);
		
		// Return the lvalue with reference semantics (64-bit pointer size)
		return { target_type, 64, result_var, 0ULL };
	}

	std::vector<IrOperand> generateStaticCastIr(const StaticCastNode& staticCastNode) {
		// Get the target type from the type specifier first
		const auto& target_type_node = staticCastNode.target_type().as<TypeSpecifierNode>();
		Type target_type = target_type_node.type();
		int target_size = static_cast<int>(target_type_node.size_in_bits());
		size_t target_pointer_depth = target_type_node.pointer_depth();
		
		// For reference casts (both lvalue and rvalue), we need the address of the expression,
		// not its loaded value. Use LValueAddress context to get the address without dereferencing.
		ExpressionContext eval_context = ExpressionContext::Load;
		if (target_type_node.is_reference()) {
			eval_context = ExpressionContext::LValueAddress;
		}
		
		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(staticCastNode.expr().as<ExpressionNode>(), eval_context);

		// Get the source type
		Type source_type = std::get<Type>(expr_operands[0]);
		int source_size = std::get<int>(expr_operands[1]);

		// Special handling for rvalue reference casts: static_cast<T&&>(expr)
		// This produces an xvalue - has identity but can be moved from
		// Equivalent to std::move
		if (target_type_node.is_rvalue_reference()) {
			return handleRValueReferenceCast(expr_operands, target_type, target_size, staticCastNode.cast_token(), "static_cast");
		}

		// Special handling for lvalue reference casts: static_cast<T&>(expr)
		// This produces an lvalue
		if (target_type_node.is_lvalue_reference()) {
			return handleLValueReferenceCast(expr_operands, target_type, target_size, staticCastNode.cast_token(), "static_cast");
		}

		// Special handling for pointer casts (e.g., char* to double*, int* to void*, etc.)
		// Pointer casts should NOT generate type conversions - they're just reinterpretations
		if (target_pointer_depth > 0) {
			// Target is a pointer type - this is a pointer cast, not a value conversion
			// Pointer casts are bitcasts - the value stays the same, only the type changes
			// Return the expression with the target pointer type (char64, int64, etc.)
			// All pointers are 64-bit on x64, so size should be 64
			FLASH_LOG_FORMAT(Codegen, Debug, "[PTR_CAST_DEBUG] Pointer cast: source={}, target={}, target_ptr_depth={}", 
				static_cast<int>(source_type), static_cast<int>(target_type), target_pointer_depth);
			return { target_type, 64, expr_operands[2], 0ULL };
		}

		// For now, static_cast just changes the type metadata
		// The actual value remains the same (this works for enum to int, int to enum, etc.)
		// More complex casts (e.g., pointer casts, numeric conversions) would need additional logic

		// If the types are the same, just return the expression as-is
		if (source_type == target_type && source_size == target_size) {
			return expr_operands;
		}

		// For enum to int or int to enum, we can just change the type
		if ((source_type == Type::Enum && target_type == Type::Int) ||
		    (source_type == Type::Int && target_type == Type::Enum) ||
		    (source_type == Type::Enum && target_type == Type::UnsignedInt) ||
		    (source_type == Type::UnsignedInt && target_type == Type::Enum)) {
			// Return the value with the new type
			return { target_type, target_size, expr_operands[2], 0ULL };
		}

		// For float-to-int conversions, generate FloatToInt IR
		if (is_floating_point_type(source_type) && is_integer_type(target_type)) {
			TempVar result_temp = var_counter.next();
			// Extract IrValue from IrOperand - visitExpressionNode returns [type, size, value]
			// where value is TempVar, string_view, unsigned long long, or double
			IrValue from_value = std::visit([](auto&& arg) -> IrValue {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
				              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
					return arg;
				} else {
					// This shouldn't happen for expression values, but default to 0
					throw std::runtime_error("Couldn't match IrValue to a known type");
					return 0ULL;
				}
			}, expr_operands[2]);
			
			TypeConversionOp op{
				.result = result_temp,
				.from = TypedValue{source_type, source_size, from_value},
				.to_type = target_type,
				.to_size_in_bits = target_size
			};
			ir_.addInstruction(IrOpcode::FloatToInt, std::move(op), staticCastNode.cast_token());
			return { target_type, target_size, result_temp, 0ULL };
		}

		// For int-to-float conversions, generate IntToFloat IR
		if (is_integer_type(source_type) && is_floating_point_type(target_type)) {
			TempVar result_temp = var_counter.next();
			IrValue from_value = std::visit([](auto&& arg) -> IrValue {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
				              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
					return arg;
				} else {
					throw std::runtime_error("Couldn't match IrValue to a known type");
					return 0ULL;
				}
			}, expr_operands[2]);
			
			TypeConversionOp op{
				.result = result_temp,
				.from = TypedValue{source_type, source_size, from_value},
				.to_type = target_type,
				.to_size_in_bits = target_size
			};
			ir_.addInstruction(IrOpcode::IntToFloat, std::move(op), staticCastNode.cast_token());
			return { target_type, target_size, result_temp, 0ULL };
		}

		// For float-to-float conversions (float <-> double), generate FloatToFloat IR
		if (is_floating_point_type(source_type) && is_floating_point_type(target_type) && source_type != target_type) {
			TempVar result_temp = var_counter.next();
			IrValue from_value = std::visit([](auto&& arg) -> IrValue {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
				              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
					return arg;
				} else {
					throw std::runtime_error("Couldn't match IrValue to a known type");
					return 0ULL;
				}
			}, expr_operands[2]);
			
			TypeConversionOp op{
				.result = result_temp,
				.from = TypedValue{source_type, source_size, from_value},
				.to_type = target_type,
				.to_size_in_bits = target_size
			};
			ir_.addInstruction(IrOpcode::FloatToFloat, std::move(op), staticCastNode.cast_token());
			return { target_type, target_size, result_temp, 0ULL };
		}

		// For integer-to-bool conversions, normalize to 0 or 1 via != 0
		// e.g. static_cast<bool>(42) must produce 1, not 42
		if (is_integer_type(source_type) && target_type == Type::Bool) {
			TempVar result_temp = var_counter.next();
			BinaryOp bin_op{
				.lhs = toTypedValue(expr_operands),
				.rhs = TypedValue{source_type, source_size, 0ULL},
				.result = result_temp,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::NotEqual, std::move(bin_op), staticCastNode.cast_token()));
			return { Type::Bool, 8, result_temp, 0ULL };
		}

		// For float-to-bool conversions, normalize to 0 or 1 via != 0.0
		if (is_floating_point_type(source_type) && target_type == Type::Bool) {
			TempVar result_temp = var_counter.next();
			BinaryOp bin_op{
				.lhs = toTypedValue(expr_operands),
				.rhs = TypedValue{source_type, source_size, 0.0},
				.result = result_temp,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::FloatNotEqual, std::move(bin_op), staticCastNode.cast_token()));
			return { Type::Bool, 8, result_temp, 0ULL };
		}

		// For numeric conversions, we might need to generate a conversion instruction
		// For now, just change the type metadata (works for most cases)
		return { target_type, target_size, expr_operands[2], 0ULL };
	}

	std::vector<IrOperand> generateTypeidIr(const TypeidNode& typeidNode) {
		// typeid returns a reference to const std::type_info
		// For polymorphic types, we need to get RTTI from the vtable
		// For non-polymorphic types, we return a compile-time constant

		TempVar result_temp = var_counter.next();

		if (typeidNode.is_type()) {
			// typeid(Type) - compile-time constant
			const auto& type_node = typeidNode.operand().as<TypeSpecifierNode>();

			// Get type information
			StringHandle type_name;
			if (type_node.type() == Type::Struct) {
				TypeIndex type_idx = type_node.type_index();
				if (type_idx < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_idx];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						type_name = struct_info->getName();
					}
				}
			}

			// Generate IR to get compile-time type_info
			TypeidOp op{
				.result = result_temp,
				.operand = type_name,  // Type name for RTTI lookup
				.is_type = true
			};
			ir_.addInstruction(IrOpcode::Typeid, std::move(op), typeidNode.typeid_token());
		}
		else {
			// typeid(expr) - may need runtime lookup for polymorphic types
			auto expr_operands = visitExpressionNode(typeidNode.operand().as<ExpressionNode>());

			// Extract IrValue from expression result
			std::variant<StringHandle, TempVar> operand_value;
			if (std::holds_alternative<TempVar>(expr_operands[2])) {
				operand_value = std::get<TempVar>(expr_operands[2]);
			} else if (std::holds_alternative<StringHandle>(expr_operands[2])) {
				operand_value = std::get<StringHandle>(expr_operands[2]);
			} else {
				// Shouldn't happen - typeid operand should be a variable
				operand_value = TempVar{0};
			}

			TypeidOp op{
				.result = result_temp,
				.operand = operand_value,  // Expression result
				.is_type = false
			};
			ir_.addInstruction(IrOpcode::Typeid, std::move(op), typeidNode.typeid_token());
		}

		// Return pointer to type_info (64-bit pointer)
		// Use void* type for now (Type::Void with pointer depth)
		return { Type::Void, 64, result_temp, 0ULL };
	}

	std::vector<IrOperand> generateDynamicCastIr(const DynamicCastNode& dynamicCastNode) {
		// dynamic_cast<Type>(expr) performs runtime type checking
		// Returns nullptr (for pointers) or throws bad_cast (for references) on failure

		// Get the target type first to determine evaluation context
		const auto& target_type_node = dynamicCastNode.target_type().as<TypeSpecifierNode>();

		// For reference casts (both lvalue and rvalue), we need the address of the expression,
		// not its loaded value. Use LValueAddress context to get the address without dereferencing.
		ExpressionContext eval_context = ExpressionContext::Load;
		if (target_type_node.is_reference()) {
			eval_context = ExpressionContext::LValueAddress;
		}

		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(dynamicCastNode.expr().as<ExpressionNode>(), eval_context);

		// Get target struct type information
		std::string target_type_name;
		if (target_type_node.type() == Type::Struct) {
			TypeIndex type_idx = target_type_node.type_index();
			if (type_idx < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_idx];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					target_type_name = StringTable::getStringView(struct_info->getName());
				}
			}
		}

		TempVar result_temp = var_counter.next();

		// Extract source pointer from expression result
		TempVar source_ptr;
		if (std::holds_alternative<TempVar>(expr_operands[2])) {
			source_ptr = std::get<TempVar>(expr_operands[2]);
		} else if (std::holds_alternative<StringHandle>(expr_operands[2])) {
			// For a named variable, load it into a temp first
			source_ptr = var_counter.next();
			StringHandle var_name_handle = std::get<StringHandle>(expr_operands[2]);
			
			// Generate assignment to load the variable into the temp
			AssignmentOp load_op;
			load_op.result = source_ptr;
			load_op.lhs = TypedValue{std::get<Type>(expr_operands[0]), std::get<int>(expr_operands[1]), source_ptr};
			load_op.rhs = TypedValue{std::get<Type>(expr_operands[0]), std::get<int>(expr_operands[1]), var_name_handle};
			ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(load_op), dynamicCastNode.cast_token()));
		} else {
			source_ptr = TempVar{0};
		}

		// Generate dynamic_cast IR
		DynamicCastOp op{
			.result = result_temp,
			.source = source_ptr,
			.target_type_name = target_type_name,
			.is_reference = target_type_node.is_reference()
		};
		ir_.addInstruction(IrOpcode::DynamicCast, std::move(op), dynamicCastNode.cast_token());

		// Get result type and size for metadata and return value
		Type result_type = target_type_node.type();
		int result_size = static_cast<int>(target_type_node.size_in_bits());

		// For reference types, the result is a pointer (64 bits), not the struct size
		bool is_reference_cast = target_type_node.is_reference() || target_type_node.is_rvalue_reference();
		if (is_reference_cast) {
			result_size = 64;  // Reference is represented as a pointer
		}

		// Mark value category for reference types
		if (target_type_node.is_rvalue_reference()) {
			markReferenceMetadata(expr_operands, result_temp, result_type, result_size, true, "dynamic_cast");
		} else if (target_type_node.is_lvalue_reference()) {
			markReferenceMetadata(expr_operands, result_temp, result_type, result_size, false, "dynamic_cast");
		}

		// Return the casted pointer/reference
		return { result_type, result_size, result_temp, 0ULL };
	}

	std::vector<IrOperand> generateConstCastIr(const ConstCastNode& constCastNode) {
		// const_cast<Type>(expr) adds or removes const/volatile qualifiers
		// It doesn't change the actual value, just the type metadata
		
		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(constCastNode.expr().as<ExpressionNode>());
		
		// Get the target type from the type specifier
		const auto& target_type_node = constCastNode.target_type().as<TypeSpecifierNode>();
		Type target_type = target_type_node.type();
		int target_size = static_cast<int>(target_type_node.size_in_bits());
		
		// Special handling for rvalue reference casts: const_cast<T&&>(expr)
		if (target_type_node.is_rvalue_reference()) {
			return handleRValueReferenceCast(expr_operands, target_type, target_size, constCastNode.cast_token(), "const_cast");
		}
		
		// Special handling for lvalue reference casts: const_cast<T&>(expr)
		if (target_type_node.is_lvalue_reference()) {
			return handleLValueReferenceCast(expr_operands, target_type, target_size, constCastNode.cast_token(), "const_cast");
		}
		
		// const_cast doesn't modify the value, only the type's const/volatile qualifiers
		// For code generation purposes, we just return the expression with the new type metadata
		// The actual value/address remains the same
		return { target_type, target_size, expr_operands[2], 0ULL };
	}

	std::vector<IrOperand> generateReinterpretCastIr(const ReinterpretCastNode& reinterpretCastNode) {
		// reinterpret_cast<Type>(expr) reinterprets the bit pattern as a different type
		// It doesn't change the actual bits, just the type interpretation
		
		// Evaluate the expression to cast
		auto expr_operands = visitExpressionNode(reinterpretCastNode.expr().as<ExpressionNode>());
		
		// Get the target type from the type specifier
		const auto& target_type_node = reinterpretCastNode.target_type().as<TypeSpecifierNode>();
		Type target_type = target_type_node.type();
		int target_size = static_cast<int>(target_type_node.size_in_bits());
		int target_pointer_depth = target_type_node.pointer_depth();
		
		// Special handling for rvalue reference casts: reinterpret_cast<T&&>(expr)
		if (target_type_node.is_rvalue_reference()) {
			return handleRValueReferenceCast(expr_operands, target_type, target_size, reinterpretCastNode.cast_token(), "reinterpret_cast");
		}
		
		// Special handling for lvalue reference casts: reinterpret_cast<T&>(expr)
		if (target_type_node.is_lvalue_reference()) {
			return handleLValueReferenceCast(expr_operands, target_type, target_size, reinterpretCastNode.cast_token(), "reinterpret_cast");
		}
		
		// reinterpret_cast reinterprets the bits without conversion
		// For code generation purposes, we just return the expression with the new type metadata
		// The actual bit pattern remains unchanged
		int result_size = (target_pointer_depth > 0) ? 64 : target_size;
		return { target_type, result_size, expr_operands[2], static_cast<unsigned long long>(target_pointer_depth) };
	}

	// Structure to track variables that need destructors called
	struct ScopeVariableInfo {
		std::string variable_name;
		std::string struct_name;
	};

	// Stack of scopes, each containing variables that need destructors
	std::vector<std::vector<ScopeVariableInfo>> scope_stack_;

	void enterScope() {
		scope_stack_.push_back({});
	}

	void exitScope() {
		if (!scope_stack_.empty()) {
			// Generate destructor calls for all variables in this scope (in reverse order)
			const auto& scope_vars = scope_stack_.back();
			for (auto it = scope_vars.rbegin(); it != scope_vars.rend(); ++it) {
				// Generate destructor call
				DestructorCallOp dtor_op;
				dtor_op.struct_name = StringTable::getOrInternStringHandle(it->struct_name);
				dtor_op.object = StringTable::getOrInternStringHandle(it->variable_name);
				ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), Token()));
			}
			scope_stack_.pop_back();
		}
	}

	void registerVariableWithDestructor(const std::string& var_name, const std::string& struct_name) {
		if (!scope_stack_.empty()) {
			scope_stack_.back().push_back({var_name, struct_name});
		}
	}
