class Evaluator {
public:
	// Main evaluation entry point
	// Evaluates a constant expression and returns the result
	static EvalResult evaluate(const ASTNode& expr_node, EvaluationContext& context) {
		// Check complexity limit
		if (++context.step_count > context.max_steps) {
			return EvalResult::error("Constexpr evaluation exceeded complexity limit (infinite loop?)");
		}

		// Evaluate a constant expression
		// Returns the result or an error if not a constant expression

		// The expr_node should be an ExpressionNode variant
		if (!expr_node.is<ExpressionNode>()) {
			return EvalResult::error("AST node is not an expression");
		}

		const ExpressionNode& expr = expr_node.as<ExpressionNode>();
		
		// Debug logging - show what type of expression we're evaluating
		FLASH_LOG(ConstExpr, Trace, "ConstExpr::evaluate: expr index=", expr.index());

		// Check what type of expression it is
		if (std::holds_alternative<BoolLiteralNode>(expr)) {
			return EvalResult::from_bool(std::get<BoolLiteralNode>(expr).value());
		}

		if (std::holds_alternative<NumericLiteralNode>(expr)) {
			return evaluate_numeric_literal(std::get<NumericLiteralNode>(expr));
		}

		// For BinaryOperatorNode, we need to check if it's in the variant
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const auto& bin_op = std::get<BinaryOperatorNode>(expr);
			return evaluate_binary_operator(bin_op.get_lhs(), bin_op.get_rhs(), bin_op.op(), context);
		}

		// For UnaryOperatorNode
		if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const auto& unary_op = std::get<UnaryOperatorNode>(expr);
			return evaluate_unary_operator(unary_op.get_operand(), unary_op.op(), context);
		}

		// For SizeofExprNode
		if (std::holds_alternative<SizeofExprNode>(expr)) {
			return evaluate_sizeof(std::get<SizeofExprNode>(expr), context);
		}
		
		// For SizeofPackNode (sizeof... operator)
		if (std::holds_alternative<SizeofPackNode>(expr)) {
			const auto& sizeof_pack = std::get<SizeofPackNode>(expr);
			std::string_view pack_name = sizeof_pack.pack_name();
			
			// Try to get pack size from the parser's pack parameter info
			if (context.parser) {
				auto pack_size = context.parser->get_pack_size(pack_name);
				if (pack_size.has_value()) {
					return EvalResult::from_int(static_cast<long long>(*pack_size));
				}
				// Also check class template pack context
				auto class_pack_size = context.parser->get_class_template_pack_size(pack_name);
				if (class_pack_size.has_value()) {
					return EvalResult::from_int(static_cast<long long>(*class_pack_size));
				}
				return EvalResult::error("sizeof... requires template instantiation context for pack: " + std::string(pack_name), EvalErrorType::TemplateDependentExpression);
			}
			
			return EvalResult::error("sizeof... operator requires template context");
		}

		// For AlignofExprNode
		if (std::holds_alternative<AlignofExprNode>(expr)) {
			return evaluate_alignof(std::get<AlignofExprNode>(expr), context);
		}

		// For ConstructorCallNode (type conversions like float(3.14), int(100))
		if (std::holds_alternative<ConstructorCallNode>(expr)) {
			return evaluate_constructor_call(std::get<ConstructorCallNode>(expr), context);
		}

		// For IdentifierNode (variable references like 'x' in 'constexpr int y = x + 1;')
		if (std::holds_alternative<IdentifierNode>(expr)) {
			return evaluate_identifier(std::get<IdentifierNode>(expr), context);
		}

		// For TemplateParameterReferenceNode (references to template parameters like 'T' or 'N')
		if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
			const auto& template_param = std::get<TemplateParameterReferenceNode>(expr);
			// Template parameters cannot be evaluated at template definition time
			// This is a template-dependent expression that needs to be deferred
			return EvalResult::error("Template parameter in constant expression: " + 
			                         std::string(StringTable::getStringView(template_param.param_name())),
			                         EvalErrorType::TemplateDependentExpression);
		}

		// For TernaryOperatorNode (condition ? true_expr : false_expr)
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			return evaluate_ternary_operator(std::get<TernaryOperatorNode>(expr), context);
		}

		// For FunctionCallNode (constexpr function calls)
		if (std::holds_alternative<FunctionCallNode>(expr)) {
			return evaluate_function_call(std::get<FunctionCallNode>(expr), context);
		}

		// For QualifiedIdentifierNode (e.g., Template<T>::member)
		if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
			return evaluate_qualified_identifier(std::get<QualifiedIdentifierNode>(expr), context);
		}

		// For MemberAccessNode (e.g., obj.member or ptr->member)
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			return evaluate_member_access(std::get<MemberAccessNode>(expr), context);
		}

		// For MemberFunctionCallNode (e.g., obj.method() in constexpr context)
		if (std::holds_alternative<MemberFunctionCallNode>(expr)) {
			return evaluate_member_function_call(std::get<MemberFunctionCallNode>(expr), context);
		}

		// For StaticCastNode (static_cast<Type>(expr) and C-style casts)
		if (std::holds_alternative<StaticCastNode>(expr)) {
			return evaluate_static_cast(std::get<StaticCastNode>(expr), context);
		}

		// For ArraySubscriptNode (e.g., arr[0] or obj.data[1])
		if (std::holds_alternative<ArraySubscriptNode>(expr)) {
			return evaluate_array_subscript(std::get<ArraySubscriptNode>(expr), context);
		}

		// For TypeTraitExprNode (e.g., __is_void(int), __is_constant_evaluated())
		if (std::holds_alternative<TypeTraitExprNode>(expr)) {
			return evaluate_type_trait(std::get<TypeTraitExprNode>(expr));
		}

		// For FoldExpressionNode (e.g., (args && ...))
		// Fold expressions depend on template parameter packs and must be evaluated during template instantiation
		if (std::holds_alternative<FoldExpressionNode>(expr)) {
			return EvalResult::error("Fold expression requires template instantiation context",
			                         EvalErrorType::TemplateDependentExpression);
		}

		// For PackExpansionExprNode (e.g., args...)
		// Pack expansions depend on template parameter packs and must be evaluated during template instantiation
		if (std::holds_alternative<PackExpansionExprNode>(expr)) {
			return EvalResult::error("Pack expansion requires template instantiation context",
			                         EvalErrorType::TemplateDependentExpression);
		}

		// Other expression types are not supported as constant expressions yet
		return EvalResult::error("Expression type not supported in constant expressions");
	}

private:
	// Internal evaluation methods for different node types
	static EvalResult evaluate_numeric_literal(const NumericLiteralNode& literal) {
		const auto& value = literal.value();

		if (std::holds_alternative<unsigned long long>(value)) {
			unsigned long long val = std::get<unsigned long long>(value);
			return EvalResult::from_uint(val);
		} else if (std::holds_alternative<double>(value)) {
			double val = std::get<double>(value);
			return EvalResult::from_double(val);
		}

		return EvalResult::error("Unknown numeric literal type");
	}

	static EvalResult evaluate_binary_operator(const ASTNode& lhs_node, const ASTNode& rhs_node, 
	                                            std::string_view op, EvaluationContext& context) {
		// Recursively evaluate left and right operands
		auto lhs_result = evaluate(lhs_node, context);
		auto rhs_result = evaluate(rhs_node, context);

		if (!lhs_result.success()) {
			return lhs_result;
		}
		if (!rhs_result.success()) {
			return rhs_result;
		}

		return apply_binary_op(lhs_result, rhs_result, op);
	}

	static EvalResult evaluate_unary_operator(const ASTNode& operand_node, std::string_view op,
	                                           EvaluationContext& context) {
		// Recursively evaluate operand
		auto operand_result = evaluate(operand_node, context);

		if (!operand_result.success()) {
			return operand_result;
		}

		return apply_unary_op(operand_result, op);
	}

	// Helper function to get struct size from gTypeInfo
	static size_t get_struct_size_from_typeinfo(const TypeSpecifierNode& type_spec) {
		if (type_spec.type() != Type::Struct) {
			return 0;
		}
		
		size_t type_index = type_spec.type_index();
		if (type_index >= gTypeInfo.size()) {
			return 0;
		}
		
		const TypeInfo& type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (!struct_info) {
			return 0;
		}
		
		return struct_info->total_size;
	}
	
	// Helper function to get the size in bytes for a type specifier
	// Handles both primitive types and struct types
	static size_t get_typespec_size_bytes(const TypeSpecifierNode& type_spec) {
		size_t size_in_bytes = type_spec.size_in_bits() / 8;
		
		// If size_in_bits is 0, look it up
		if (size_in_bytes == 0) {
			if (type_spec.type() == Type::Struct) {
				size_in_bytes = get_struct_size_from_typeinfo(type_spec);
			} else {
				size_in_bytes = get_type_size_bits(type_spec.type()) / 8;
			}
		}
		
		return size_in_bytes;
	}

	static EvalResult evaluate_sizeof(const SizeofExprNode& sizeof_expr, EvaluationContext& context) {
		// sizeof is always a constant expression
		// Get the actual size from the type
		if (sizeof_expr.is_type()) {
			// sizeof(type) - get size from TypeSpecifierNode
			const auto& type_node = sizeof_expr.type_or_expr();
			if (type_node.is<TypeSpecifierNode>()) {
				const auto& type_spec = type_node.as<TypeSpecifierNode>();
				
				// Workaround for parser limitation: when sizeof(arr) is parsed where arr is an
				// array variable, the parser may incorrectly parse it as a type.
				// If size_in_bits is 0, try looking up the identifier in the symbol table.
				if (type_spec.size_in_bits() == 0 && type_spec.token().type() == Token::Type::Identifier && context.symbols) {
					std::string_view identifier = type_spec.token().value();
					
					// Look up the identifier in the symbol table (local first, then global)
					std::optional<ASTNode> symbol = context.symbols->lookup(identifier);
					if (!symbol.has_value() && context.global_symbols) {
						symbol = context.global_symbols->lookup(identifier);
					}
					if (symbol.has_value()) {
						const DeclarationNode* decl = get_decl_from_symbol(*symbol);
						if (decl) {
							// Check if it's an array
							if (decl->is_array()) {
								const auto& array_type_spec = decl->type_node().as<TypeSpecifierNode>();
								size_t element_size = get_typespec_size_bytes(array_type_spec);
								
								// Get total array size from all dimensions
								const auto& dims = decl->array_dimensions();
								if (!dims.empty()) {
									long long total_count = 1;
									bool all_evaluated = true;
									for (const auto& dim_expr : dims) {
										auto eval_result = evaluate(dim_expr, context);
										if (eval_result.success() && eval_result.as_int() > 0) {
											total_count *= eval_result.as_int();
										} else {
											all_evaluated = false;
											break;
										}
									}
									if (all_evaluated && element_size > 0) {
										return EvalResult::from_int(static_cast<long long>(element_size * total_count));
									}
								}
							}
							
							// Not an array, just return the variable's type size
							const auto& var_type = decl->type_node().as<TypeSpecifierNode>();
							size_t var_size = get_typespec_size_bytes(var_type);
							if (var_size > 0) {
								return EvalResult::from_int(static_cast<long long>(var_size));
							}
						}
					}
					
					// If not found in symbol table and we're in a template class member function,
					// try to resolve as a template parameter from the struct name
					if (!symbol.has_value() && context.struct_info) {
						std::string_view struct_name = StringTable::getStringView(context.struct_info->getName());
						
						// Parse the struct name to extract template arguments
						// e.g., "Container_int" -> T = int (4 bytes), "Processor_char" -> T = char (1 byte)
						// For variadic templates like "List_int_char", try all arguments in order
						// Pointer types have "P" suffix: "Container_intP" -> T = int* (8 bytes)
						// Reference types have "R" or "RR" suffix: "Container_intR" -> T = int& (sizeof returns size of int)
						
						// Find the first underscore (start of template arguments)
						size_t first_underscore = struct_name.find('_');
						if (first_underscore != std::string_view::npos && first_underscore + 1 < struct_name.size()) {
							// Extract all template arguments by splitting on underscores
							std::vector<std::string_view> template_args;
							size_t start = first_underscore + 1;
							while (start < struct_name.size()) {
								size_t next = struct_name.find('_', start);
								if (next == std::string_view::npos) {
									template_args.push_back(struct_name.substr(start));
									break;
								} else {
									template_args.push_back(struct_name.substr(start, next - start));
									start = next + 1;
								}
							}
							
							// Try each template argument in order until we find one with a valid size
							// For templates like List<Tp, Up...>, the first argument corresponds to Tp
							for (const auto& type_suffix_raw : template_args) {
								std::string_view type_suffix = type_suffix_raw;
							
							// Strip CV qualifier prefixes ('C' for const, 'V' for volatile)
							// TemplateTypeArg::toString() adds CV qualifiers as prefixes (e.g., "Cint" for const int)
							// sizeof(const T) and sizeof(volatile T) return the same size as sizeof(T)
							while (!type_suffix.empty() && (type_suffix.front() == 'C' || type_suffix.front() == 'V')) {
								type_suffix = type_suffix.substr(1);
							}
							
							// Check for reference types (suffix ends with 'R' or 'RR')
							// TemplateTypeArg::toString() appends "R" for lvalue reference, "RR" for rvalue reference
							// sizeof(T&) and sizeof(T&&) return the size of T, not the size of the reference itself
							if (type_suffix.size() >= 2 && type_suffix.ends_with("RR")) {
								// Rvalue reference - strip "RR" and get base type size
								type_suffix = type_suffix.substr(0, type_suffix.size() - 2);
							} else if (!type_suffix.empty() && type_suffix.back() == 'R') {
								// Lvalue reference - strip "R" and get base type size
								type_suffix = type_suffix.substr(0, type_suffix.size() - 1);
							}
							
							// Check for pointer types (suffix ends with 'P')
							// TemplateTypeArg::toString() appends 'P' for each pointer level
							// e.g., "intP" for int*, "intPP" for int**, etc.
							if (!type_suffix.empty() && type_suffix.back() == 'P') {
								// All pointers are 8 bytes on x64
								return EvalResult::from_int(8);
							}
							
							// Check for array types (suffix contains 'A')
							// Arrays are like "intA[10]" - sizeof(array) = element_size * element_count
							size_t array_pos = type_suffix.find('A');
							if (array_pos != std::string_view::npos) {
								// Extract base type and array dimensions
								std::string_view base_type = type_suffix.substr(0, array_pos);
								std::string_view array_part = type_suffix.substr(array_pos + 1); // Skip 'A'
								
								// Strip CV qualifiers from base_type (already stripped from type_suffix earlier, but double-check)
								while (!base_type.empty() && (base_type.front() == 'C' || base_type.front() == 'V')) {
									base_type = base_type.substr(1);
								}
								
								// Parse array dimensions like "[10]" or "[]"
								if (array_part.starts_with('[') && array_part.ends_with(']')) {
									std::string_view dimensions = array_part.substr(1, array_part.size() - 2);
									if (!dimensions.empty()) {
										// Parse the dimension as a number
										size_t array_count = 0;
										auto result = std::from_chars(dimensions.data(), dimensions.data() + dimensions.size(), array_count);
										if (result.ec == std::errc{} && array_count > 0) {
											// Get base type size
											size_t base_size = 0;
											
											// Check if base_type is a pointer (ends with 'P')
											// e.g., "intP" for int*, "charPP" for char**, etc.
											if (!base_type.empty() && base_type.back() == 'P') {
												// All pointers are 8 bytes on x64
												base_size = 8;
											} else {
												// Look up non-pointer base type size
												if (base_type == "int") base_size = 4;
												else if (base_type == "char") base_size = 1;
												else if (base_type == "short") base_size = 2;
												else if (base_type == "long") base_size = get_long_size_bits() / 8;
												else if (base_type == "float") base_size = 4;
												else if (base_type == "double") base_size = 8;
												else if (base_type == "bool") base_size = 1;
												else if (base_type == "uint") base_size = 4;
												else if (base_type == "uchar") base_size = 1;
												else if (base_type == "ushort") base_size = 2;
												else if (base_type == "ulong") base_size = get_long_size_bits() / 8;
												else if (base_type == "ulonglong") base_size = 8;
												else if (base_type == "longlong") base_size = 8;
											}
											
											if (base_size > 0) {
												return EvalResult::from_int(static_cast<long long>(base_size * array_count));
											}
										}
									}
								}
								// Failed to parse array dimensions - fall through
							} else {
								// Map common type suffixes to their sizes
								// Note: Must match the output of TemplateTypeArg::toString() in TemplateRegistry.h
								// This logic is duplicated in CodeGen.h::resolveTemplateSizeFromStructName
								size_t param_size_bytes = 0;
								if (type_suffix == "int") param_size_bytes = 4;
								else if (type_suffix == "char") param_size_bytes = 1;
								else if (type_suffix == "short") param_size_bytes = 2;
								else if (type_suffix == "long") param_size_bytes = get_long_size_bits() / 8;
								else if (type_suffix == "float") param_size_bytes = 4;
								else if (type_suffix == "double") param_size_bytes = 8;
								else if (type_suffix == "bool") param_size_bytes = 1;
								else if (type_suffix == "uint") param_size_bytes = 4;
								else if (type_suffix == "uchar") param_size_bytes = 1;
								else if (type_suffix == "ushort") param_size_bytes = 2;
								else if (type_suffix == "ulong") param_size_bytes = get_long_size_bits() / 8;
								else if (type_suffix == "ulonglong") param_size_bytes = 8;
								else if (type_suffix == "longlong") param_size_bytes = 8;
								
								if (param_size_bytes > 0) {
									return EvalResult::from_int(static_cast<long long>(param_size_bytes));
								}
							}
							}  // End of for loop over template_args
						}
					}
				}
				
				// size_in_bits() returns bits, convert to bytes
				unsigned long long size_in_bytes = get_typespec_size_bytes(type_spec);
				// sizeof never returns 0 in valid C++ (sizeof(char) == 1, all complete types >= 1).
				// A zero result indicates an incomplete or template-dependent type.
				// Return a template-dependent error so static_assert can be deferred in template contexts.
				if (size_in_bytes == 0) {
					return EvalResult::error(
						"sizeof evaluated to 0 for type '" + std::string(type_spec.token().value()) + "' (incomplete or dependent type)",
						EvalErrorType::TemplateDependentExpression);
				}
				return EvalResult::from_int(static_cast<long long>(size_in_bytes));
			}
		}
		else {
			// sizeof(expression) - determine the size from the expression's type
			const auto& expr_node = sizeof_expr.type_or_expr();
			if (expr_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = expr_node.as<ExpressionNode>();
				
				// Handle identifier - get type from its declaration
				if (std::holds_alternative<IdentifierNode>(expr)) {
					const auto& id_node = std::get<IdentifierNode>(expr);
					
					// Look up the identifier in the symbol table (local first, then global)
					if (context.symbols) {
						auto symbol = context.symbols->lookup(id_node.name());
						if (!symbol.has_value() && context.global_symbols) {
							symbol = context.global_symbols->lookup(id_node.name());
						}
						if (symbol.has_value()) {
							// Get the declaration and extract the type
							const DeclarationNode* decl = get_decl_from_symbol(*symbol);
							
							if (decl) {
								// Check if it's an array - if so, calculate total size
								if (decl->is_array()) {
									const auto& type_spec = decl->type_node().as<TypeSpecifierNode>();
									size_t element_size = get_typespec_size_bytes(type_spec);
									
									// Get total array size from all dimensions
									const auto& dims = decl->array_dimensions();
									if (!dims.empty()) {
										long long total_count = 1;
										bool all_evaluated = true;
										for (const auto& dim_expr : dims) {
											auto eval_result = evaluate(dim_expr, context);
											if (eval_result.success() && eval_result.as_int() > 0) {
												total_count *= eval_result.as_int();
											} else {
												all_evaluated = false;
												break;
											}
										}
										if (all_evaluated && element_size > 0) {
											return EvalResult::from_int(static_cast<long long>(element_size * total_count));
										}
									}
								}
								
								const auto& type_node = decl->type_node();
								if (type_node.is<TypeSpecifierNode>()) {
									const auto& type_spec = type_node.as<TypeSpecifierNode>();
									unsigned long long size_in_bytes = get_typespec_size_bytes(type_spec);
									return EvalResult::from_int(static_cast<long long>(size_in_bytes));
								}
							}
						}
					}
					
					// If we couldn't look up the identifier, return error
					return EvalResult::error("sizeof: identifier not found in symbol table");
				}
				
				// For numeric literals, we can determine the size from the literal itself
				if (std::holds_alternative<NumericLiteralNode>(expr)) {
					const auto& lit = std::get<NumericLiteralNode>(expr);
					unsigned long long size_in_bytes = lit.sizeInBits() / 8;
					return EvalResult::from_int(static_cast<long long>(size_in_bytes));
				}
				
				// Handle array subscript: sizeof(arr[index])
				// For single dimension: returns element size
				// For multidimensional (e.g. int arr[3][4]): sizeof(arr[0]) returns sizeof(int[4]) = 16
				if (std::holds_alternative<ArraySubscriptNode>(expr)) {
					const auto& array_subscript = std::get<ArraySubscriptNode>(expr);
					const ASTNode& array_expr_node = array_subscript.array_expr();
					
					// Check if the array expression is an identifier
					if (array_expr_node.is<ExpressionNode>()) {
						const ExpressionNode& array_expr = array_expr_node.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(array_expr)) {
							const auto& id_node = std::get<IdentifierNode>(array_expr);
							
							// Look up the array identifier in the symbol table
							if (context.symbols) {
								auto symbol = context.symbols->lookup(id_node.name());
								if (symbol.has_value()) {
									const DeclarationNode* decl = get_decl_from_symbol(*symbol);
									if (decl && decl->is_array()) {
										const auto& array_type_spec = decl->type_node().as<TypeSpecifierNode>();
										size_t element_size = get_typespec_size_bytes(array_type_spec);
										
										// For multidimensional arrays, calculate sub-array size
										const auto& dims = decl->array_dimensions();
										if (dims.size() > 1) {
											// Calculate size of sub-array: element_size * product of dims[1..]
											long long sub_array_count = 1;
											bool all_evaluated = true;
											for (size_t i = 1; i < dims.size(); ++i) {
												auto eval_result = evaluate(dims[i], context);
												if (eval_result.success() && eval_result.as_int() > 0) {
													sub_array_count *= eval_result.as_int();
												} else {
													all_evaluated = false;
													break;
												}
											}
											if (all_evaluated && element_size > 0) {
												return EvalResult::from_int(static_cast<long long>(element_size * sub_array_count));
											}
										} else {
											// Single dimension array, return element size
											if (element_size > 0) {
												return EvalResult::from_int(static_cast<long long>(element_size));
											}
										}
									}
								}
							}
						}
					}
				}
				
				// For other expressions, we would need full type inference
				// which requires tracking expression types through the AST
				// This is a compiler limitation, not a C++20 limitation
				return EvalResult::error("sizeof with complex expression not yet supported in constexpr");
			}
		}
		
		return EvalResult::error("Invalid sizeof operand");
	}

	static EvalResult evaluate_alignof(const AlignofExprNode& alignof_expr, EvaluationContext& context) {
		// alignof is always a constant expression
		// Get the actual alignment from the type
		if (alignof_expr.is_type()) {
			// alignof(type) - get alignment from TypeSpecifierNode
			const auto& type_node = alignof_expr.type_or_expr();
			if (type_node.is<TypeSpecifierNode>()) {
				const auto& type_spec = type_node.as<TypeSpecifierNode>();
				
				// For struct types, look up alignment from type info
				if (type_spec.type() == Type::Struct) {
					size_t type_index = type_spec.type_index();
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						const StructTypeInfo* struct_info = type_info.getStructInfo();
						if (struct_info) {
							return EvalResult::from_int(static_cast<long long>(struct_info->alignment));
						}
					}
					return EvalResult::error("Struct alignment not available");
				}
				
				// For primitive types, use standard alignment calculation
				int size_bits = type_spec.size_in_bits();
				if (size_bits == 0) {
					size_bits = get_type_size_bits(type_spec.type());
				}
				size_t size_in_bytes = size_bits / 8;
				size_t alignment = calculate_alignment_from_size(size_in_bytes, type_spec.type());
				
				return EvalResult::from_int(static_cast<long long>(alignment));
			}
		}
		else {
			// alignof(expression) - determine the alignment from the expression's type
			const auto& expr_node = alignof_expr.type_or_expr();
			if (expr_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = expr_node.as<ExpressionNode>();
				
				// Handle identifier - get type from its declaration
				if (std::holds_alternative<IdentifierNode>(expr)) {
					const auto& id_node = std::get<IdentifierNode>(expr);
					
					// Look up the identifier in the symbol table
					if (context.symbols) {
						auto symbol = context.symbols->lookup(id_node.name());
						if (symbol.has_value()) {
							// Get the declaration and extract the type
							const DeclarationNode* decl = get_decl_from_symbol(*symbol);
							
							if (decl) {
								const auto& type_node = decl->type_node();
								if (type_node.is<TypeSpecifierNode>()) {
									const auto& type_spec = type_node.as<TypeSpecifierNode>();
									
									// Handle struct types
									if (type_spec.type() == Type::Struct) {
										size_t type_index = type_spec.type_index();
										if (type_index < gTypeInfo.size()) {
											const TypeInfo& type_info = gTypeInfo[type_index];
											const StructTypeInfo* struct_info = type_info.getStructInfo();
											if (struct_info) {
												return EvalResult::from_int(static_cast<long long>(struct_info->alignment));
											}
										}
									}
									
									// For primitive types
									int size_bits = type_spec.size_in_bits();
									if (size_bits == 0) {
										size_bits = get_type_size_bits(type_spec.type());
									}
									size_t size_in_bytes = size_bits / 8;
									size_t alignment = calculate_alignment_from_size(size_in_bytes, type_spec.type());
									
									return EvalResult::from_int(static_cast<long long>(alignment));
								}
							}
						}
					}
					
					// If we couldn't look up the identifier, return error
					return EvalResult::error("alignof: identifier not found in symbol table");
				}
				
				// For other expressions, return error
				return EvalResult::error("alignof with complex expression not yet supported in constexpr");
			}
		}
		
		return EvalResult::error("Invalid alignof operand");
	}

	static EvalResult evaluate_constructor_call(const ConstructorCallNode& ctor_call, EvaluationContext& context) {
		// Constructor calls like float(3.14), int(100), double(2.718), or type_identity<int>{}
		// These are essentially type conversions/casts in constant expressions
		// Get the argument(s)
		const auto& args = ctor_call.arguments();
		
		// Get the target type
		const ASTNode& type_node = ctor_call.type_node();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Constructor call without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		
		// Handle empty constructor calls (default/value initialization): Type{}
		if (args.size() == 0) {
			// For struct types, this is valid - it's default initialization
			// Return a success result with default value (0 for integers, false for bool, etc.)
			// This allows the constructor call to be used for template argument deduction
			switch (type_spec.type()) {
				case Type::Bool:
					return EvalResult::from_bool(false);
				case Type::Char:
				case Type::Short:
				case Type::Int:
				case Type::Long:
				case Type::LongLong:
					return EvalResult::from_int(0);
				case Type::UnsignedChar:
				case Type::UnsignedShort:
				case Type::UnsignedInt:
				case Type::UnsignedLong:
				case Type::UnsignedLongLong:
					return EvalResult::from_int(0);
				case Type::Float:
				case Type::Double:
				case Type::LongDouble:
					return EvalResult::from_double(0.0);
				case Type::Struct:
				case Type::UserDefined:
					// For struct types, return a success result with value 0
					// This indicates successful default construction
					return EvalResult::from_int(0);
				default:
					return EvalResult::error("Unsupported type for default construction in constant expression");
			}
		}
		
		// For basic type conversions with 1 argument: Type(value)
		if (args.size() != 1) {
			return EvalResult::error("Constructor call must have 0 or 1 arguments for constant evaluation");
		}
		
		return evaluate_expr_node(type_spec.type(), args[0], context, "Unsupported type in constructor call for constant evaluation");
	}

	static EvalResult evaluate_static_cast(const StaticCastNode& cast_node, EvaluationContext& context) {
		// Evaluate static_cast<Type>(expr) and C-style casts in constant expressions
		
		// Get the target type
		const ASTNode& type_node = cast_node.target_type();
		if (!type_node.is<TypeSpecifierNode>()) {
			return EvalResult::error("Cast without valid type specifier");
		}
		
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		
		// Evaluate the expression being cast
		return evaluate_expr_node(type_spec.type(), cast_node.expr(), context, "Unsupported type in static_cast for constant evaluation");
	}

	static EvalResult evaluate_expr_node(Type target_type, const ASTNode& expr, EvaluationContext& context, const char* invalidTypeErrorStr) {
		auto expr_result = evaluate(expr, context);
		if (!expr_result.success()) {
			return expr_result;
		}
		
		// Perform the type conversion
		switch (target_type) {
			case Type::Bool:
				return EvalResult::from_bool(expr_result.as_bool());
			
			case Type::Char:
			case Type::Short:
			case Type::Int:
			case Type::Long:
			case Type::LongLong:
				return EvalResult::from_int(expr_result.as_int());
			
			case Type::UnsignedChar:
			case Type::UnsignedShort:
			case Type::UnsignedInt:
			case Type::UnsignedLong:
			case Type::UnsignedLongLong:
				// For unsigned types, convert to unsigned
				return EvalResult::from_uint(static_cast<unsigned long long>(expr_result.as_int()));
			
			case Type::Float:
			case Type::Double:
			case Type::LongDouble:
				return EvalResult::from_double(expr_result.as_double());
			
			default:
				return EvalResult::error(invalidTypeErrorStr);
		}
	}

	static EvalResult evaluate_identifier(const IdentifierNode& identifier, EvaluationContext& context) {
		// Look up the identifier in the symbol table
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate variable reference: no symbol table provided");
		}

		std::string_view var_name = identifier.name();
		auto symbol_opt = context.symbols->lookup(var_name);
		
		// If not found in symbol table, check for static members in the current struct
		if (!symbol_opt.has_value()) {
			// Check StructDeclarationNode first (for AST-based static members)
			if (context.struct_node != nullptr) {
				StringHandle name_handle = StringTable::getOrInternStringHandle(var_name);
				for (const auto& static_member : context.struct_node->static_members()) {
					if (static_member.name == name_handle) {
						// Found static member in struct AST node
						if (static_member.initializer.has_value()) {
							// Recursively evaluate the initializer
							return evaluate(static_member.initializer.value(), context);
						} else {
							return EvalResult::error("Static member has no initializer: " + std::string(var_name));
						}
					}
				}
			}
			
			// Check StructTypeInfo (for runtime-built struct info)
			if (context.struct_info != nullptr) {
				StringHandle name_handle = StringTable::getOrInternStringHandle(var_name);
				for (const auto& static_member : context.struct_info->static_members) {
					if (static_member.getName() == name_handle) {
						// Found static member in StructTypeInfo
						if (static_member.initializer.has_value()) {
							// Recursively evaluate the initializer
							return evaluate(static_member.initializer.value(), context);
						} else {
							return EvalResult::error("Static member has no initializer: " + std::string(var_name));
						}
					}
				}
			}
			
			// Variable not found - might be a template parameter that hasn't been substituted yet
			// Check if we have a parser context (indicates we're in template definition)
			// Template parameters have short names (typically single letters like T, N, etc.)
			// If the identifier looks like a template parameter, mark it as template-dependent
			if (context.parser != nullptr || var_name.length() <= 2) {
				// Likely a template parameter - return template-dependent error
				return EvalResult::error("Template parameter or undefined variable in constant expression: " + std::string(var_name),
				                         EvalErrorType::TemplateDependentExpression);
			}
			
			return EvalResult::error("Undefined variable in constant expression: " + std::string(var_name));
		}

		const ASTNode& symbol_node = symbol_opt.value();
		
		// Check if it's a TemplateVariableDeclarationNode - these are template-dependent
		if (symbol_node.is<TemplateVariableDeclarationNode>()) {
			// Variable template references with template arguments are template-dependent
			// They need to be evaluated during template instantiation
			return EvalResult::error("Variable template in constant expression - instantiation required: " + std::string(var_name),
			                         EvalErrorType::TemplateDependentExpression);
		}
		
		// Check if it's a DeclarationNode for an enum constant
		if (symbol_node.is<DeclarationNode>()) {
			const DeclarationNode& decl = symbol_node.as<DeclarationNode>();
			if (decl.type_node().is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
				if (type_spec.type() == Type::Enum) {
					// Look up the enumerator value from the type info
					auto type_index = type_spec.type_index();
					if (type_index != 0 && type_index < gTypeInfo.size()) {
						const TypeInfo& ti = gTypeInfo[type_index];
						const EnumTypeInfo* enum_info = ti.getEnumInfo();
						if (enum_info) {
							StringHandle name_handle = StringTable::getOrInternStringHandle(var_name);
							const Enumerator* e = enum_info->findEnumerator(name_handle);
							if (e) {
								return EvalResult(static_cast<int64_t>(e->value));
							}
						}
					}
					// Enum constant but value not found - not an error per se, just unknown
					return EvalResult::error("Enum constant value not found: " + std::string(var_name));
				}
			}
		}
		
		// Check if it's a VariableDeclarationNode
		if (!symbol_node.is<VariableDeclarationNode>()) {
			return EvalResult::error("Identifier in constant expression is not a variable: " + std::string(var_name));
		}

		const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
		
		// Check if it's a constexpr variable
		if (!var_decl.is_constexpr()) {
			return EvalResult::error("Variable in constant expression must be constexpr: " + std::string(var_name));
		}

		// Get the initializer
		const auto& initializer = var_decl.initializer();
		if (!initializer.has_value()) {
			return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
		}

		// Check if the initializer is an InitializerListNode (for arrays)
		if (initializer->is<InitializerListNode>()) {
			const InitializerListNode& init_list = initializer->as<InitializerListNode>();
			const auto& initializers = init_list.initializers();
			
			// Evaluate each element
			std::vector<int64_t> array_values;
			for (const auto& elem : initializers) {
				auto elem_result = evaluate(elem, context);
				if (!elem_result.success()) {
					return elem_result;
				}
				array_values.push_back(elem_result.as_int());
			}
			
			// Return as an array result
			EvalResult array_result;
			array_result.error_type = EvalErrorType::None;
			array_result.is_array = true;
			array_result.array_values = std::move(array_values);
			return array_result;
		}

		// Recursively evaluate the initializer
		return evaluate(initializer.value(), context);
	}

	static EvalResult evaluate_ternary_operator(const TernaryOperatorNode& ternary, EvaluationContext& context) {
		// Evaluate the condition
		auto cond_result = evaluate(ternary.condition(), context);
		if (!cond_result.success()) {
			return cond_result;
		}

		// Evaluate the appropriate branch based on the condition
		if (cond_result.as_bool()) {
			return evaluate(ternary.true_expr(), context);
		} else {
			return evaluate(ternary.false_expr(), context);
		}
	}

	// Helper to extract a LambdaExpressionNode from a variable's initializer
	// Returns nullptr if the initializer is not a lambda
	static const LambdaExpressionNode* extract_lambda_from_initializer(const std::optional<ASTNode>& initializer) {
		if (!initializer.has_value()) {
			return nullptr;
		}
		
		// Check for lambda expression (direct)
		if (initializer->is<LambdaExpressionNode>()) {
			return &initializer->as<LambdaExpressionNode>();
		}
		
		// Check for lambda expression (wrapped in ExpressionNode)
		if (initializer->is<ExpressionNode>()) {
			const ExpressionNode& expr = initializer->as<ExpressionNode>();
			if (std::holds_alternative<LambdaExpressionNode>(expr)) {
				return &std::get<LambdaExpressionNode>(expr);
			}
		}
		
		return nullptr;
	}

	// Evaluate lambda captures and add their values to bindings
	static EvalResult evaluate_lambda_captures(
		const std::vector<LambdaCaptureNode>& captures,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		for (const auto& capture : captures) {
			using CaptureKind = LambdaCaptureNode::CaptureKind;
			
			switch (capture.kind()) {
				case CaptureKind::ByValue:
				case CaptureKind::ByReference: {
					// Named capture: [x] or [&x]
					std::string_view var_name = capture.identifier_name();
					
					// Check for init-capture: [x = expr]
					if (capture.has_initializer()) {
						auto init_result = evaluate(capture.initializer().value(), context);
						if (!init_result.success()) {
							return EvalResult::error("Failed to evaluate init-capture '" + 
								std::string(var_name) + "': " + init_result.error_message);
						}
						bindings[var_name] = init_result;
					} else {
						// Look up the variable in the symbol table
						if (!context.symbols) {
							return EvalResult::error("Cannot evaluate capture: no symbol table provided");
						}
						
						auto symbol_opt = context.symbols->lookup(var_name);
						if (!symbol_opt.has_value()) {
							return EvalResult::error("Captured variable not found: " + std::string(var_name));
						}
						
						const ASTNode& symbol_node = symbol_opt.value();
						if (!symbol_node.is<VariableDeclarationNode>()) {
							return EvalResult::error("Captured identifier is not a variable: " + std::string(var_name));
						}
						
						const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
						
						// For constexpr evaluation, the captured variable must be constexpr
						if (!var_decl.is_constexpr()) {
							return EvalResult::error("Captured variable must be constexpr in constant expression: " + 
								std::string(var_name));
						}
						
						// Evaluate the variable's initializer
						if (!var_decl.initializer().has_value()) {
							return EvalResult::error("Captured constexpr variable has no initializer: " + 
								std::string(var_name));
						}
						
						auto var_result = evaluate(var_decl.initializer().value(), context);
						if (!var_result.success()) {
							return EvalResult::error("Failed to evaluate captured variable '" + 
								std::string(var_name) + "': " + var_result.error_message);
						}
						bindings[var_name] = var_result;
					}
					break;
				}
				
				case CaptureKind::AllByValue:
				case CaptureKind::AllByReference:
					// [=] or [&] - implicit capture
					// In constexpr context, we don't know which variables are used without analyzing the body
					// For now, this is a limitation - we'd need body analysis to support this
					return EvalResult::error("Implicit capture [=] or [&] not supported in constexpr lambdas - use explicit captures");
				
				case CaptureKind::This:
				case CaptureKind::CopyThis:
					// [this] or [*this] - capturing this pointer
					// This would require being in a member function context
					return EvalResult::error("Capture of 'this' not supported in constexpr lambdas");
			}
		}
		
		// Success - all captures evaluated
		EvalResult success;
		success.error_type = EvalErrorType::None;
		success.value = 0LL;  // Dummy value, not used
		return success;
	}

	// Evaluate a callable object (lambda or user-defined functor with operator())
	static EvalResult evaluate_callable_object(
		const VariableDeclarationNode& var_decl,
		const ChunkedVector<ASTNode>& arguments,
		EvaluationContext& context) {
		
		// Check for lambda
		const LambdaExpressionNode* lambda = extract_lambda_from_initializer(var_decl.initializer());
		if (lambda) {
			return evaluate_lambda_call(*lambda, arguments, context);
		}
		
		// Check for ConstructorCallNode (user-defined functor)
		const auto& initializer = var_decl.initializer();
		if (initializer.has_value() && initializer->is<ConstructorCallNode>()) {
			// TODO: Look up operator() in the struct and call it
			return EvalResult::error("User-defined functor constexpr calls not yet implemented");
		}
		
		return EvalResult::error("Object is not callable in constant expression");
	}

	// Evaluate a lambda call
	static EvalResult evaluate_lambda_call(
		const LambdaExpressionNode& lambda,
		const ChunkedVector<ASTNode>& arguments,
		EvaluationContext& context) {
		
		// Check recursion depth
		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded in lambda call");
		}
		
		// Get lambda parameters
		const auto& parameters = lambda.parameters();
		
		if (arguments.size() != parameters.size()) {
			return EvalResult::error("Lambda argument count mismatch in constant expression");
		}
		
		// Build parameter bindings
		std::unordered_map<std::string_view, EvalResult> bindings;
		
		for (size_t i = 0; i < arguments.size(); ++i) {
			const ASTNode& param_node = parameters[i];
			if (!param_node.is<DeclarationNode>()) {
				return EvalResult::error("Invalid parameter node in constexpr lambda");
			}
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
			std::string_view param_name = param_decl.identifier_token().value();
			
			// Evaluate argument
			auto arg_result = evaluate(arguments[i], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			bindings[param_name] = arg_result;
		}
		
		// Handle captures - evaluate each captured variable and add to bindings
		const auto& captures = lambda.captures();
		auto capture_result = evaluate_lambda_captures(captures, bindings, context);
		if (!capture_result.success()) {
			return capture_result;
		}
		
		// Increase recursion depth
		context.current_depth++;
		
		// Evaluate the lambda body
		const ASTNode& body_node = lambda.body();
		
		EvalResult result;
		if (body_node.is<BlockNode>()) {
			// Block body - look for return statement
			const BlockNode& body = body_node.as<BlockNode>();
			const auto& statements = body.get_statements();
			
			if (statements.size() != 1) {
				context.current_depth--;
				return EvalResult::error("Constexpr lambda must have a single return statement (complex statements not yet supported)");
			}
			
			result = evaluate_statement_with_bindings(statements[0], bindings, context);
		} else if (body_node.is<ExpressionNode>()) {
			// Expression body (implicit return)
			result = evaluate_expression_with_bindings(body_node, bindings, context);
		} else {
			context.current_depth--;
			return EvalResult::error("Invalid lambda body in constant expression");
		}
		
		context.current_depth--;
		return result;
	}

	// Evaluate compiler builtin functions at compile time
	static EvalResult evaluate_builtin_function(std::string_view func_name, const ChunkedVector<ASTNode>& arguments, EvaluationContext& context) {
		// Handle __builtin_clzll - count leading zeros for long long
		if (func_name == "__builtin_clzll") {
			if (arguments.size() != 1) {
				return EvalResult::error("__builtin_clzll requires exactly 1 argument");
			}
			auto arg_result = evaluate(arguments[0], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			unsigned long long value;
			if (std::holds_alternative<unsigned long long>(arg_result.value)) {
				value = std::get<unsigned long long>(arg_result.value);
			} else if (std::holds_alternative<long long>(arg_result.value)) {
				value = static_cast<unsigned long long>(std::get<long long>(arg_result.value));
			} else {
				return EvalResult::error("__builtin_clzll argument must be an integer");
			}
			
			if (value == 0) {
				// __builtin_clzll(0) is undefined behavior in GCC/Clang. We return the
				// bit width (64 on typical systems) which matches what some implementations
				// do, and is a reasonable choice for constexpr evaluation. This allows
				// code that guards against zero to work correctly at compile time.
				return EvalResult::from_int(static_cast<long long>(sizeof(long long) * 8));
			}
			
			// Count leading zeros
			int count = 0;
			unsigned long long mask = 1ULL << (sizeof(long long) * 8 - 1);
			while ((value & mask) == 0 && mask != 0) {
				count++;
				mask >>= 1;
			}
			return EvalResult::from_int(static_cast<long long>(count));
		}
		
		// Handle __builtin_clz - count leading zeros for int
		if (func_name == "__builtin_clz") {
			if (arguments.size() != 1) {
				return EvalResult::error("__builtin_clz requires exactly 1 argument");
			}
			auto arg_result = evaluate(arguments[0], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			unsigned int value;
			if (std::holds_alternative<unsigned long long>(arg_result.value)) {
				value = static_cast<unsigned int>(std::get<unsigned long long>(arg_result.value));
			} else if (std::holds_alternative<long long>(arg_result.value)) {
				value = static_cast<unsigned int>(std::get<long long>(arg_result.value));
			} else {
				return EvalResult::error("__builtin_clz argument must be an integer");
			}
			
			if (value == 0) {
				return EvalResult::from_int(static_cast<long long>(sizeof(int) * 8));
			}
			
			int count = 0;
			unsigned int mask = 1U << (sizeof(int) * 8 - 1);
			while ((value & mask) == 0 && mask != 0) {
				count++;
				mask >>= 1;
			}
			return EvalResult::from_int(static_cast<long long>(count));
		}
		
		// Handle __builtin_ctzll - count trailing zeros for long long
		if (func_name == "__builtin_ctzll") {
			if (arguments.size() != 1) {
				return EvalResult::error("__builtin_ctzll requires exactly 1 argument");
			}
			auto arg_result = evaluate(arguments[0], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			unsigned long long value;
			if (std::holds_alternative<unsigned long long>(arg_result.value)) {
				value = std::get<unsigned long long>(arg_result.value);
			} else if (std::holds_alternative<long long>(arg_result.value)) {
				value = static_cast<unsigned long long>(std::get<long long>(arg_result.value));
			} else {
				return EvalResult::error("__builtin_ctzll argument must be an integer");
			}
			
			if (value == 0) {
				return EvalResult::from_int(static_cast<long long>(sizeof(long long) * 8));
			}
			
			int count = 0;
			while ((value & 1) == 0) {
				count++;
				value >>= 1;
			}
			return EvalResult::from_int(static_cast<long long>(count));
		}
		
		// Handle __builtin_ctz - count trailing zeros for int
		if (func_name == "__builtin_ctz") {
			if (arguments.size() != 1) {
				return EvalResult::error("__builtin_ctz requires exactly 1 argument");
			}
			auto arg_result = evaluate(arguments[0], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			unsigned int value;
			if (std::holds_alternative<unsigned long long>(arg_result.value)) {
				value = static_cast<unsigned int>(std::get<unsigned long long>(arg_result.value));
			} else if (std::holds_alternative<long long>(arg_result.value)) {
				value = static_cast<unsigned int>(std::get<long long>(arg_result.value));
			} else {
				return EvalResult::error("__builtin_ctz argument must be an integer");
			}
			
			if (value == 0) {
				return EvalResult::from_int(static_cast<long long>(sizeof(int) * 8));
			}
			
			int count = 0;
			while ((value & 1) == 0) {
				count++;
				value >>= 1;
			}
			return EvalResult::from_int(static_cast<long long>(count));
		}
		
		// Handle __builtin_popcountll - count set bits in long long
		if (func_name == "__builtin_popcountll") {
			if (arguments.size() != 1) {
				return EvalResult::error("__builtin_popcountll requires exactly 1 argument");
			}
			auto arg_result = evaluate(arguments[0], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			unsigned long long value;
			if (std::holds_alternative<unsigned long long>(arg_result.value)) {
				value = std::get<unsigned long long>(arg_result.value);
			} else if (std::holds_alternative<long long>(arg_result.value)) {
				value = static_cast<unsigned long long>(std::get<long long>(arg_result.value));
			} else {
				return EvalResult::error("__builtin_popcountll argument must be an integer");
			}
			
			int count = 0;
			while (value != 0) {
				count += (value & 1);
				value >>= 1;
			}
			return EvalResult::from_int(static_cast<long long>(count));
		}
		
		// Handle __builtin_popcount - count set bits in int
		if (func_name == "__builtin_popcount") {
			if (arguments.size() != 1) {
				return EvalResult::error("__builtin_popcount requires exactly 1 argument");
			}
			auto arg_result = evaluate(arguments[0], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			unsigned int value;
			if (std::holds_alternative<unsigned long long>(arg_result.value)) {
				value = static_cast<unsigned int>(std::get<unsigned long long>(arg_result.value));
			} else if (std::holds_alternative<long long>(arg_result.value)) {
				value = static_cast<unsigned int>(std::get<long long>(arg_result.value));
			} else {
				return EvalResult::error("__builtin_popcount argument must be an integer");
			}
			
			int count = 0;
			while (value != 0) {
				count += (value & 1);
				value >>= 1;
			}
			return EvalResult::from_int(static_cast<long long>(count));
		}
		
		// Handle __builtin_ffsll - find first set bit (1-indexed) in long long
		if (func_name == "__builtin_ffsll") {
			if (arguments.size() != 1) {
				return EvalResult::error("__builtin_ffsll requires exactly 1 argument");
			}
			auto arg_result = evaluate(arguments[0], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			unsigned long long value;
			if (std::holds_alternative<unsigned long long>(arg_result.value)) {
				value = std::get<unsigned long long>(arg_result.value);
			} else if (std::holds_alternative<long long>(arg_result.value)) {
				value = static_cast<unsigned long long>(std::get<long long>(arg_result.value));
			} else {
				return EvalResult::error("__builtin_ffsll argument must be an integer");
			}
			
			if (value == 0) {
				return EvalResult::from_int(0LL);
			}
			
			int pos = 1;
			while ((value & 1) == 0) {
				pos++;
				value >>= 1;
			}
			return EvalResult::from_int(static_cast<long long>(pos));
		}
		
		// Handle __builtin_ffs - find first set bit (1-indexed) in int
		if (func_name == "__builtin_ffs") {
			if (arguments.size() != 1) {
				return EvalResult::error("__builtin_ffs requires exactly 1 argument");
			}
			auto arg_result = evaluate(arguments[0], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			unsigned int value;
			if (std::holds_alternative<unsigned long long>(arg_result.value)) {
				value = static_cast<unsigned int>(std::get<unsigned long long>(arg_result.value));
			} else if (std::holds_alternative<long long>(arg_result.value)) {
				value = static_cast<unsigned int>(std::get<long long>(arg_result.value));
			} else {
				return EvalResult::error("__builtin_ffs argument must be an integer");
			}
			
			if (value == 0) {
				return EvalResult::from_int(0LL);
			}
			
			int pos = 1;
			while ((value & 1) == 0) {
				pos++;
				value >>= 1;
			}
			return EvalResult::from_int(static_cast<long long>(pos));
		}
		
		// Handle __builtin_constant_p - check if argument is a compile-time constant
		if (func_name == "__builtin_constant_p") {
			if (arguments.size() != 1) {
				return EvalResult::error("__builtin_constant_p requires exactly 1 argument");
			}
			// In a constexpr context, if we can evaluate the argument, it's a constant
			auto arg_result = evaluate(arguments[0], context);
			return EvalResult::from_int(arg_result.success() ? 1LL : 0LL);
		}
		
		// Handle __builtin_abs, __builtin_labs, __builtin_llabs
		if (func_name == "__builtin_abs" || func_name == "__builtin_labs" || func_name == "__builtin_llabs") {
			if (arguments.size() != 1) {
				return EvalResult::error(std::string(func_name) + " requires exactly 1 argument");
			}
			auto arg_result = evaluate(arguments[0], context);
			if (!arg_result.success()) {
				return arg_result;
			}
			long long value = arg_result.as_int();
			// abs(LLONG_MIN) is undefined behavior (overflow when negating)
			if (value == LLONG_MIN) {
				return EvalResult::error(std::string(func_name) + "(LLONG_MIN) is undefined behavior");
			}
			return EvalResult::from_int(value < 0 ? -value : value);
		}
		
		// Not a known builtin function - return a special error that callers can check
		return EvalResult::error("Unknown builtin function: " + std::string(func_name));
	}

	// Try to evaluate a FunctionCallNode as a variable template instantiation.
	// Variable templates like __is_ratio_v<T> get parsed as FunctionCallNodes because
	// identifier<args> looks like a function call syntactically. This helper extracts
	// the template arguments, instantiates the variable template, and evaluates it.
	static EvalResult tryEvaluateAsVariableTemplate(std::string_view func_name, const FunctionCallNode& func_call, EvaluationContext& context) {
		if (!context.parser) {
			return EvalResult::error("No parser available for variable template instantiation");
		}
		
		if (!func_call.has_template_arguments()) {
			return EvalResult::error("No template arguments for variable template");
		}
		
		std::vector<TemplateTypeArg> template_args;
		for (const ASTNode& arg_node : func_call.template_arguments()) {
			if (arg_node.is<TypeSpecifierNode>()) {
				template_args.emplace_back(arg_node.as<TypeSpecifierNode>());
			} else if (arg_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = arg_node.as<ExpressionNode>();
				if (std::holds_alternative<NumericLiteralNode>(expr)) {
					const auto& lit = std::get<NumericLiteralNode>(expr);
					const auto& raw_value = lit.value();
					int64_t val = 0;
					if (std::holds_alternative<unsigned long long>(raw_value)) {
						val = static_cast<int64_t>(std::get<unsigned long long>(raw_value));
					} else if (std::holds_alternative<double>(raw_value)) {
						val = static_cast<int64_t>(std::get<double>(raw_value));
					}
					template_args.emplace_back(val, lit.type());
				} else if (std::holds_alternative<BoolLiteralNode>(expr)) {
					const auto& lit = std::get<BoolLiteralNode>(expr);
					template_args.emplace_back(static_cast<int64_t>(lit.value() ? 1 : 0), Type::Bool);
				} else {
					return EvalResult::error("Cannot extract template argument value for variable template");
				}
			} else {
				return EvalResult::error("Unsupported template argument type for variable template");
			}
		}
		
		if (template_args.empty()) {
			return EvalResult::error("No template arguments extracted for variable template");
		}
		
		// Try to instantiate the variable template
		auto var_node = context.parser->try_instantiate_variable_template(func_name, template_args);
		
		// Try with qualified name if simple name didn't work
		if (!var_node.has_value() && func_call.has_qualified_name()) {
			var_node = context.parser->try_instantiate_variable_template(func_call.qualified_name(), template_args);
		}
		
		if (var_node.has_value() && var_node->is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = var_node->as<VariableDeclarationNode>();
			if (var_decl.initializer().has_value()) {
				return evaluate(var_decl.initializer().value(), context);
			}
		}
		
		return EvalResult::error("Variable template instantiation failed: " + std::string(func_name));
	}

	static EvalResult evaluate_function_call(const FunctionCallNode& func_call, EvaluationContext& context) {
		// Check recursion depth
		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded");
		}

		// Get the function declaration
		const DeclarationNode& func_decl_node = func_call.function_declaration();
		
		// Look up the function in the symbol table to get the FunctionDeclarationNode
		if (!context.symbols) {
			return EvalResult::error("Cannot evaluate function call: no symbol table provided");
		}

		std::string_view func_name = func_decl_node.identifier_token().value();
		
		// First try to get the qualified source name (e.g., "std::__is_complete_or_unbounded")
		// This is set by the parser for qualified function calls
		std::string_view qualified_name = func_name;
		if (func_call.has_qualified_name()) {
			qualified_name = func_call.qualified_name();
			FLASH_LOG(Templates, Debug, "Using qualified name for template lookup: ", qualified_name);
		}
		
		// Special handling for std::__is_complete_or_unbounded
		// This is a helper function in the standard library that checks if a type is complete
		// __is_complete_or_unbounded evaluates to true if either:
		// 1. T is a complete type, or
		// 2. T is an unbounded array type (e.g. int[])
		if (qualified_name == "std::__is_complete_or_unbounded" || func_name == "__is_complete_or_unbounded") {
			FLASH_LOG(Templates, Debug, "Special handling for __is_complete_or_unbounded");
			
			// The function takes a __type_identity<T> argument
			// We need to extract the type T and check if it's complete or unbounded
			if (func_call.arguments().size() == 0) {
				return EvalResult::error("__is_complete_or_unbounded requires a type argument");
			}
			
			// Get the first argument (should be a ConstructorCallNode for __type_identity<T>{})
			const ASTNode& arg = func_call.arguments()[0];
			
			// Try to extract the type from the argument
			// The argument is typically __type_identity<T>{} which is a constructor call
			if (arg.is<ExpressionNode>()) {
				const ExpressionNode& expr = arg.as<ExpressionNode>();
				if (std::holds_alternative<ConstructorCallNode>(expr)) {
					const ConstructorCallNode& ctor = std::get<ConstructorCallNode>(expr);
					const ASTNode& type_node = ctor.type_node();
					
					if (type_node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
						Type base_type = type_spec.type();
						bool is_reference = type_spec.is_reference();
						size_t pointer_depth = type_spec.pointer_depth();
						bool is_array = type_spec.is_array();
						std::optional<size_t> array_size = type_spec.array_size();
						
						// Check for void - always incomplete
						if (base_type == Type::Void && pointer_depth == 0 && !is_reference) {
							return EvalResult::from_bool(false);
						}
						
						// Check for unbounded array - always returns true
						if (is_array && (!array_size.has_value() || *array_size == 0)) {
							return EvalResult::from_bool(true);
						}
						
						// Check for incomplete class/struct types
						// A type is incomplete if it's a struct/class with no StructTypeInfo
						TypeIndex type_idx = type_spec.type_index();
						if (type_idx != TypeIndex{0} && (base_type == Type::Struct || base_type == Type::UserDefined)) {
							const TypeInfo& type_info = gTypeInfo[type_idx];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							
							// If it's a struct/class type with no struct_info, it's incomplete
							if (!struct_info && pointer_depth == 0 && !is_reference) {
								return EvalResult::from_bool(false);
							}
						}
						
						// All other types are considered complete
						return EvalResult::from_bool(true);
					}
				}
			}
			
			// If we can't extract the type, return true as a fallback
			FLASH_LOG(Templates, Debug, "__is_complete_or_unbounded: couldn't extract type, returning true as fallback");
			return EvalResult::from_bool(true);
		}
		
		// First try simple name lookup in symbol table
		auto symbol_opt = context.symbols->lookup(func_name);
		
		// If not found in symbol table, try the global template registry
		// This handles cases where a template function is defined but not yet instantiated
		if (!symbol_opt.has_value() && context.parser) {
			// Try to find the template in the global registry with qualified name first
			auto template_opt = gTemplateRegistry.lookupTemplate(qualified_name);
			
			// If not found with qualified name, try with simple name
			if (!template_opt.has_value() && qualified_name != func_name) {
				template_opt = gTemplateRegistry.lookupTemplate(func_name);
			}
			
			// If still not found with simple name, try with common namespace prefixes
			if (!template_opt.has_value()) {
				std::vector<std::string> name_candidates;
				name_candidates.push_back(std::string("std::") + std::string(func_name));
				name_candidates.push_back(std::string("__gnu_cxx::") + std::string(func_name));
				
				for (const auto& candidate_name : name_candidates) {
					template_opt = gTemplateRegistry.lookupTemplate(candidate_name);
					if (template_opt.has_value()) {
						break;
					}
				}
			}
			
			// If we found the template, use it
			if (template_opt.has_value()) {
				symbol_opt = template_opt;
			}
		}
		
		// If simple lookup fails, try to find the function as a static member in struct types
		if (!symbol_opt.has_value()) {
			// Search all struct types for a static member function with this name
			// This handles cases like Point::static_sum where the parser creates a FunctionCallNode
			// but the function name is just "static_sum" without the qualifier
			
			// Note: This search will find both static and non-static member functions.
			// For non-static members, the evaluation will naturally fail when we try to call them
			// without an instance (parameter count mismatch or missing 'this' context).
			// Static member functions have no implicit 'this' parameter, so they work correctly.
			
			for (const auto& type_info : ::gTypeInfo) {
				if (!type_info.struct_info_) continue;
				
				// Search member functions in this struct
				for (const auto& member_func : type_info.struct_info_->member_functions) {
					if (member_func.name == StringTable::getOrInternStringHandle(func_name)) {
						// Found a matching member function
						const ASTNode& func_node = member_func.function_decl;
						if (func_node.is<FunctionDeclarationNode>()) {
							const FunctionDeclarationNode& func_decl = func_node.as<FunctionDeclarationNode>();
							
							// For static storage duration, also try non-constexpr functions with simple bodies
							// (static initializers can call any function whose body is available)
							bool can_evaluate = func_decl.is_constexpr() ||
								(context.storage_duration == ConstExpr::StorageDuration::Static);
							if (can_evaluate) {
								// Get the function body
								const auto& definition = func_decl.get_definition();
								if (definition.has_value()) {
									// Evaluate arguments
									const auto& arguments = func_call.arguments();
									const auto& parameters = func_decl.parameter_nodes();
									
									// This parameter count check implicitly ensures we're calling static members:
									// Non-static members would have a conceptual 'this' parameter that we're not providing
									if (arguments.size() == parameters.size()) {
										// Pass empty bindings for static member function calls
										std::unordered_map<std::string_view, EvalResult> empty_bindings;
										return evaluate_function_call_with_bindings(func_decl, arguments, empty_bindings, context);
									}
								}
							}
						}
					}
				}
			}
			
			// Check if this is a compiler builtin function (starts with __builtin)
			if (func_name.starts_with("__builtin")) {
				auto builtin_result = evaluate_builtin_function(func_name, func_call.arguments(), context);
				if (builtin_result.success()) {
					return builtin_result;
				}
				// Builtin evaluation failed - propagate the specific error
				// (e.g., "argument must be an integer", "LLONG_MIN is undefined")
				return builtin_result;
			}
			
			// Try variable template instantiation before giving up
			// Variable templates like __is_ratio_v<T> might not be in the symbol table
			// but can be instantiated from the template registry
			if (func_call.has_template_arguments() && context.parser) {
				auto var_template_result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
				if (var_template_result.success()) return var_template_result;
			}
			
			return EvalResult::error("Undefined function in constant expression: " + std::string(func_name));
		}

		const ASTNode& symbol_node = symbol_opt.value();
		
		// Check if it's a TemplateVariableDeclarationNode (variable template like __is_ratio_v<T>)
		// These get parsed as FunctionCallNodes because identifier<args> looks like a function call
		if (symbol_node.is<TemplateVariableDeclarationNode>()) {
			auto result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
			if (result.success()) return result;
			// If variable template instantiation failed, fall through to try other lookups
		}
		
		// Check if it's a FunctionDeclarationNode (regular function)
		if (symbol_node.is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& func_decl = symbol_node.as<FunctionDeclarationNode>();
			
			// For static storage duration, also try non-constexpr functions with simple bodies
			// (static initializers can call any function whose body is available)
			if (!func_decl.is_constexpr() && context.storage_duration != ConstExpr::StorageDuration::Static) {
				return EvalResult::error("Function in constant expression must be constexpr: " + std::string(func_name));
			}

			// Get the function body
			const auto& definition = func_decl.get_definition();
			if (!definition.has_value()) {
				return EvalResult::error("Constexpr function has no body: " + std::string(func_name));
			}

			// Evaluate arguments
			const auto& arguments = func_call.arguments();
			const auto& parameters = func_decl.parameter_nodes();
			
			if (arguments.size() != parameters.size()) {
				return EvalResult::error("Function argument count mismatch in constant expression");
			}

			// Pass empty bindings for top-level function calls
			std::unordered_map<std::string_view, EvalResult> empty_bindings;
			return evaluate_function_call_with_bindings(func_decl, arguments, empty_bindings, context);
		}
		
		// Check if it's a TemplateFunctionDeclarationNode (template function)
		if (symbol_node.is<TemplateFunctionDeclarationNode>()) {
			const auto& arguments = func_call.arguments();
			
			// Try to find or instantiate the function with the given arguments
			// First, try to find an already-instantiated version in the symbol table
			// Try both qualified and unqualified names
			std::vector<ASTNode> all_overloads = context.symbols->lookup_all(qualified_name);
			if (all_overloads.empty() && qualified_name != func_name) {
				all_overloads = context.symbols->lookup_all(func_name);
			}
			
			// Look for a constexpr FunctionDeclarationNode that matches the argument count
			for (const auto& overload : all_overloads) {
				if (overload.is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& candidate = overload.as<FunctionDeclarationNode>();
					if (candidate.is_constexpr() && 
					    candidate.parameter_nodes().size() == arguments.size()) {
						// Found a potential match - try to evaluate it
						std::unordered_map<std::string_view, EvalResult> empty_bindings;
						return evaluate_function_call_with_bindings(candidate, arguments, empty_bindings, context);
					}
				}
			}
			
			// No pre-instantiated version found - try to instantiate on-demand if parser is available
			if (context.parser) {
				// Use shared helper to deduce template arguments from function call arguments
				std::vector<TemplateTypeArg> deduced_args = TemplateInstantiationHelper::deduceTemplateArgsFromCall(arguments);
				
				// Try to instantiate even if we have fewer deduced args than template params
				// The template might have default parameters that can fill in the rest
				if (!deduced_args.empty()) {
					// Use shared helper to try instantiation with various name variations
					auto instantiated_opt = TemplateInstantiationHelper::tryInstantiateTemplateFunction(
						*context.parser, qualified_name, func_name, deduced_args);
					
					if (instantiated_opt.has_value() && instantiated_opt->is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& instantiated_func = instantiated_opt->as<FunctionDeclarationNode>();
						if (instantiated_func.is_constexpr()) {
							// Successfully instantiated - evaluate it
							std::unordered_map<std::string_view, EvalResult> empty_bindings;
							return evaluate_function_call_with_bindings(instantiated_func, arguments, empty_bindings, context);
						}
					} else if (instantiated_opt.has_value()) {
						FLASH_LOG(Templates, Debug, "Instantiation succeeded but result is not a FunctionDeclarationNode");
					}
				} else {
					FLASH_LOG(Templates, Debug, "No template arguments could be deduced from function call arguments");
				}
			}
			
			// No pre-instantiated version found and couldn't instantiate on-demand
			// Return a specific error indicating this is a template function issue
			return EvalResult::error("Template function in constant expression - instantiation required: " + std::string(qualified_name), 
			                         EvalErrorType::TemplateDependentExpression);
		}
		
		// Check if it's a VariableDeclarationNode (could be a lambda/functor callable object)
		if (symbol_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
			return evaluate_callable_object(var_decl, func_call.arguments(), context);
		}
		
		// Check if it's a TemplateVariableDeclarationNode (variable template like __is_ratio_v<T>)
		// These get parsed as FunctionCallNodes because identifier<args> looks like a function call
		if (symbol_node.is<TemplateVariableDeclarationNode>()) {
			auto result = tryEvaluateAsVariableTemplate(func_name, func_call, context);
			if (result.success()) return result;
		}
		
		return EvalResult::error("Identifier is not a function or callable object: " + std::string(func_name));
	}

	static EvalResult evaluate_function_call_with_bindings(
		const FunctionDeclarationNode& func_decl,
		const ChunkedVector<ASTNode>& arguments,
		const std::unordered_map<std::string_view, EvalResult>& outer_bindings,
		EvaluationContext& context) {
		
		// Check recursion depth
		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded");
		}

		// Get the function body
		const auto& definition = func_decl.get_definition();
		if (!definition.has_value()) {
			return EvalResult::error("Constexpr function has no body");
		}

		// Evaluate arguments
		const auto& parameters = func_decl.parameter_nodes();
		
		if (arguments.size() != parameters.size()) {
			return EvalResult::error("Function argument count mismatch in constant expression");
		}

		// Create a new symbol table scope for the function
		// We'll use a simple map to bind parameters to their evaluated values
		std::unordered_map<std::string_view, EvalResult> param_bindings;
		
		for (size_t i = 0; i < arguments.size(); ++i) {
			// Evaluate the argument with outer bindings (for nested calls)
			auto arg_result = evaluate_expression_with_bindings_const(arguments[i], outer_bindings, context);
			if (!arg_result.success()) {
				return arg_result;
			}
			
			// Get parameter name
			const ASTNode& param_node = parameters[i];
			if (!param_node.is<DeclarationNode>()) {
				return EvalResult::error("Invalid parameter node");
			}
			
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
			std::string_view param_name = param_decl.identifier_token().value();
			
			// Bind parameter to its value
			param_bindings[param_name] = arg_result;
		}

		// Increase recursion depth
		context.current_depth++;
		
		// Evaluate the function body with parameter bindings
		const ASTNode& body_node = definition.value();
		if (!body_node.is<BlockNode>()) {
			context.current_depth--;
			return EvalResult::error("Function body is not a block");
		}
		
		const BlockNode& body = body_node.as<BlockNode>();
		const auto& statements = body.get_statements();
		
		// Evaluate all statements in the function body
		// Local variable bindings are mutable - they can be added to as we process statements
		std::unordered_map<std::string_view, EvalResult> local_bindings = param_bindings;
		
		for (size_t i = 0; i < statements.size(); i++) {
			auto result = evaluate_statement_with_bindings(statements[i], local_bindings, context);
			
			// If the result is successful, it means a return value was computed
			// This can happen either directly from a return statement, or indirectly
			// from an if/while/for statement that contains a return
			if (result.success()) {
				context.current_depth--;
				return result;
			}
			
			// For other statements (like variable declarations), result contains the binding info
			// The binding has already been added to local_bindings by evaluate_statement_with_bindings
			if (!result.success() && result.error_message != "Statement executed (not a return)") {
				// An actual error occurred
				context.current_depth--;
				return result;
			}
		}
		
		context.current_depth--;
		return EvalResult::error("Constexpr function did not return a value");
	}

	static EvalResult evaluate_statement_with_bindings(
		const ASTNode& stmt_node,
		std::unordered_map<std::string_view, EvalResult>& bindings,
		EvaluationContext& context) {
		
		// Check if it's a return statement
		if (stmt_node.is<ReturnStatementNode>()) {
			const ReturnStatementNode& ret_stmt = stmt_node.as<ReturnStatementNode>();
			const auto& return_expr = ret_stmt.expression();
			
			if (!return_expr.has_value()) {
				return EvalResult::error("Constexpr function return statement has no expression");
			}
			
			return evaluate_expression_with_bindings(return_expr.value(), bindings, context);
		}
		
		// Handle variable declarations
		if (stmt_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = stmt_node.as<VariableDeclarationNode>();
			const DeclarationNode& decl = var_decl.declaration_node().as<DeclarationNode>();
			std::string_view var_name = decl.identifier_token().value();
			
			// Evaluate the initializer if present
			if (var_decl.initializer().has_value()) {
				const ASTNode& init_expr = var_decl.initializer().value();
				
				// Handle array initialization with InitializerListNode
				if (init_expr.is<InitializerListNode>()) {
					const InitializerListNode& init_list = init_expr.as<InitializerListNode>();
					const auto& initializers = init_list.initializers();
					
					// Create array value - evaluate each element
					std::vector<int64_t> array_values;
					for (size_t i = 0; i < initializers.size(); i++) {
						auto elem_result = evaluate_expression_with_bindings(initializers[i], bindings, context);
						if (!elem_result.success()) {
							return elem_result;
						}
						array_values.push_back(elem_result.as_int());
					}
					
					// Store as an array binding
					EvalResult array_result;
					array_result.error_type = EvalErrorType::None;
					array_result.is_array = true;
					array_result.array_values = std::move(array_values);
					bindings[var_name] = array_result;
					
					// Return a sentinel indicating statement executed successfully
					return EvalResult::error("Statement executed (not a return)");
				}
				
				// Regular expression initializer
				auto init_result = evaluate_expression_with_bindings(init_expr, bindings, context);
				if (!init_result.success()) {
					return init_result;
				}
				
				// Add to bindings
				bindings[var_name] = init_result;
				return EvalResult::error("Statement executed (not a return)");
			}
			
			// Uninitialized variable - set to 0
			bindings[var_name] = EvalResult::from_int(0);
			return EvalResult::error("Statement executed (not a return)");
		}
		
		// Handle for loops (C++14 constexpr)
		if (stmt_node.is<ForStatementNode>()) {
			const ForStatementNode& for_stmt = stmt_node.as<ForStatementNode>();
			
			// Execute init statement if present
			if (for_stmt.has_init()) {
				auto init_result = evaluate_statement_with_bindings(for_stmt.get_init_statement().value(), bindings, context);
				// Ignore result for init statement (it's usually a variable declaration)
			}
			
			// Loop until condition is false
			while (true) {
				// Check complexity limit
				if (++context.step_count > context.max_steps) {
					return EvalResult::error("Constexpr evaluation exceeded complexity limit in for loop");
				}
				
				// Evaluate condition if present
				if (for_stmt.has_condition()) {
					auto cond_result = evaluate_expression_with_bindings(for_stmt.get_condition().value(), bindings, context);
					if (!cond_result.success()) {
						return cond_result;
					}
					if (!cond_result.as_bool()) {
						break;  // Exit loop when condition is false
					}
				}
				
				// Execute loop body
				const ASTNode& body = for_stmt.get_body_statement();
				if (body.is<BlockNode>()) {
					const BlockNode& block = body.as<BlockNode>();
					const auto& statements = block.get_statements();
					for (size_t i = 0; i < statements.size(); i++) {
						auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
						// If this was a return statement, propagate it up
						if (statements[i].is<ReturnStatementNode>()) {
							return result;
						}
					}
				} else {
					auto result = evaluate_statement_with_bindings(body, bindings, context);
					// If this was a return statement, propagate it up
					if (body.is<ReturnStatementNode>()) {
						return result;
					}
				}
				
				// Execute update expression if present
				if (for_stmt.has_update()) {
					auto update_result = evaluate_expression_with_bindings(for_stmt.get_update_expression().value(), bindings, context);
					// Update expression result is ignored (side effects have been applied)
				}
			}
			
			return EvalResult::error("Statement executed (not a return)");
		}
		
		// Handle while loops (C++14 constexpr)
		if (stmt_node.is<WhileStatementNode>()) {
			const WhileStatementNode& while_stmt = stmt_node.as<WhileStatementNode>();
			
			while (true) {
				// Check complexity limit
				if (++context.step_count > context.max_steps) {
					return EvalResult::error("Constexpr evaluation exceeded complexity limit in while loop");
				}
				
				// Evaluate condition
				auto cond_result = evaluate_expression_with_bindings(while_stmt.get_condition(), bindings, context);
				if (!cond_result.success()) {
					return cond_result;
				}
				if (!cond_result.as_bool()) {
					break;  // Exit loop when condition is false
				}
				
				// Execute loop body
				const ASTNode& body = while_stmt.get_body_statement();
				if (body.is<BlockNode>()) {
					const BlockNode& block = body.as<BlockNode>();
					const auto& statements = block.get_statements();
					for (size_t i = 0; i < statements.size(); i++) {
						auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
						// If this was a return statement, propagate it up
						if (statements[i].is<ReturnStatementNode>()) {
							return result;
						}
					}
				} else {
					auto result = evaluate_statement_with_bindings(body, bindings, context);
					// If this was a return statement, propagate it up
					if (body.is<ReturnStatementNode>()) {
						return result;
					}
				}
			}
			
			return EvalResult::error("Statement executed (not a return)");
		}
		
		// Handle if statements (C++14 constexpr)
		if (stmt_node.is<IfStatementNode>()) {
			const IfStatementNode& if_stmt = stmt_node.as<IfStatementNode>();
			
			// Execute init statement if present (C++17 feature)
			if (if_stmt.has_init()) {
				auto init_result = evaluate_statement_with_bindings(if_stmt.get_init_statement().value(), bindings, context);
				// Ignore result for init statement
			}
			
			// Evaluate condition
			auto cond_result = evaluate_expression_with_bindings(if_stmt.get_condition(), bindings, context);
			if (!cond_result.success()) {
				return cond_result;
			}
			
			// Execute then or else branch
			if (cond_result.as_bool()) {
				// Execute then branch
				const ASTNode& then_stmt = if_stmt.get_then_statement();
				if (then_stmt.is<BlockNode>()) {
					const BlockNode& block = then_stmt.as<BlockNode>();
					const auto& statements = block.get_statements();
					for (size_t i = 0; i < statements.size(); i++) {
						auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
						// If this was a return statement, propagate it up
						if (statements[i].is<ReturnStatementNode>()) {
							return result;
						}
					}
				} else {
					auto result = evaluate_statement_with_bindings(then_stmt, bindings, context);
					if (then_stmt.is<ReturnStatementNode>()) {
						return result;
					}
				}
			} else if (if_stmt.has_else()) {
				// Execute else branch
				// Fix dangling reference warning by storing the value first
				std::optional<ASTNode> else_stmt_opt = if_stmt.get_else_statement();
				if (else_stmt_opt.has_value()) {
					const ASTNode& else_stmt = *else_stmt_opt;
					if (else_stmt.is<BlockNode>()) {
						const BlockNode& block = else_stmt.as<BlockNode>();
						const auto& statements = block.get_statements();
						for (size_t i = 0; i < statements.size(); i++) {
							auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
							// If this was a return statement, propagate it up
							if (statements[i].is<ReturnStatementNode>()) {
								return result;
							}
						}
					} else {
						auto result = evaluate_statement_with_bindings(else_stmt, bindings, context);
						if (else_stmt.is<ReturnStatementNode>()) {
							return result;
						}
					}
				}
			}
			
			return EvalResult::error("Statement executed (not a return)");
		}
		
		// Handle expression statements (assignments, increments, etc.)
		if (stmt_node.is<ExpressionNode>()) {
			// Evaluate the expression (which may have side effects like assignments)
			auto result = evaluate_expression_with_bindings(stmt_node, bindings, context);
			// Expression statements don't return values to the caller
			return EvalResult::error("Statement executed (not a return)");
		}
		
		// Handle block statements (nested blocks)
		if (stmt_node.is<BlockNode>()) {
			const BlockNode& block = stmt_node.as<BlockNode>();
			const auto& statements = block.get_statements();
			for (size_t i = 0; i < statements.size(); i++) {
				auto result = evaluate_statement_with_bindings(statements[i], bindings, context);
				// If this was a return statement, propagate it up
				if (statements[i].is<ReturnStatementNode>()) {
					return result;
				}
			}
			return EvalResult::error("Statement executed (not a return)");
		}
		
		return EvalResult::error("Unsupported statement type in constexpr function");
	}

	// Overload for mutable bindings (used in statements with side effects like assignments)
