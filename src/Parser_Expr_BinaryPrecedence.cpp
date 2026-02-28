ParseResult Parser::parse_expression(int precedence, ExpressionContext context)
{
	static thread_local int recursion_depth = 0;
	constexpr int MAX_RECURSION_DEPTH = 50;
	
	// RAII guard to ensure recursion_depth is decremented on all exit paths
	struct RecursionGuard {
		int& depth;
		RecursionGuard(int& d) : depth(d) { ++depth; }
		~RecursionGuard() { --depth; }
	} guard(recursion_depth);
	
	if (recursion_depth > MAX_RECURSION_DEPTH) {
		FLASH_LOG_FORMAT(Parser, Error, "Hit MAX_RECURSION_DEPTH limit ({}) in parse_expression", MAX_RECURSION_DEPTH);
		return ParseResult::error("Parser error: maximum recursion depth exceeded", current_token_);
	}
	
	FLASH_LOG_FORMAT(Parser, Debug, ">>> parse_expression: Starting with precedence={}, context={}, depth={}, current token: {}", 
		precedence, static_cast<int>(context), recursion_depth, 
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	
	ParseResult result = parse_unary_expression(context);
	if (result.is_error()) {
		FLASH_LOG(Parser, Debug, "parse_expression: parse_unary_expression failed: ", result.error_message());
		return result;
	}

	constexpr int MAX_BINARY_OP_ITERATIONS = 100;
	int binary_op_iteration = 0;
	while (true) {
		if (++binary_op_iteration > MAX_BINARY_OP_ITERATIONS) {
			FLASH_LOG_FORMAT(Parser, Error, "Hit MAX_BINARY_OP_ITERATIONS limit ({}) in parse_expression binary operator loop", MAX_BINARY_OP_ITERATIONS);
			return ParseResult::error("Parser error: too many binary operator iterations", current_token_);
		}
		
		// Safety check: ensure we have a token to examine
		if (peek().is_eof()) {
			break;
		}
		
		// Check if the current token is a binary operator or comma (which can be an operator)
		bool is_operator = peek().is_operator();
		bool is_comma = peek().is_punctuator() && peek() == ","_tok;

		if (!is_operator && !is_comma) {
			break;
		}

		// Skip pack expansion operator '...' - it should be handled by the caller (e.g., function call argument parsing)
		if (peek() == "..."_tok) {
			break;
		}

		// Skip ternary operator '?' - it's handled separately below
		if (is_operator && peek() == "?"_tok) {
			break;
		}
		
		// In TemplateArgument context, stop at '>' and ',' as they delimit template arguments
		// This allows parsing expressions like "T::value || X::value" while stopping at the
		// template argument delimiter
		if (context == ExpressionContext::TemplateArgument) {
			if (peek() == ">"_tok || peek() == ">>"_tok) {
				break;  // Stop at template closing bracket
			}
			if (peek() == ","_tok) {
				break;  // Stop at template argument separator
			}
		}

		// Phase 1: C++20 Template Argument Disambiguation
		// Phase 3: Enhanced with context-aware disambiguation
		// Before treating '<' as a comparison operator, check if it could be template arguments
		// This handles cases like: decltype(ns::func<Args...>(0)) where '<' after qualified-id
		// should be parsed as template arguments, not as less-than operator
		// 
		// Context-aware rules:
		// - Decltype context: strongly prefer template arguments (strictest)
		// - TemplateArgument context: prefer template arguments
		// - RequiresClause context: prefer template arguments
		// - Normal context: use regular disambiguation
		if (is_operator && peek() == "<"_tok && result.node().has_value()) {
			FLASH_LOG(Parser, Debug, "Binary operator loop: checking if '<' is template arguments, context=", static_cast<int>(context));
			
			// Check if the left side could be a template name
			// Don't attempt template argument parsing if it's clearly a simple variable
			bool could_be_template_name = false;
			
			if (result.node()->is<ExpressionNode>()) {
				const auto& expr = result.node()->as<ExpressionNode>();
				
				// Check if it's an identifier that could be a template
				if (std::holds_alternative<IdentifierNode>(expr)) {
					const auto& ident = std::get<IdentifierNode>(expr);
					std::string_view ident_name = ident.name();
					
					// Check if this identifier is in the symbol table as a regular variable
					auto symbol_type = gSymbolTable.lookup(StringTable::getOrInternStringHandle(ident_name), 
					                                       gSymbolTable.get_current_scope_handle());
					
					// If it's a variable, don't try template argument parsing
					if (symbol_type && (symbol_type->is<VariableDeclarationNode>() || 
					                   symbol_type->is<DeclarationNode>())) {
						// This is a regular variable, treat < as comparison
						could_be_template_name = false;
					} else {
						// Not a known variable, could be a template
						could_be_template_name = true;
					}
				} else if (std::holds_alternative<FunctionCallNode>(expr) ||
				           std::holds_alternative<ConstructorCallNode>(expr)) {
					// Function calls and constructor calls cannot have template arguments after them.
					// This handles cases like:
					// - T(-1) < T(0) where T is a template parameter used in functional-style cast
					// - func() < value where func is a function call
					// In both cases, '<' after the call expression is a comparison operator, not
					// the start of template arguments. This is because:
					// 1. The result of a function/constructor call is a value, not a template name
					// 2. C++ doesn't allow template arguments to follow call expressions
					// Note: This is safe because if a function returns a template type, the template
					// instantiation happens at the function definition, not at the call site.
					could_be_template_name = false;
				} else if (std::holds_alternative<QualifiedIdentifierNode>(expr) ||
				           std::holds_alternative<MemberAccessNode>(expr)) {
					// For qualified identifiers like R1<T>::num or member access expressions,
					// we need to check if the final member could be a template.
					// In TemplateArgument context, patterns like _R1::num < _R2::num> should be
					// parsed as comparisons, not as _R1::num<_R2::num> (template instantiation).
					// 
					// The key insight is: for dependent member access (where the base is a template
					// parameter), the member is likely a static data member, not a member template.
					// Even if could_be_template_arguments() succeeds (because _R2::num> looks like
					// valid template arguments), we should prefer treating < as comparison in
					// TemplateArgument context.
					//
					// Strategy:
					// 1. Extract the final member name from the qualified identifier
					// 2. Check if it's a known template (class or variable template)
					// 3. If not a known template AND we're in TemplateArgument context,
					//    treat < as comparison operator
					
					std::string_view member_name;
					if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
						const auto& qual_id = std::get<QualifiedIdentifierNode>(expr);
						member_name = qual_id.name();
					} else {
						const auto& member_access = std::get<MemberAccessNode>(expr);
						member_name = member_access.member_name();
					}
					
					// Check if the member is a known template
					auto template_opt = gTemplateRegistry.lookupTemplate(member_name);
					auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(member_name);
					auto alias_template_opt = gTemplateRegistry.lookup_alias_template(member_name);
					
					if (template_opt.has_value() || var_template_opt.has_value() || alias_template_opt.has_value()) {
						// Member is a known template, allow template argument parsing
						could_be_template_name = true;
					} else if (context == ExpressionContext::TemplateArgument) {
						// Member is NOT a known template and we're parsing template arguments
						// This is likely a pattern like: integral_constant<bool, _R1::num < _R2::num>
						// where < is a comparison operator, not template arguments
						FLASH_LOG(Parser, Debug, "In TemplateArgument context, member '", member_name, 
						          "' is not a known template - treating '<' as comparison operator");
						could_be_template_name = false;
					} else {
						// Not in TemplateArgument context, be conservative and allow template parsing
						could_be_template_name = true;
					}
				} else {
					// Not a simple identifier, could be a complex expression that needs template args
					could_be_template_name = true;
				}
			} else {
				// Not an expression node, be conservative and allow template parsing
				could_be_template_name = true;
			}
			
			// Use lookahead to check if this could be template arguments
			// In Decltype context, be more aggressive about treating < as template arguments
			if (could_be_template_name && could_be_template_arguments()) {
				FLASH_LOG(Parser, Debug, "Confirmed: '<' starts template arguments, not comparison operator");
				// Template arguments were successfully parsed by could_be_template_arguments()
				// The parse_explicit_template_arguments() call inside it already consumed the tokens
				// We need to re-parse to get the actual template arguments
				auto template_args = parse_explicit_template_arguments();
				
				// Check if followed by '::' for qualified member access
				// This handles patterns like: Base<T>::member(args)
				if (peek() == "::"_tok) {
					advance(); // consume '::'
					
					// Expect member name
					if (!peek().is_identifier()) {
						return ParseResult::error("Expected identifier after '::'", current_token_);
					}
					Token member_token = peek_info();
					advance(); // consume member name
					
					// Build the qualified name for lookup
					std::string_view base_name;
					if (result.node()->is<ExpressionNode>()) {
						const auto& expr = result.node()->as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(expr)) {
							base_name = std::get<IdentifierNode>(expr).name();
						}
					}
					
					// Check if followed by '(' for function call
					if (peek() == "("_tok) {
						advance(); // consume '('
						
						auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
							.handle_pack_expansion = true,
							.collect_types = true,
							.expand_simple_packs = false
						});
						if (!args_result.success) {
							return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
						}
						
						if (!consume(")"_tok)) {
							return ParseResult::error("Expected ')' after function call arguments", current_token_);
						}
						
						// Try to resolve Template<Args>::member to a real member function declaration
						const DeclarationNode* decl_ptr = nullptr;
						const FunctionDeclarationNode* func_decl_ptr = nullptr;

						if (!base_name.empty() && template_args.has_value()) {
							std::string_view instantiated_class_name;
							auto instantiation_result = try_instantiate_class_template(base_name, *template_args);
							if (instantiation_result.has_value() && instantiation_result->is<StructDeclarationNode>()) {
								instantiated_class_name = StringTable::getStringView(instantiation_result->as<StructDeclarationNode>().name());
							} else {
								instantiated_class_name = get_instantiated_class_name(base_name, *template_args);
							}

							auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_class_name));
							if (type_it != gTypesByName.end() && type_it->second) {
								const StructTypeInfo* struct_info = type_it->second->getStructInfo();
								if (struct_info) {
									StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_token.value());
									const FunctionDeclarationNode* first_name_match = nullptr;
									size_t call_arg_count = args_result.args.size();
									for (const auto& member_func : struct_info->member_functions) {
										if (member_func.getName() == member_name_handle && member_func.function_decl.is<FunctionDeclarationNode>()) {
											const FunctionDeclarationNode& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
											if (!first_name_match) {
												first_name_match = &candidate;
											}
											if (candidate.parameter_nodes().size() == call_arg_count) {
												func_decl_ptr = &candidate;
												decl_ptr = &func_decl_ptr->decl_node();
												break;
											}
										}
									}
									if (!decl_ptr && first_name_match) {
										func_decl_ptr = first_name_match;
										decl_ptr = &func_decl_ptr->decl_node();
									}
								}
							}
						}

						// Fall back to forward declaration if lookup failed
						if (!decl_ptr) {
							auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
							auto forward_decl = emplace_node<DeclarationNode>(type_node, member_token);
							decl_ptr = &forward_decl.as<DeclarationNode>();
						}

						auto call_node = emplace_node<ExpressionNode>(
							FunctionCallNode(*decl_ptr, std::move(args_result.args), member_token));
						if (func_decl_ptr && func_decl_ptr->has_mangled_name()) {
							std::get<FunctionCallNode>(call_node.as<ExpressionNode>()).set_mangled_name(func_decl_ptr->mangled_name());
						}
						result = ParseResult::success(call_node);
						continue;
					}
					
					// Not a function call - just a qualified identifier access
					auto ident_node = emplace_node<ExpressionNode>(IdentifierNode(member_token));
					result = ParseResult::success(ident_node);
					continue;
				}
				
				// Note: We don't directly use template_args here because the postfix operator loop
				// will handle function calls with template arguments. We just needed to prevent
				// the binary operator loop from consuming '<' as a comparison operator.
				// Continue to the next iteration to let postfix operators handle this.
				continue;
			}
			// If could_be_template_arguments() returned false, fall through to treat '<' as operator
		}

		// Get the precedence of the current operator
		int current_operator_precedence =
			get_operator_precedence(peek_info().value());

		// If the current operator has lower precedence than the provided
		// precedence, stop parsing the expression
		if (current_operator_precedence < precedence) {
			break;
		}

		// Consume the operator token
		Token operator_token = current_token_;
		advance();

		// Parse the right-hand side expression
		ParseResult rhs_result = parse_expression(current_operator_precedence + 1, context);
		if (rhs_result.is_error()) {
			return rhs_result;
		}

		if (auto leftNode = result.node()) {
			if (auto rightNode = rhs_result.node()) {
				// SFINAE: validate binary operator for struct types
				// When in SFINAE context (e.g., decltype(a + b)), check that the
				// operator is actually defined for the operand types. For struct types,
				// this means checking member operator overloads and free operator functions.
				if (in_sfinae_context_ && !sfinae_type_map_.empty()) {
					auto resolve_operand_type_index = [&](const ASTNode& operand) -> TypeIndex {
						if (!operand.is<ExpressionNode>()) return 0;
						const ExpressionNode& expr = operand.as<ExpressionNode>();
						if (!std::holds_alternative<IdentifierNode>(expr)) return 0;
						const auto& ident = std::get<IdentifierNode>(expr);
						auto symbol = lookup_symbol(ident.nameHandle());
						if (!symbol.has_value()) return 0;
						const DeclarationNode* decl = get_decl_from_symbol(*symbol);
						if (!decl) return 0;
						if (!decl->type_node().is<TypeSpecifierNode>()) return 0;
						const auto& type_spec = decl->type_node().as<TypeSpecifierNode>();
						if (type_spec.type() != Type::UserDefined && type_spec.type() != Type::Struct) return 0;
						TypeIndex type_idx = type_spec.type_index();
						// Resolve template parameter types via sfinae_type_map_
						if (type_idx < gTypeInfo.size()) {
							StringHandle type_name_handle = gTypeInfo[type_idx].name();
							auto subst_it = sfinae_type_map_.find(type_name_handle);
							if (subst_it != sfinae_type_map_.end()) {
								type_idx = subst_it->second;
							} else {
								// Unresolved template parameter — skip validation
								return 0;
							}
						}
						return type_idx;
					};

					TypeIndex left_type_idx = resolve_operand_type_index(*leftNode);
					TypeIndex right_type_idx = resolve_operand_type_index(*rightNode);

					// If at least one operand is a struct type, validate the operator exists
					if (left_type_idx > 0 || right_type_idx > 0) {
						bool operator_found = false;
						std::string_view op_symbol = operator_token.value();

						// Check member operator overload on the left operand
						if (left_type_idx > 0) {
							auto member_result = findBinaryOperatorOverload(left_type_idx, right_type_idx, op_symbol);
							if (member_result.has_overload) {
								operator_found = true;
							}
						}

						// Check free function operator overload (e.g., operator+(A, B))
						if (!operator_found) {
							StringBuilder op_name_builder;
							op_name_builder.append("operator").append(op_symbol);
							std::string_view op_func_name = op_name_builder.commit();
							auto op_symbol_opt = lookup_symbol(StringTable::getOrInternStringHandle(op_func_name));
							if (op_symbol_opt.has_value()) {
								// Verify the free operator accepts the operand types
								if (op_symbol_opt->is<FunctionDeclarationNode>()) {
									const auto& op_func = op_symbol_opt->as<FunctionDeclarationNode>();
									const auto& op_params = op_func.parameter_nodes();
									// Check first parameter type matches one of the operand types
									if (op_params.size() >= 2 && op_params[0].is<DeclarationNode>()) {
										const auto& p0 = op_params[0].as<DeclarationNode>();
										if (p0.type_node().is<TypeSpecifierNode>()) {
											TypeIndex p0_idx = p0.type_node().as<TypeSpecifierNode>().type_index();
											if (p0_idx == left_type_idx || p0_idx == right_type_idx) {
												operator_found = true;
											}
										}
									}
								}
								// If not a FunctionDeclarationNode, don't conservatively accept —
								// require explicit match for SFINAE correctness
							}
						}

						if (!operator_found) {
							return ParseResult::error("SFINAE: operator not defined for type", operator_token);
						}
					}
				}

				// Create the binary operation and update the result
				auto binary_op = emplace_node<ExpressionNode>(
					BinaryOperatorNode(operator_token, *leftNode, *rightNode));
				result = ParseResult::success(binary_op);
			}
		}
	}

	// Check for ternary operator (condition ? true_expr : false_expr)
	// Ternary has precedence 5 (between assignment=3 and logical-or=7)
	// Only parse ternary if we're at a precedence level that allows it
	if (precedence <= 5 && peek() == "?"_tok) {
		advance();  // Consume '?'
		Token question_token = current_token_;  // Save the '?' token

		// Parse the true expression (allow lower precedence on the right)
		// IMPORTANT: Pass the context to preserve template argument parsing mode
		// This ensures that '<' and '>' inside ternary branches are handled correctly
		// when the ternary is itself inside template arguments (e.g., integral_constant<int, (x < 0) ? -1 : 1>)
		ParseResult true_result = parse_expression(0, context);
		if (true_result.is_error()) {
			return true_result;
		}

		// Expect ':'
		if (peek() != ":"_tok) {
			return ParseResult::error("Expected ':' in ternary operator", current_token_);
		}
		advance();  // Consume ':'

		// Parse the false expression (use precedence 5 for right-associativity)
		// IMPORTANT: Pass the context to preserve template argument parsing mode
		ParseResult false_result = parse_expression(5, context);
		if (false_result.is_error()) {
			return false_result;
		}

		if (auto condition_node = result.node()) {
			if (auto true_node = true_result.node()) {
				if (auto false_node = false_result.node()) {
					// Create the ternary operator node
					auto ternary_op = emplace_node<ExpressionNode>(
						TernaryOperatorNode(*condition_node, *true_node, *false_node, question_token));
					result = ParseResult::success(ternary_op);
				}
			}
		}
	}

	return result;
}

std::optional<TypedNumeric> get_numeric_literal_type(std::string_view text)
{
	// Convert the text to lowercase for case-insensitive parsing
	// and strip digit separators (') which are valid C++14+ syntax
	std::string lowerText;
	lowerText.reserve(text.size());
	for (char c : text) {
		if (c != '\'')
			lowerText.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
	}

	TypedNumeric typeInfo;
	char* end_ptr = nullptr;

	// Check if this is a hex or binary literal FIRST, before checking for exponent
	// This is important because 'e' and 'f' are valid hex digits (a-f)
	bool is_hex_literal = lowerText.find("0x") == 0;
	bool is_binary_literal = lowerText.find("0b") == 0;

	// Check if this is a floating-point literal (contains '.', 'e', or 'E', or has 'f'/'l' suffix)
	// BUT only check for 'e' (exponent) and 'f' (float suffix) if NOT a hex literal
	bool has_decimal_point = lowerText.find('.') != std::string::npos;
	bool has_exponent = !is_hex_literal && lowerText.find('e') != std::string::npos;
	bool has_float_suffix = !is_hex_literal && lowerText.find('f') != std::string::npos;
	bool is_floating_point = has_decimal_point || has_exponent || has_float_suffix;

	if (is_floating_point) {
		// Parse as floating-point literal
		double float_value = std::strtod(lowerText.c_str(), &end_ptr);
		typeInfo.value = float_value;

		// Check suffix to determine float vs double
		std::string_view suffix = end_ptr;

		// Branchless suffix detection using bit manipulation
		// Check for 'f' or 'F' suffix
		bool is_float = (suffix.find('f') != std::string_view::npos);
		// Check for 'l' or 'L' suffix (long double)
		bool is_long_double = (suffix.find('l') != std::string_view::npos) && !is_float;

		// Branchless type selection
		// If is_float: Type::Float, else if is_long_double: Type::LongDouble, else Type::Double
		typeInfo.type = static_cast<Type>(
			static_cast<int>(Type::Float) * is_float +
			static_cast<int>(Type::LongDouble) * is_long_double * (!is_float) +
			static_cast<int>(Type::Double) * (!is_float) * (!is_long_double)
		);

		// Branchless size selection: float=32, double=64, long double=80
		typeInfo.sizeInBits = static_cast<unsigned char>(
			32 * is_float +
			80 * is_long_double * (!is_float) +
			64 * (!is_float) * (!is_long_double)
		);

		typeInfo.typeQualifier = TypeQualifier::None;
		return typeInfo;
	}

	// Integer literal parsing
	if (is_hex_literal) {
		// Hexadecimal literal
		typeInfo.sizeInBits = static_cast<unsigned char>(std::ceil((lowerText.length() - 2) * 4.0 / 8) * 8);
		typeInfo.value = std::strtoull(lowerText.c_str() + 2, &end_ptr, 16);
	}
	else if (is_binary_literal) {
		// Binary literal
		typeInfo.sizeInBits = static_cast<unsigned char>(std::ceil((lowerText.length() - 2) * 1.0 / 8) * 8);
		typeInfo.value = std::strtoull(lowerText.c_str() + 2, &end_ptr, 2);
	}
	else if (lowerText.find("0") == 0 && lowerText.length() > 1 && lowerText[1] != '.') {
		// Octal literal (but not "0." which is a float)
		typeInfo.sizeInBits = static_cast<unsigned char>(std::ceil((lowerText.length() - 1) * 3.0 / 8) * 8);
		typeInfo.value = std::strtoull(lowerText.c_str() + 1, &end_ptr, 8);
	}
	else {
		// Decimal integer literal
		typeInfo.sizeInBits = static_cast<unsigned char>(sizeof(int) * 8);
		typeInfo.value = std::strtoull(lowerText.c_str(), &end_ptr, 10);
	}

	// Check for integer suffixes
	static constexpr std::string_view suffixCharacters = "ul";
	std::string_view suffix = end_ptr;
	if (!suffix.empty() && suffix.find_first_not_of(suffixCharacters) == std::string_view::npos) {
		bool hasUnsigned = suffix.find('u') != std::string_view::npos;
		typeInfo.typeQualifier = hasUnsigned ? TypeQualifier::Unsigned : TypeQualifier::Signed;
		typeInfo.type = hasUnsigned ? Type::UnsignedInt : Type::Int;

		// Count the number of 'l' characters
		auto l_count = std::count(suffix.begin(), suffix.end(), 'l');
		if (l_count > 0) {
			// 'l' suffix: long (size depends on target)
			// 'll' suffix: long long (always 64 bits)
			if (l_count >= 2) {
				typeInfo.sizeInBits = 64;  // long long is always 64 bits
			} else {
				typeInfo.sizeInBits = static_cast<size_t>(get_type_size_bits(Type::Long));  // long is target-dependent
			}
		}
	} else {
		// Default for literals without suffix: signed int
		typeInfo.typeQualifier = TypeQualifier::Signed;
		typeInfo.type = Type::Int;
	}

	return typeInfo;
}


int Parser::get_operator_precedence(const std::string_view& op)
{
	// C++ operator precedence (higher number = higher precedence)
	// Standard precedence order: Shift > Three-Way (<=>)  > Relational
	static const std::unordered_map<std::string_view, int> precedence_map = {
			// Multiplicative (precedence 17)
			{"*", 17},  {"/", 17},  {"%", 17},
			// Additive (precedence 16)
			{"+", 16},  {"-", 16},
			// Shift (precedence 15)
			{"<<", 15}, {">>", 15},
			// Spaceship/Three-way comparison (precedence 14) - C++20 standard compliant
			{"<=>", 14},
			// Relational (precedence 13)
			{"<", 13},  {"<=", 13}, {">", 13},  {">=", 13},
			// Equality (precedence 12)
			{"==", 12}, {"!=", 12},
			// Bitwise AND (precedence 11)
			{"&", 11},
			// Bitwise XOR (precedence 10)
			{"^", 10},
			// Bitwise OR (precedence 9)
			{"|", 9},
			// Logical AND (precedence 8)
			{"&&", 8},
			// Logical OR (precedence 7)
			{"||", 7},
			// Ternary conditional (precedence 5, handled specially in parse_expression)
			{"?", 5},
			// Assignment operators (precedence 3, right-associative, lowest precedence)
			{"=", 3}, {"+=", 3}, {"-=", 3}, {"*=", 3}, {"/=", 3},
			{"%=", 3}, {"&=", 3}, {"|=", 3}, {"^=", 3},
			{"<<=", 3}, {">>=", 3},
			// Comma operator (precedence 1, lowest precedence)
			{",", 1},
	};

	auto it = precedence_map.find(op);
	if (it != precedence_map.end()) {
		return it->second;
	}
	else {
		// Log warning for unknown operators to help debugging
		FLASH_LOG(Parser, Warning, "Unknown operator '", op, "' in get_operator_precedence, returning 0");
		return 0;
	}
}

bool Parser::consume_keyword(const std::string_view& value)
{
	if (peek().is_keyword() &&
		peek_info().value() == value) {
		advance(); // consume keyword
		return true;
	}
	return false;
}

bool Parser::consume_punctuator(const std::string_view& value)
{
	if (peek().is_punctuator() &&
		peek_info().value() == value) {
		advance(); // consume punctuator
		return true;
	}
	return false;
}

// Skip C++ standard attributes like [[nodiscard]], [[maybe_unused]], etc.
void Parser::skip_cpp_attributes()
{
	while (peek() == "["_tok) {
		auto next = peek_info(1);
		if (next.value() == "[") {
			// Found [[
			advance(); // consume first [
			advance(); // consume second [
			
			// Skip everything until ]]
			int bracket_depth = 2;
			while (!peek().is_eof() && bracket_depth > 0) {
				if (peek() == "["_tok) {
					bracket_depth++;
				} else if (peek() == "]"_tok) {
					bracket_depth--;
				}
				advance();
			}
		} else {
			break; // Not [[, stop
		}
	}
	
	// Also skip GCC-style attributes - they often appear together
	skip_gcc_attributes();
}

// Skip GCC-style __attribute__((...)) specifications
void Parser::skip_gcc_attributes()
{
	while (!peek().is_eof() && (peek_info().value() == "__attribute__" || peek_info().value() == "__attribute")) {
		advance(); // consume "__attribute__" or "__attribute"
		
		// Expect ((
		if (peek() != "("_tok) {
			return; // Invalid __attribute__, return
		}
		advance(); // consume first (
		
		if (peek() != "("_tok) {
			return; // Invalid __attribute__, return
		}
		advance(); // consume second (
		
		// Skip everything until ))
		int paren_depth = 2;
		while (!peek().is_eof() && paren_depth > 0) {
			if (peek() == "("_tok) {
				paren_depth++;
			} else if (peek() == ")"_tok) {
				paren_depth--;
			}
			advance();
		}
	}
}

// Skip noexcept specifier: noexcept or noexcept(expression)
void Parser::skip_noexcept_specifier()
{
	if (peek().is_eof()) return;
	
	// Check for noexcept keyword
	if (peek().is_keyword() && peek() == "noexcept"_tok) {
		advance(); // consume 'noexcept'
		
		// Check for optional noexcept(expression)
		if (peek() == "("_tok) {
			advance(); // consume '('
			
			// Skip everything until matching ')'
			int paren_depth = 1;
			while (!peek().is_eof() && paren_depth > 0) {
				if (peek() == "("_tok) {
					paren_depth++;
				} else if (peek() == ")"_tok) {
					paren_depth--;
				}
				advance();
			}
		}
	}
}

// Parse constructor exception specifier (noexcept or throw())
// Returns true if the constructor should be treated as noexcept
// throw() is equivalent to noexcept(true) in C++
bool Parser::parse_constructor_exception_specifier()
{
	bool is_noexcept = false;
	
	// Parse noexcept specifier
	if (peek() == "noexcept"_tok) {
		advance(); // consume 'noexcept'
		is_noexcept = true;
		
		// Check for noexcept(expr) form
		if (peek() == "("_tok) {
			skip_balanced_parens(); // skip the noexcept expression
		}
	}
	
	// Parse throw() (old-style exception specification)
	// throw() is equivalent to noexcept(true) in C++
	if (peek() == "throw"_tok) {
		advance(); // consume 'throw'
		if (peek() == "("_tok) {
			skip_balanced_parens(); // skip throw(...)
		}
		is_noexcept = true;
	}
	
	return is_noexcept;
}

// Skip function trailing specifiers and attributes after parameters
// Handles: const, volatile, &, &&, noexcept, noexcept(...), throw(), = 0, __attribute__((...))
// Stops before: override, final, = default, = delete (callers handle those with semantic info),
//               requires (callers handle with proper parameter scope)
void Parser::skip_function_trailing_specifiers(FlashCpp::MemberQualifiers& out_quals)
{
	// Clear any previously parsed requires clause
	last_parsed_requires_clause_.reset();
	
	// Reset output qualifiers
	out_quals = FlashCpp::MemberQualifiers{};
	
	while (!peek().is_eof()) {
		auto token = peek_info();
		
		// Handle cv-qualifiers
		if (token.type() == Token::Type::Keyword && 
			(token.value() == "const" || token.value() == "volatile")) {
			if (token.value() == "const") out_quals.cv |= CVQualifier::Const;
			else out_quals.cv |= CVQualifier::Volatile;
			advance();
			continue;
		}
		
		// Handle ref-qualifiers (& and &&)
		if (peek() == "&"_tok) {
			out_quals.ref_qualifier = ReferenceQualifier::LValueReference;
			advance();
			continue;
		}
		if (peek() == "&&"_tok) {
			out_quals.ref_qualifier = ReferenceQualifier::RValueReference;
			advance();
			continue;
		}
		
		// Handle noexcept
		if (token.type() == Token::Type::Keyword && token.value() == "noexcept") {
			skip_noexcept_specifier();
			continue;
		}
		
		// Handle throw() (old-style exception specification)
		if (token.type() == Token::Type::Keyword && token.value() == "throw") {
			advance(); // consume 'throw'
			if (peek() == "("_tok) {
				advance(); // consume '('
				int paren_depth = 1;
				while (!peek().is_eof() && paren_depth > 0) {
					if (peek() == "("_tok) paren_depth++;
					else if (peek() == ")"_tok) paren_depth--;
					advance();
				}
			}
			continue;
		}
		
		// NOTE: Do NOT skip 'override' and 'final' here!
		// These keywords have semantic meaning for member functions and need to be
		// parsed and recorded by the calling code (struct parsing handles these).
		// Skipping them here would cause the member function parsing to miss
		// these important virtual function specifiers.
		
		// Handle __attribute__((...))
		if (token.value() == "__attribute__") {
			skip_gcc_attributes();
			continue;
		}
		
		// Stop before trailing requires clause - don't consume it here.
		// Callers like parse_static_member_function need to handle requires clauses
		// themselves so they can set up proper function parameter scope first.
		// This allows requires clauses referencing function parameters to work correctly.
		if (token.type() == Token::Type::Keyword && token.value() == "requires") {
			break;
		}
		
		// Handle pure virtual (= 0) — note: = default and = delete are NOT consumed here;
		// callers (struct body parsing, friend declarations, parse_static_member_function)
		// handle those explicitly so they can record the semantic information.
		if (token.type() == Token::Type::Punctuator && token.value() == "=") {
			auto next = peek_info(1);
			if (next.value() == "0") {
				advance(); // consume '='
				advance(); // consume 0
				continue;
			}
		}
		
		// Not a trailing specifier, stop
		break;
	}
}

// Parse and discard a trailing requires clause if present.
// Used by call sites that don't need to enforce the constraint (e.g., out-of-line definitions
// where the constraint was already recorded during the in-class declaration).
// For call sites that need parameter scope (e.g., parse_static_member_function),
// handle the requires clause directly instead of using this helper.
std::optional<ASTNode> Parser::parse_trailing_requires_clause()
{
	if (peek() == "requires"_tok) {
		Token requires_token = peek_info();
		advance(); // consume 'requires'
		auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (constraint_result.is_error()) {
			FLASH_LOG(Parser, Warning, "Failed to parse trailing requires clause: ", constraint_result.error_message());
			return std::nullopt;
		}
		if (auto node = constraint_result.node()) {
			return emplace_node<RequiresClauseNode>(*node, requires_token);
		}
	}
	return std::nullopt;
}

void Parser::skip_trailing_requires_clause()
{
	(void)parse_trailing_requires_clause();
}

// Consume pointer (*) and reference (& / &&) modifiers, applying them to the type specifier.
// Handles: T*, T**, T&, T&&, T*&, T* const*, etc.
// Per C++20 grammar [dcl.decl], ptr-operator (* cv-qualifier-seq? | & | &&) is part of
// the declarator, not the type-specifier-seq. This helper is called by declarator-parsing
// sites after parse_type_specifier() to consume the ptr-operator portion.
// Also consumes and ignores MSVC-specific pointer modifiers (__ptr32, __ptr64, __w64,
// __unaligned, __uptr, __sptr) that may appear after cv-qualifiers on pointer declarators.
void Parser::consume_pointer_ref_modifiers(TypeSpecifierNode& type_spec) {
	// Microsoft-specific pointer modifier check — same list used in parse_type_specifier()
	auto is_msvc_pointer_modifier = [](std::string_view kw) {
		return kw == "__ptr32" || kw == "__ptr64" || kw == "__w64" ||
		       kw == "__unaligned" || kw == "__uptr" || kw == "__sptr";
	};
	while (peek() == "*"_tok) {
		advance(); // consume '*'
		CVQualifier ptr_cv = parse_cv_qualifiers(); // Parse CV-qualifiers after the * (const, volatile)
		// Consume and ignore Microsoft-specific pointer modifiers
		while (peek().is_keyword() && is_msvc_pointer_modifier(peek_info().value())) {
			advance();
		}
		type_spec.add_pointer_level(ptr_cv);
	}
	if (peek() == "&&"_tok) {
		advance();
		type_spec.set_reference_qualifier(ReferenceQualifier::RValueReference);
	} else if (peek() == "&"_tok) {
		advance();
		type_spec.set_reference_qualifier(ReferenceQualifier::LValueReference);
	}
}

// Consume pointer/reference modifiers after conversion operator target type
// Handles: operator _Tp&(), operator _Tp*(), operator _Tp&&()
void Parser::consume_conversion_operator_target_modifiers(TypeSpecifierNode& target_type) {
	consume_pointer_ref_modifiers(target_type);
}

// Parse a function type parameter list for template argument parsing.
// Expects the parser to be positioned after the opening '(' of the parameter list.
// Parses types separated by commas, handling pack expansion (...), C-style varargs,
// and pointer/reference modifiers. Stops before ')' — caller must consume it.
// Returns true if at least one type was parsed or the list is empty (valid).
bool Parser::parse_function_type_parameter_list(std::vector<Type>& out_param_types) {
	while (peek() != ")"_tok && !peek().is_eof()) {
		// Handle C-style varargs: just '...' (without type before it)
		if (peek() == "..."_tok) {
			advance(); // consume '...'
			break;
		}
		
		auto param_type_result = parse_type_specifier();
		if (!param_type_result.is_error() && param_type_result.node().has_value()) {
			TypeSpecifierNode& param_type = param_type_result.node()->as<TypeSpecifierNode>();
			
			// Handle pack expansion (...) after a parameter type
			if (peek() == "..."_tok) {
				advance(); // consume '...'
			}
			
			// Apply pointer/reference modifiers to the parameter type
			consume_pointer_ref_modifiers(param_type);
			out_param_types.push_back(param_type.type());
		} else {
			return false; // Parsing failed
		}
		
		if (peek() == ","_tok) {
			advance(); // consume ','
		} else {
			break;
		}
	}
	
	// Handle trailing C-style varargs: _ArgTypes... ...
	// After breaking out of the loop, we might have '...' before ')'
	if (peek() == "..."_tok) {
		advance(); // consume C-style varargs '...'
	}
	
	return true;
}

// Helper to parse static member functions - reduces code duplication across three call sites
bool Parser::parse_static_member_function(
	ParseResult& type_and_name_result,
	bool is_static_constexpr,
	StringHandle struct_name_handle,
	StructDeclarationNode& struct_ref,
	StructTypeInfo* struct_info,
	AccessSpecifier current_access,
	const std::vector<StringHandle>& current_template_param_names
) {
	// Check if this is a function (has '(')
	if (peek() != "("_tok) {
		return false;  // Not a function, caller should handle as static data member
	}

	// This is a static member function
	if (!type_and_name_result.node().has_value() || !type_and_name_result.node()->is<DeclarationNode>()) {
		// Set error in result
		type_and_name_result = ParseResult::error("Expected declaration node for static member function", peek_info());
		return true;  // We handled it (even though it's an error)
	}

	DeclarationNode& decl_node = type_and_name_result.node()->as<DeclarationNode>();

	// Parse function declaration with parameters
	auto func_result = parse_function_declaration(decl_node);
	if (func_result.is_error()) {
		type_and_name_result = func_result;
		return true;
	}

	if (!func_result.node().has_value()) {
		type_and_name_result = ParseResult::error("Failed to create function declaration node", peek_info());
		return true;
	}

	FunctionDeclarationNode& func_decl = func_result.node()->as<FunctionDeclarationNode>();

	// Create a new FunctionDeclarationNode with member function info
	auto [member_func_node, member_func_ref] =
		emplace_node_ref<FunctionDeclarationNode>(decl_node, struct_name_handle);

	// Copy parameters from the parsed function
	for (const auto& param : func_decl.parameter_nodes()) {
		member_func_ref.add_parameter_node(param);
	}

	// Mark as constexpr
	member_func_ref.set_is_constexpr(is_static_constexpr);

	// Mark as static member function (no implicit 'this' parameter)
	member_func_ref.set_is_static(true);

	// Skip any trailing specifiers (const, volatile, noexcept, etc.) after parameter list
	FlashCpp::MemberQualifiers member_quals;
	skip_function_trailing_specifiers(member_quals);

	// Check for trailing requires clause: static int func(int x) requires constraint { ... }
	// This is common in C++20 code, e.g., requires requires { expr; } 
	if (peek() == "requires"_tok) {
		Token requires_token = peek_info(); // Preserve source location
		advance(); // consume 'requires'
		
		// Enter a temporary scope and add function parameters so they're visible in the requires clause
		// Example: static pointer pointer_to(element_type& __r) requires requires { __r; }
		gSymbolTable.enter_scope(ScopeType::Function);
		for (const auto& param : member_func_ref.parameter_nodes()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				gSymbolTable.insert(param_decl.identifier_token().value(), param);
			}
		}
		
		// Parse the constraint expression (can be a requires expression: requires { ... })
		auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		
		// Exit the temporary scope
		gSymbolTable.exit_scope();
		
		if (constraint_result.is_error()) {
			type_and_name_result = constraint_result;
			return true;
		}
		
		// Store the parsed requires clause - it will be evaluated at compile time
		// during template instantiation via the evaluateConstraint() infrastructure.
		last_parsed_requires_clause_ = emplace_node<RequiresClauseNode>(
			*constraint_result.node(),
			requires_token);
		FLASH_LOG(Parser, Debug, "Parsed trailing requires clause for static member function (compile-time evaluation)");
	}

	// Parse function body if present
	if (peek() == "{"_tok) {
		// DELAYED PARSING: Save the current position (start of '{')
		SaveHandle body_start = save_token_position();

		// Look up the struct type
		auto type_it = gTypesByName.find(struct_name_handle);
		size_t struct_type_idx = 0;
		if (type_it != gTypesByName.end()) {
			struct_type_idx = type_it->second->type_index_;
		}

		// Skip over the function body by counting braces
		skip_balanced_braces();

		// Record this for delayed parsing
		delayed_function_bodies_.push_back({
			&member_func_ref,
			body_start,
			{},       // initializer_list_start (not used)
			struct_name_handle,
			struct_type_idx,
			&struct_ref,
			false,    // has_initializer_list
			false,    // is_constructor
			false,    // is_destructor
			nullptr,  // ctor_node
			nullptr,  // dtor_node
			current_template_param_names
		});
	} else if (peek() == "="_tok) {
		// Handle = delete or = default
		advance(); // consume '='
		if (peek() == "delete"_tok) {
			advance(); // consume 'delete'
			if (!consume(";"_tok)) {
				type_and_name_result = ParseResult::error("Expected ';' after '= delete'", peek_info());
				return true;
			}
			// Deleted static member functions are not callable - skip registration
			return true;
		} else if (peek() == "default"_tok) {
			advance(); // consume 'default'
			member_func_ref.set_is_implicit(true);
			if (!consume(";"_tok)) {
				type_and_name_result = ParseResult::error("Expected ';' after '= default'", peek_info());
				return true;
			}
		} else {
			type_and_name_result = ParseResult::error("Expected 'delete' or 'default' after '='", peek_info());
			return true;
		}
	} else if (!consume(";"_tok)) {
		type_and_name_result = ParseResult::error("Expected '{' or ';' after static member function declaration", peek_info());
		return true;
	}

	// Add static member function to struct
	FLASH_LOG(Templates, Debug, "Adding static member function '", decl_node.identifier_token().value(), "' to struct '", StringTable::getStringView(struct_name_handle), "'");
	struct_ref.add_member_function(member_func_node, current_access,
	                               false, false, false, false,
	                               member_quals.is_const(), member_quals.is_volatile());
	FLASH_LOG(Templates, Debug, "Struct '", StringTable::getStringView(struct_name_handle), "' now has ", struct_ref.member_functions().size(), " member functions after adding static member");
	
	// Also register in StructTypeInfo
	auto& registered = struct_info->member_functions.emplace_back(
		decl_node.identifier_token().handle(),
		member_func_node,
		current_access,
		false,  // is_virtual
		false,  // is_pure_virtual
		false   // is_override
	);
	registered.is_const = member_quals.is_const();
	registered.is_volatile = member_quals.is_volatile();

	return true;  // Successfully handled as a function
}

// Helper to parse entire static member block (data or function) - reduces code duplication
ParseResult Parser::parse_static_member_block(
	StringHandle struct_name_handle,
	StructDeclarationNode& struct_ref,
	StructTypeInfo* struct_info,
	AccessSpecifier current_access,
	const std::vector<StringHandle>& current_template_param_names,
	bool use_struct_type_info
) {
	// consume "static" already done by caller
	
	// Handle optional const and constexpr
	CVQualifier cv_qual = CVQualifier::None;
	bool is_static_constexpr = false;
	while (peek().is_keyword()) {
		std::string_view kw = peek_info().value();
		if (kw == "const") {
			cv_qual |= CVQualifier::Const;
			advance();
		} else if (kw == "constexpr") {
			is_static_constexpr = true;
			advance();
		} else if (kw == "inline") {
			advance(); // consume 'inline'
		} else {
			break;
		}
	}
	
	// Parse type and name
	auto type_and_name = parse_type_and_name();
	if (type_and_name.is_error()) {
		return type_and_name;
	}

	// Check if this is a static member function (has '(')
	if (parse_static_member_function(type_and_name, is_static_constexpr,
	                                   struct_name_handle, struct_ref, struct_info,
	                                   current_access, current_template_param_names)) {
		// Function was handled (or error occurred)
		if (type_and_name.is_error()) {
			return type_and_name;
		}
		return ParseResult::success();  // Signal caller to continue
	}

	// If not a function, handle as static data member
	// Optional initializer
	std::optional<ASTNode> init_expr_opt;
	if (peek() == "="_tok) {
		advance(); // consume "="

		// Push struct context so static member references can be resolved
		// This enables expressions like `!is_signed` to find `is_signed` as a static member
		size_t struct_type_index = 0;
		auto type_it = gTypesByName.find(struct_name_handle);
		if (type_it != gTypesByName.end()) {
			struct_type_index = type_it->second->type_index_;
		}
		
		// Push context (reusing MemberFunctionContext for static member lookup)
		// Pass struct_info directly since TypeInfo::struct_info_ hasn't been populated yet
		member_function_context_stack_.push_back({struct_name_handle, struct_type_index, &struct_ref, struct_info});
		
		// Parse initializer expression
		auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		
		// Pop context after parsing
		member_function_context_stack_.pop_back();
		
		if (init_result.is_error()) {
			return init_result;
		}
		init_expr_opt = init_result.node();
	} else if (peek() == "{"_tok) {
		// Brace initialization: static constexpr int x{42};
		advance(); // consume '{'
		
		auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (init_result.is_error()) {
			return init_result;
		}
		init_expr_opt = init_result.node();
		
		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' after brace initializer", current_token_);
		}
	}

	// Consume semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after static member declaration", peek_info());
	}

	// Get the declaration and type specifier
	if (!type_and_name.node().has_value()) {
		return ParseResult::error("Expected static member declaration", peek_info());
	}
	const DeclarationNode& decl = type_and_name.node()->as<DeclarationNode>();
	const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

	// Register static member in struct info
	// Calculate size and alignment for the static member (handles pointers/references correctly)
	auto [member_size, member_alignment] = calculateMemberSizeAndAlignment(type_spec);
	ReferenceQualifier ref_qual = type_spec.reference_qualifier();
	int ptr_depth = static_cast<int>(type_spec.pointer_depth());

	// Register the static member
	StringHandle static_member_name_handle = decl.identifier_token().handle();
	
	// Determine the access specifier to use
	AccessSpecifier access = current_access;
	if (use_struct_type_info) {
		// For template specializations that use struct_type_info.getStructInfo()
		// We need to get it from the global map
		auto type_it = gTypesByName.find(struct_name_handle);
		if (type_it != gTypesByName.end() && type_it->second->getStructInfo()) {
			type_it->second->getStructInfo()->addStaticMember(
				static_member_name_handle,
				type_spec.type(),
				type_spec.type_index(),
				member_size,
				member_alignment,
				AccessSpecifier::Public,  // Full specializations use Public
				init_expr_opt,
				cv_qual,
				ref_qual,
				ptr_depth
			);
		}
	} else {
		// Normal case - use provided struct_info directly
		struct_info->addStaticMember(
			static_member_name_handle,
			type_spec.type(),
			type_spec.type_index(),
			member_size,
			member_alignment,
			access,
			init_expr_opt,
			cv_qual,
			ref_qual,
			ptr_depth
		);
	}

	return ParseResult::success();  // Signal caller to continue
}

// Parse Microsoft __declspec(...) attributes and return linkage
Linkage Parser::parse_declspec_attributes()
{
	Linkage linkage = Linkage::None;
	
	// Parse all __declspec attributes
	while (peek() == "__declspec"_tok) {
		advance(); // consume "__declspec"
		
		if (!consume("("_tok)) {
			return linkage; // Invalid __declspec, return what we have
		}
		
		// Parse the declspec specifier(s)
		while (!peek().is_eof() && peek() != ")"_tok) {
			if (peek().is_identifier() || peek().is_keyword()) {
				std::string_view spec = peek_info().value();
				if (spec == "dllimport") {
					linkage = Linkage::DllImport;
				} else if (spec == "dllexport") {
					linkage = Linkage::DllExport;
				}
				// else: ignore other declspec attributes like align, deprecated, allocator, restrict, etc.
				advance();
			} else if (peek() == "("_tok) {
				// Skip nested parens like __declspec(align(16)) or __declspec(deprecated("..."))
				int paren_depth = 1;
				advance();
				while (!peek().is_eof() && paren_depth > 0) {
					if (peek() == "("_tok) {
						paren_depth++;
					} else if (peek() == ")"_tok) {
						paren_depth--;
					}
					advance();
				}
			} else {
				advance(); // Skip other tokens
			}
		}
		
		if (!consume(")"_tok)) {
			return linkage; // Missing closing paren
		}
	}
	
	return linkage;
}

// Parse calling convention keywords and return the calling convention
CallingConvention Parser::parse_calling_convention()
{
	CallingConvention calling_conv = CallingConvention::Default;
	
	while (!peek().is_eof() &&
	       (peek().is_keyword() || peek().is_identifier())) {
		std::string_view token_val = peek_info().value();
		
		// Look up calling convention in the mapping table using ranges
		auto it = std::ranges::find(calling_convention_map, token_val, &CallingConventionMapping::keyword);
		if (it != std::end(calling_convention_map)) {
			calling_conv = it->convention;
			advance();
		} else {
			break;
		}
	}
	
	return calling_conv;
}

// Parse all types of attributes (both C++ standard and Microsoft-specific)
Parser::AttributeInfo Parser::parse_attributes()
{
	AttributeInfo info;
	
	skip_cpp_attributes();  // C++ [[...]] and GCC __attribute__(...) specifications
	info.linkage = parse_declspec_attributes();
	info.calling_convention = parse_calling_convention();
	
	// Handle potential interleaved attributes (e.g., __declspec(...) [[nodiscard]] __declspec(...))
	if (!peek().is_eof() && (peek() == "["_tok || peek_info().value() == "__attribute__")) {
		// Recurse to handle more attributes (prefer more specific linkage)
		AttributeInfo more_info = parse_attributes();
		if (more_info.linkage != Linkage::None) {
			info.linkage = more_info.linkage;
		}
		if (more_info.calling_convention != CallingConvention::Default) {
			info.calling_convention = more_info.calling_convention;
		}
	}
	
	return info;
}

std::optional<size_t> Parser::parse_alignas_specifier()
{
	// Parse: alignas(constant-expression) or alignas(type-id)
	// C++11 standard allows both forms:
	// 1. alignas(16) - constant expression
	// 2. alignas(double) - type-id
	// 3. alignas(Point) - user-defined type

	// Check if next token is alignas keyword
	if (peek() != "alignas"_tok) {
		return std::nullopt;
	}

	// Save position in case parsing fails
	SaveHandle saved_pos = save_token_position();

	advance(); // consume "alignas"

	if (!consume("("_tok)) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	size_t alignment = 0;
	auto token = peek_info();
	
	// Try to parse as integer literal first (most common case)
	if (token.type() == Token::Type::Literal) {
		// Parse the numeric literal
		std::string_view value_str = token.value();

		// Try to parse as integer
		auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), alignment);
		if (result.ec == std::errc()) {
			advance(); // consume the literal
			
			if (!consume(")"_tok)) {
				restore_token_position(saved_pos);
				return std::nullopt;
			}

			// Validate alignment (must be power of 2)
			if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
				restore_token_position(saved_pos);
				return std::nullopt;
			}

			// Success - discard saved position
			discard_saved_token(saved_pos);
			return alignment;
		}
	}
	
	// Try to parse as type-id (e.g., alignas(Point) or alignas(double))
	if ((token.type() == Token::Type::Keyword || token.type() == Token::Type::Identifier)) {
		// Save position before type specifier attempt to allow fallback to expression
		SaveHandle pre_type_pos = save_token_position();
		// Try to parse a full type specifier to handle all type variations
		ParseResult type_result = parse_type_specifier();
		
		if (!type_result.is_error() && type_result.node().has_value()) {
			// Successfully parsed a type specifier - check if followed by ')'
			if (consume(")"_tok)) {
				const TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
				Type parsed_type = type_spec.type();
				
				// Use existing get_type_alignment function for consistency
				int type_size_bits = get_type_size_bits(parsed_type);
				size_t type_size_bytes = type_size_bits / 8;
				
				// For struct types, look up alignment from struct info
				if (parsed_type == Type::Struct || parsed_type == Type::UserDefined) {
					TypeIndex type_index = type_spec.type_index();
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						if (type_info.isStruct()) {
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info) {
								alignment = struct_info->alignment;
								discard_saved_token(pre_type_pos);
								discard_saved_token(saved_pos);
								return alignment;
							}
						}
					}
				}
				
				// For other types, use the standard alignment function
				alignment = get_type_alignment(parsed_type, type_size_bytes);
				discard_saved_token(pre_type_pos);
				discard_saved_token(saved_pos);
				return alignment;
			}
			// Type parsed but ')' not found - fall through to expression parsing
		}
		// Type parsing failed or ')' not found - restore and try expression
		restore_token_position(pre_type_pos);
	}

	// Try to parse as a constant expression (e.g., alignas(__alignof__(_Tp2::_M_t)))
	// This handles cases where the argument is a complex expression like alignof, sizeof, etc.
	{
		// Restore to just after the '(' for a fresh parse attempt
		restore_token_position(saved_pos);
		saved_pos = save_token_position();
		advance(); // consume "alignas"
		consume("("_tok);

		ParseResult expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (!expr_result.is_error() && expr_result.node().has_value()) {
			if (consume(")"_tok)) {
				// Try to evaluate the expression as a constant
				auto eval_result = try_evaluate_constant_expression(*expr_result.node());
				if (eval_result.has_value()) {
					alignment = static_cast<size_t>(eval_result->value);
					if (alignment > 0 && (alignment & (alignment - 1)) == 0) {
						discard_saved_token(saved_pos);
						return alignment;
					}
				}
				// Expression parsed but couldn't evaluate (template-dependent) - use default alignment
				// In template contexts, actual alignment will be resolved at instantiation time
				discard_saved_token(saved_pos);
				return static_cast<size_t>(8); // Default to 8-byte alignment
			}
		}
	}

	// Failed to parse - restore position
	restore_token_position(saved_pos);
	return std::nullopt;
}
