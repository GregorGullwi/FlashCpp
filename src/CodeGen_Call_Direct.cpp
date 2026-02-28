	std::vector<IrOperand> generateFunctionCallIr(const FunctionCallNode& functionCallNode) {
		std::vector<IrOperand> irOperands;
		irOperands.reserve(2 + functionCallNode.arguments().size() * 4);  // ret_var + name + ~4 operands per arg

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
									Type operand_type = Type::Int;  // Default
									int operand_size = 32;
									if (const DeclarationNode* decl = lookupDeclaration(id_handle)) {
										const TypeSpecifierNode& type = decl->type_node().as<TypeSpecifierNode>();
										operand_type = type.type();
										operand_size = static_cast<int>(type.size_in_bits());
										if (operand_size == 0) operand_size = get_type_size_bits(operand_type);
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
							self_type.set_reference_qualifier(ReferenceQualifier::RValueReference);
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
		
		// Helper: resolve mangled name from a matched function declaration
		auto resolveMangledName = [&](const FunctionDeclarationNode* func_decl, std::string_view struct_name = "") {
			if (!has_precomputed_mangled) {
				if (func_decl->has_mangled_name()) {
					function_name = func_decl->mangled_name();
				} else if (func_decl->linkage() != Linkage::C) {
					function_name = struct_name.empty()
						? generateMangledNameForCall(*func_decl, "", current_namespace_stack_)
						: generateMangledNameForCall(*func_decl, struct_name);
				}
			}
		};

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
					resolveMangledName(matched_func_decl);
					FLASH_LOG_FORMAT(Codegen, Debug, "Matched overload, function_name: {}", function_name);
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
	
			resolveMangledName(matched_func_decl);
		}

		// Additional fallback: check gSymbolTable directly (for member functions added during delayed parsing)
		if (!matched_func_decl && gSymbolTable_overloads.size() == 1 &&
		    (gSymbolTable_overloads[0].is<FunctionDeclarationNode>() || gSymbolTable_overloads[0].is<TemplateFunctionDeclarationNode>())) {
			matched_func_decl = gSymbolTable_overloads[0].is<FunctionDeclarationNode>()
				? &gSymbolTable_overloads[0].as<FunctionDeclarationNode>()
				: &gSymbolTable_overloads[0].as<TemplateFunctionDeclarationNode>().function_decl_node();
	
			resolveMangledName(matched_func_decl);
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
								resolveMangledName(matched_func_decl, StringTable::getStringView(current_struct_name_));
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
													resolveMangledName(matched_func_decl, StringTable::getStringView(base_struct_info->getName()));
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
							resolveMangledName(matched_func_decl, parent_for_mangling);
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
			// Direct lookup: if the struct qualifier is directly in gTypesByName (e.g., "Mid$hash::get"),
			// find it immediately rather than only checking base classes.
			if (scope_pos != std::string_view::npos && !matched_func_decl) {
				std::string_view struct_part = func_name_view.substr(0, scope_pos);
				std::string_view member_name_direct = func_name_view.substr(scope_pos + 2);
				auto direct_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_part));
				if (direct_it != gTypesByName.end() && direct_it->second->isStruct()) {
					const StructTypeInfo* si = direct_it->second->getStructInfo();
					if (si) {
						// Count expected parameters for overload disambiguation
						size_t direct_expected_param_count = 0;
						functionCallNode.arguments().visit([&](ASTNode) { ++direct_expected_param_count; });
						for (const auto& mf : si->member_functions) {
							if (mf.function_decl.is<FunctionDeclarationNode>()) {
								const auto& fd = mf.function_decl.as<FunctionDeclarationNode>();
								if (fd.decl_node().identifier_token().value() == member_name_direct
								    && fd.parameter_nodes().size() == direct_expected_param_count) {
									matched_func_decl = &fd;
									resolveMangledName(matched_func_decl, struct_part);
									// Queue all member functions of this struct for deferred generation
									std::vector<std::string> ns_stack;
									auto parse_ns = [&](std::string_view qualified_name) {
										size_t ns_end = qualified_name.rfind("::");
										if (ns_end == std::string_view::npos) return;
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
									parse_ns(struct_part);
									if (ns_stack.empty()) {
										parse_ns(StringTable::getStringView(direct_it->second->name()));
									}
									for (const auto& dmf : si->member_functions) {
										DeferredMemberFunctionInfo deferred_info;
										deferred_info.struct_name = direct_it->second->name();
										deferred_info.function_node = dmf.function_decl;
										deferred_info.namespace_stack = ns_stack;
										deferred_member_functions_.push_back(std::move(deferred_info));
									}
									break;
								}
							}
						}
					}
				}
			}
			if (!matched_func_decl && !base_template_name.empty() && scope_pos != std::string_view::npos) {
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
														resolveMangledName(matched_func_decl, StringTable::getStringView(base_struct_info->getName()));
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
			auto local_func_symbol = lookupSymbol(func_decl_node.identifier_token().value());
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
				const DeclarationNode* decl_ptr = lookupDeclaration(identifier.name());
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
									TempVar this_ptr = emitAddressOf(arg_type, arg_size, IrValue(std::get<StringHandle>(source_value)));
									
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
				std::optional<ASTNode> symbol = lookupSymbol(identifier.name());
				if (!symbol.has_value()) {
					FLASH_LOG(Codegen, Error, "Symbol '", identifier.name(), "' not found for function argument");
					FLASH_LOG(Codegen, Error, "  Current function: ", current_function_name_);
					throw std::runtime_error("Missing symbol for function argument");
				}
				const DeclarationNode* decl_ptr = get_decl_from_symbol(*symbol);
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
					// Generate AddressOf IR instruction to get the address of the array
					TempVar addr_var = emitAddressOf(type_node.type(), static_cast<int>(type_node.size_in_bits()), IrValue(StringTable::getOrInternStringHandle(identifier.name())));

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
						TempVar addr_var = emitAddressOf(type_node.type(), static_cast<int>(type_node.size_in_bits()), IrValue(StringTable::getOrInternStringHandle(identifier.name())));

						// Pass the address
						irOperands.emplace_back(type_node.type());
						irOperands.emplace_back(64);  // Pointer size
						irOperands.emplace_back(addr_var);
					}
				} else if (type_node.is_reference() || type_node.is_rvalue_reference()) {
					// Argument is a reference but parameter expects a value - dereference
					TempVar deref_var = emitDereference(type_node.type(), 64, 1,
						StringTable::getOrInternStringHandle(identifier.name()));
					
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
						TempVar addr_var = emitAddressOf(literal_type, literal_size, IrValue(temp_var));
						
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
								TempVar addr_var = emitAddressOf(expr_type, expr_size, IrValue(expr_var));
								
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

