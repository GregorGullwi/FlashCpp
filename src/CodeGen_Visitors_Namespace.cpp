#include "CodeGen.h"

	void AstToIr::visitNamespaceDeclarationNode(const NamespaceDeclarationNode& node) {
		// Namespace declarations themselves don't generate IR - they just provide scope
		// Track the current namespace for proper name mangling
		// For anonymous namespaces, push empty string which will be handled specially by mangling
		current_namespace_stack_.push_back(std::string(node.name()));
		
		// Visit all declarations within the namespace
		for (const auto& decl : node.declarations()) {
			visit(decl);
		}
		
		// Pop the namespace from the stack
		current_namespace_stack_.pop_back();
	}

	void AstToIr::visitUsingDirectiveNode(const UsingDirectiveNode& node) {
		// Using directives don't generate IR - they affect name lookup in the symbol table
		// Add the namespace to the current scope's using directives in the local symbol table
		// (not gSymbolTable, which is the parser's symbol table and has different scope management)
		symbol_table.add_using_directive(node.namespace_handle());
	}

	void AstToIr::visitUsingDeclarationNode(const UsingDeclarationNode& node) {
		// Using declarations don't generate IR - they import a specific name into the current scope
		// Add the using declaration to the local symbol table (not gSymbolTable)
		FLASH_LOG(Codegen, Debug, "Adding using declaration: ", node.identifier_name(), " from namespace handle=", node.namespace_handle().index);
		symbol_table.add_using_declaration(
			node.identifier_name(),
			node.namespace_handle(),
			node.identifier_name()
		);
	}

	void AstToIr::visitUsingEnumNode(const UsingEnumNode& node) {
		// C++20 using enum - brings all enumerators of a scoped enum into the current scope
		// Look up the enum type and add all enumerators to the local symbol table
		StringHandle enum_name = node.enum_type_name();
		
		auto type_it = gTypesByName.find(enum_name);
		if (type_it != gTypesByName.end() && type_it->second->getEnumInfo()) {
			const EnumTypeInfo* enum_info = type_it->second->getEnumInfo();
			TypeIndex enum_type_index = type_it->second->type_index_;
			
			// Add each enumerator to the local symbol table
			for (const auto& enumerator : enum_info->enumerators) {
				// Create a type node for the enum type
				Token enum_type_token(Token::Type::Identifier, 
					StringTable::getStringView(enum_name), 0, 0, 0);
				auto enum_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
					Type::Enum, enum_type_index, enum_info->underlying_size, enum_type_token);
				
				// Create a declaration node for the enumerator
				Token enumerator_token(Token::Type::Identifier, 
					StringTable::getStringView(enumerator.getName()), 0, 0, 0);
				auto enumerator_decl = ASTNode::emplace_node<DeclarationNode>(enum_type_node, enumerator_token);
				
				// Insert into local symbol table
				symbol_table.insert(StringTable::getStringView(enumerator.getName()), enumerator_decl);
			}
			
			FLASH_LOG(Codegen, Debug, "Using enum '", StringTable::getStringView(enum_name), 
				"' - added ", enum_info->enumerators.size(), " enumerators to local scope");
		} else {
			FLASH_LOG(General, Error, "Enum type '", StringTable::getStringView(enum_name), 
				"' not found for 'using enum' declaration");
		}
	}

	void AstToIr::visitNamespaceAliasNode(const NamespaceAliasNode& node) {
		// Namespace aliases don't generate IR - they create an alias for a namespace
		// Add the alias to the local symbol table (not gSymbolTable)
		symbol_table.add_namespace_alias(node.alias_name(), node.target_namespace());
	}

	void AstToIr::visitReturnStatementNode(const ReturnStatementNode& node) {
		if (node.expression()) {
			const auto& expr_opt = node.expression();
			
			// Handle InitializerListNode for braced initializers in return statements
			if (expr_opt->is<InitializerListNode>()) {
				// Create a temporary variable to hold the initialized struct
				TempVar temp_var = var_counter.next();
				
				// Generate initialization code similar to variable declarations
				const InitializerListNode& init_list = expr_opt->as<InitializerListNode>();
				
				// Get struct type information
				Type return_type = current_function_return_type_;
				int return_size = current_function_return_size_;
				
				if (return_type != Type::Struct) {
					FLASH_LOG(Codegen, Error, "InitializerListNode in return statement for non-struct type");
					return;
				}
				
				// Find the struct info
				const StructTypeInfo* struct_info = nullptr;
				
				// Look up the struct by return type index or name
				for (size_t i = 0; i < gTypeInfo.size(); ++i) {
					if (gTypeInfo[i].struct_info_ &&
					    static_cast<int>(gTypeInfo[i].struct_info_->total_size * 8) == return_size) {
						struct_info = gTypeInfo[i].struct_info_.get();
						break;
					}
				}
				
				if (!struct_info) {
					FLASH_LOG(Codegen, Error, "Could not find struct type info for return type");
					return;
				}
				
				// Process initializer list to generate member stores
				const auto& initializers = init_list.initializers();
				std::unordered_map<StringHandle, const ASTNode*> member_values;
				size_t positional_index = 0;
				
				for (size_t i = 0; i < initializers.size(); ++i) {
					if (init_list.is_designated(i)) {
						// Designated initializer - use member name
						StringHandle member_name = init_list.member_name(i);
						member_values[member_name] = &initializers[i];
					} else {
						// Positional initializer - map to member by index
						if (positional_index < struct_info->members.size()) {
							StringHandle member_name = struct_info->members[positional_index].getName();
							member_values[member_name] = &initializers[i];
							positional_index++;
						}
					}
				}
				
				// Generate member stores for each initialized member
				for (const StructMember& member : struct_info->members) {
					StringHandle member_name_handle = member.getName();
					auto it = member_values.find(member_name_handle);
					
					if (it != member_values.end()) {
						// Evaluate the initializer expression
						const ASTNode* init_expr = it->second;
						if (init_expr->is<ExpressionNode>()) {
							auto init_operands = visitExpressionNode(init_expr->as<ExpressionNode>());
							
							if (init_operands.size() >= 3) {
								// Generate member store
								MemberStoreOp store_op;
								store_op.object = temp_var;
								store_op.member_name = member.getName();
								store_op.offset = static_cast<int>(member.offset);
								
								// Create TypedValue from operands
								store_op.value = toTypedValue(init_operands);
								store_op.is_reference = false;
								
								ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_op), node.return_token()));
							}
						}
					}
				}
				
				// Call any enclosing __finally funclets before returning
				emitSehFinallyCallsBeforeReturn(node.return_token());

				// Now return the temporary variable
				emitReturn(temp_var, return_type, return_size, node.return_token());
				return;
			}
			
			// Original handling for ExpressionNode
			assert(expr_opt->is<ExpressionNode>());
			
			// Set flag if we should use RVO (returning struct by value with hidden return param)
			if (current_function_has_hidden_return_param_) {
				in_return_statement_with_rvo_ = true;
			}
			
			// Fast path: reference return of '*this' can directly return the this pointer
			if (current_function_returns_reference_ && expr_opt->is<ExpressionNode>()) {
				const auto& ret_expr = expr_opt->as<ExpressionNode>();
				if (std::holds_alternative<UnaryOperatorNode>(ret_expr)) {
					const auto& unary = std::get<UnaryOperatorNode>(ret_expr);
					if (unary.op() == "*" && unary.get_operand().is<ExpressionNode>()) {
						const auto& operand_expr = unary.get_operand().as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(operand_expr)) {
							const auto& ident = std::get<IdentifierNode>(operand_expr);
							if (ident.name() == "this") {
								emitSehFinallyCallsBeforeReturn(node.return_token());
								emitReturn(StringTable::getOrInternStringHandle("this"),
								           current_function_return_type_, current_function_return_size_,
								           node.return_token());
								return;
							}
						}
					}
				}
			}

			// For reference return types, use LValueAddress context to get the address instead of the value
			// This ensures "return *this" returns the address (this pointer), not the dereferenced value
			ExpressionContext return_context = current_function_returns_reference_ 
				? ExpressionContext::LValueAddress 
				: ExpressionContext::Load;
			auto operands = visitExpressionNode(expr_opt->as<ExpressionNode>(), return_context);
			
			// Clear the RVO flag after evaluation
			in_return_statement_with_rvo_ = false;
			
			// Check if this is a void return with a void expression (e.g., return void_func();)
			if (!operands.empty() && operands.size() >= 1) {
				Type expr_type = std::get<Type>(operands[0]);
				
				// If returning a void expression in a void function, just emit void return
				// (the expression was already evaluated for its side effects)
				if (expr_type == Type::Void && current_function_return_type_ == Type::Void) {
					emitSehFinallyCallsBeforeReturn(node.return_token());
					emitVoidReturn(node.return_token());
					return;
				}
			}
			
			// If the current function has auto return type, deduce it from the return expression
			if (current_function_return_type_ == Type::Auto && !operands.empty() && operands.size() >= 2) {
				Type expr_type = std::get<Type>(operands[0]);
				int expr_size = std::get<int>(operands[1]);
				
				// Build a TypeSpecifierNode for the deduced type
				TypeSpecifierNode deduced_type(expr_type, TypeQualifier::None, expr_size, node.return_token());
				
				// If we have type_index information (for structs), include it
				if (operands.size() >= 4) {
					if (std::holds_alternative<unsigned long long>(operands[3])) {
						TypeIndex type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operands[3]));
						deduced_type = TypeSpecifierNode(expr_type, TypeQualifier::None, expr_size, node.return_token());
						deduced_type.set_type_index(type_index);
					}
				}
				
				// Store the deduced type for this function
				if (current_function_name_.isValid()) {
					deduced_auto_return_types_[std::string(StringTable::getStringView(current_function_name_))] = deduced_type;
				}
				
				// Update current function return type for subsequent return statements
				current_function_return_type_ = expr_type;
				current_function_return_size_ = expr_size;
			}
			
			// Convert to the function's return type if necessary
			// Skip type conversion for reference returns - the expression already has the correct representation
			if (!current_function_returns_reference_ && !operands.empty() && operands.size() >= 2) {
				Type expr_type = std::get<Type>(operands[0]);
				int expr_size = std::get<int>(operands[1]);
		
				// Get the current function's return type
				Type return_type = current_function_return_type_;
				int return_size = current_function_return_size_;
		
				// Convert if types don't match
				if (expr_type != return_type || expr_size != return_size) {
					// Check for user-defined conversion operator
					// If expr is a struct type with a conversion operator to return_type, call it
					if (expr_type == Type::Struct && operands.size() >= 4) {
						TypeIndex expr_type_index = 0;
						if (std::holds_alternative<unsigned long long>(operands[3])) {
							expr_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operands[3]));
						}
						
						if (expr_type_index > 0 && expr_type_index < gTypeInfo.size()) {
							const TypeInfo& source_type_info = gTypeInfo[expr_type_index];
							const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
							
							// Look for a conversion operator to the return type
							const StructMemberFunction* conv_op = findConversionOperator(
								source_struct_info, return_type, 0);
							
							if (conv_op) {
								FLASH_LOG(Codegen, Debug, "Found conversion operator in return statement from ", 
									StringTable::getStringView(source_type_info.name()), 
									" to return type");
								
								// Generate call to the conversion operator
								TempVar result_var = var_counter.next();
								
								// Get the source variable value
								IrValue source_value = std::visit([](auto&& arg) -> IrValue {
									using T = std::decay_t<decltype(arg)>;
									if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
									              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
										return arg;
									} else {
										return 0ULL;
									}
								}, operands[2]);
								
								// Build the mangled name for the conversion operator
								StringHandle struct_name_handle = source_type_info.name();
								std::string_view struct_name = StringTable::getStringView(struct_name_handle);
								
								// Generate the call using CallOp (member function call)
								if (conv_op->function_decl.is<FunctionDeclarationNode>()) {
									const auto& func_decl = conv_op->function_decl.as<FunctionDeclarationNode>();
									std::string_view mangled_name;
									if (func_decl.has_mangled_name()) {
										mangled_name = func_decl.mangled_name();
									} else {
										// Generate mangled name for the conversion operator
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
									call_op.return_type = return_type;
									call_op.return_size_in_bits = return_size;
									call_op.return_type_index = (return_type == Type::Struct) ? current_function_return_type_index_ : 0;
									call_op.is_member_function = true;
									call_op.is_variadic = false;
									
									// For member function calls, first argument is 'this' pointer
									if (std::holds_alternative<StringHandle>(source_value)) {
										// It's a variable - take its address
										TempVar this_ptr = var_counter.next();
										AddressOfOp addr_op;
										addr_op.result = this_ptr;
										addr_op.operand.type = expr_type;
										addr_op.operand.size_in_bits = expr_size;
										addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
										addr_op.operand.value = std::get<StringHandle>(source_value);
										ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
										
										// Add 'this' as first argument
										TypedValue this_arg;
										this_arg.type = expr_type;
										this_arg.size_in_bits = 64;  // Pointer size
										this_arg.value = this_ptr;
										this_arg.type_index = expr_type_index;
										call_op.args.push_back(std::move(this_arg));
									} else if (std::holds_alternative<TempVar>(source_value)) {
										// It's already a temporary
										// ASSUMPTION: For struct types, TempVars at this point
										// represent the address of the object (not the object value itself).
										TypedValue this_arg;
										this_arg.type = expr_type;
										this_arg.size_in_bits = 64;  // Pointer size for 'this'
										this_arg.value = std::get<TempVar>(source_value);
										this_arg.type_index = expr_type_index;
										call_op.args.push_back(std::move(this_arg));
									}
									
									ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), node.return_token()));
									
									// Replace operands with the result of the conversion
									operands.clear();
									operands.emplace_back(return_type);
									operands.emplace_back(return_size);
									operands.emplace_back(result_var);
								}
							} else {
								// No conversion operator found - fall back to generateTypeConversion
								operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
							}
						} else {
							// No valid type_index - fall back to generateTypeConversion
							operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
						}
					} else {
						// Not a struct type - use standard type conversion
						operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
					}
				}
			}
			
			// Call any enclosing __finally funclets before returning
			emitSehFinallyCallsBeforeReturn(node.return_token());

			// Check if operands has at least 3 elements before accessing
			if (operands.size() < 3) {
				FLASH_LOG(Codegen, Error, "Return statement: expression evaluation failed or returned insufficient operands");
				return;
			}
			
			// Extract IrValue from operand[2]
			IrValue return_value;
			if (std::holds_alternative<unsigned long long>(operands[2])) {
				return_value = std::get<unsigned long long>(operands[2]);
			} else if (std::holds_alternative<TempVar>(operands[2])) {
				TempVar return_temp = std::get<TempVar>(operands[2]);
				return_value = return_temp;
				
				// C++17 mandatory copy elision: Check if this is a prvalue (e.g., constructor call result)
				// being returned - prvalues used to initialize objects of the same type must have copies elided
				if (isTempVarRVOEligible(return_temp)) {
					FLASH_LOG_FORMAT(Codegen, Debug,
						"RVO opportunity detected: returning prvalue {} (constructor call result)",
						return_temp.name());
				}
				
				// Mark the temp as a return value for potential NRVO analysis
				markTempVarAsReturnValue(return_temp);
			} else if (std::holds_alternative<StringHandle>(operands[2])) {
				return_value = std::get<StringHandle>(operands[2]);
			} else if (std::holds_alternative<double>(operands[2])) {
				return_value = std::get<double>(operands[2]);
			}
			// Use the function's return type, not the expression type
			emitReturn(return_value, current_function_return_type_, current_function_return_size_,
			           node.return_token());
		}
		else {
			// Call any enclosing __finally funclets before returning
			emitSehFinallyCallsBeforeReturn(node.return_token());
			emitVoidReturn(node.return_token());
		}
	}

