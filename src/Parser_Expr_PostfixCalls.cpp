
// Apply postfix operators (., ->, [], (), ++, --) to an existing expression result
// This allows cast expressions (static_cast, dynamic_cast, etc.) to be followed by member access
// e.g., static_cast<T&&>(t).operator<=>(u)
ParseResult Parser::apply_postfix_operators(ASTNode& start_result)
{
	std::optional<ASTNode> result = start_result;
	
	// Handle postfix operators in a loop
	constexpr int MAX_POSTFIX_ITERATIONS = 100;  // Safety limit to prevent infinite loops
	int postfix_iteration = 0;
	while (result.has_value() && !peek().is_eof() && postfix_iteration < MAX_POSTFIX_ITERATIONS) {
		++postfix_iteration;
		FLASH_LOG_FORMAT(Parser, Debug, "apply_postfix_operators iteration {}: peek token type={}, value='{}'", 
			postfix_iteration, static_cast<int>(peek_info().type()), peek_info().value());
		
		// Check for ++ and -- postfix operators
		if (peek().is_operator()) {
			std::string_view op = peek_info().value();
			if (op == "++" || op == "--") {
				Token operator_token = current_token_;
				advance(); // consume the postfix operator

				// Create a postfix unary operator node (is_prefix = false)
				result = emplace_node<ExpressionNode>(
					UnaryOperatorNode(operator_token, *result, false));
				continue;  // Check for more postfix operators
			}
		}
		
		// Check for member access (. or ->) - these need special handling for .operator<=>()
		if (peek().is_punctuator() && peek() == "."_tok) {
			Token dot_token = peek_info();
			advance(); // consume '.'
			
			// Check for .operator
			if (peek() == "operator"_tok) {
				Token operator_keyword_token = peek_info();
				advance(); // consume 'operator'
				
				// Parse the operator symbol (can be multiple tokens like ==, <=>, () etc.)
				StringBuilder operator_name_builder;
				operator_name_builder.append("operator");
				
				if (peek().is_eof()) {
					return ParseResult::error("Expected operator symbol after 'operator' keyword", operator_keyword_token);
				}
				
				// Handle various operator symbols including multi-character ones
				std::string_view op_char = peek_info().value();
				operator_name_builder.append(op_char);
				advance();
				
				// Handle multi-character operators like >>=, <<=, <=>, etc.
				while (!peek().is_eof()) {
					std::string_view next = peek_info().value();
					if (next == "=" || next == ">" || next == "<") {
						if (op_char == ">" && (next == ">" || next == "=")) {
							operator_name_builder.append(next);
							advance();
							op_char = next;
						} else if (op_char == "<" && (next == "<" || next == "=" || next == ">")) {
							operator_name_builder.append(next);
							advance();
							op_char = next;
						} else if (op_char == "=" && next == ">") {
							// Complete <=> operator
							operator_name_builder.append(next);
							advance();
							break;
						} else if ((op_char == ">" || op_char == "<" || op_char == "!" || op_char == "=") && next == "=") {
							operator_name_builder.append(next);
							advance();
							break;
						} else {
							break;
						}
					} else {
						break;
					}
				}
				
				std::string_view operator_name = operator_name_builder.commit();
				Token operator_name_token(Token::Type::Identifier, operator_name,
				                          operator_keyword_token.line(), operator_keyword_token.column(),
				                          operator_keyword_token.file_index());
				
				// Expect '(' for the operator call
				if (peek() != "("_tok) {
					return ParseResult::error("Expected '(' after operator name in member operator call", current_token_);
				}
				advance(); // consume '('
				
				// Parse function arguments
				auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
					.handle_pack_expansion = true,
					.collect_types = true,
					.expand_simple_packs = false
				});
				if (!args_result.success) {
					return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
				}
				ChunkedVector<ASTNode> args = std::move(args_result.args);
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after member operator call arguments", current_token_);
				}
				
				// Create a member function call node for the operator
				auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, operator_name_token);
				auto& operator_decl = emplace_node<DeclarationNode>(type_spec, operator_name_token).as<DeclarationNode>();
				auto& func_decl_node = emplace_node<FunctionDeclarationNode>(operator_decl).as<FunctionDeclarationNode>();
				
				result = emplace_node<ExpressionNode>(
					MemberFunctionCallNode(*result, func_decl_node, std::move(args), operator_name_token));
				continue;  // Continue checking for more postfix operators
			}
			
			// Not .operator - restore and let the normal postfix handling deal with it
			// (this is a limitation - we'd need to refactor more to handle regular member access here)
			// For now, just break and let the caller handle remaining tokens
			// Actually, we consumed the '.', so we need to handle member access here or error
			
			// Simple member access without operator
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected member name after '.'", dot_token);
			}
			
			Token member_name_token = peek_info();
			advance();
			
			// Check if this is a member function call (followed by '(')
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
				ChunkedVector<ASTNode> args = std::move(args_result.args);
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after member function call arguments", current_token_);
				}
				
				// Create a member function call node
				auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, member_name_token);
				auto& member_decl = emplace_node<DeclarationNode>(type_spec, member_name_token).as<DeclarationNode>();
				auto& func_decl_node = emplace_node<FunctionDeclarationNode>(member_decl).as<FunctionDeclarationNode>();
				
				result = emplace_node<ExpressionNode>(
					MemberFunctionCallNode(*result, func_decl_node, std::move(args), member_name_token));
			} else {
				// Simple member access
				result = emplace_node<ExpressionNode>(
					MemberAccessNode(*result, member_name_token, false)); // false = dot access
			}
			continue;
		}
		
		// Check for -> member access (-> is a punctuator, not an operator)
		if (peek() == "->"_tok) {
			Token arrow_token = peek_info();
			advance(); // consume '->'
			
			// Check for ->operator
			if (peek() == "operator"_tok) {
				// Similar handling to .operator - for brevity, just error for now
				// A full implementation would duplicate the .operator handling
				return ParseResult::error("->operator syntax not yet implemented in apply_postfix_operators", arrow_token);
			}
			
			// Simple member access via arrow
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected member name after '->'", arrow_token);
			}
			
			Token member_name_token = peek_info();
			advance();
			
			// Check if this is a member function call (followed by '(')
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
				ChunkedVector<ASTNode> args = std::move(args_result.args);
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after arrow member function call arguments", current_token_);
				}
				
				auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, member_name_token);
				auto& member_decl = emplace_node<DeclarationNode>(type_spec, member_name_token).as<DeclarationNode>();
				auto& func_decl_node = emplace_node<FunctionDeclarationNode>(member_decl).as<FunctionDeclarationNode>();
				
				result = emplace_node<ExpressionNode>(
					MemberFunctionCallNode(*result, func_decl_node, std::move(args), member_name_token));
			} else {
				// Create arrow access node
				result = emplace_node<ExpressionNode>(
					MemberAccessNode(*result, member_name_token, true)); // true = arrow access
			}
			continue;
		}
		
		// Check for function call operator () - e.g., static_cast<T&&>(x)(args...)
		if (peek().is_punctuator() && peek() == "("_tok) {
			Token paren_token = peek_info();
			advance(); // consume '('

			// Parse function arguments
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = true,
				.expand_simple_packs = false
			});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}
			ChunkedVector<ASTNode> args = std::move(args_result.args);

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			// Create operator() call as a member function call
			Token operator_token(Token::Type::Identifier, "operator()"sv,
			                     paren_token.line(), paren_token.column(), paren_token.file_index());
			auto temp_type = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, operator_token);
			auto temp_decl = emplace_node<DeclarationNode>(temp_type, operator_token);
			auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>());

			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(*result, func_ref, std::move(args), operator_token));
			continue;
		}

		// Check for array subscript operator [] - e.g., static_cast<T*>(p)[i]
		if (peek().is_punctuator() && peek() == "["_tok) {
			Token bracket_token = peek_info();
			advance(); // consume '['

			ParseResult index_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (index_result.is_error()) {
				return index_result;
			}

			if (peek() != "]"_tok) {
				return ParseResult::error("Expected ']' after array index", current_token_);
			}
			advance(); // consume ']'

			if (auto index_node = index_result.node()) {
				result = emplace_node<ExpressionNode>(
					ArraySubscriptNode(*result, *index_node, bracket_token));
				continue;
			} else {
				return ParseResult::error("Invalid array index expression", bracket_token);
			}
		}

		// No more postfix operators we handle here - break
		break;
	}
	
	if (postfix_iteration >= MAX_POSTFIX_ITERATIONS) {
		return ParseResult::error("Parser error: too many postfix operator iterations", current_token_);
	}
	
	if (result.has_value()) {
		return ParseResult::success(*result);
	}
	
	return ParseResult();
}

// Phase 3: New postfix expression layer
// This function handles postfix operators: ++, --, [], (), ::, ., ->
// It calls parse_primary_expression and then handles postfix operators in a loop
ParseResult Parser::parse_postfix_expression(ExpressionContext context)
{
	// First, parse the primary expression
	ParseResult prim_result = parse_primary_expression(context);
	if (prim_result.is_error()) {
		return prim_result;
	}
	
	// Phase 3: Postfix operator loop moved from parse_primary_expression
	// This handles postfix operators: ++, --, [], (), ::, ., ->
	// The loop continues until we run out of postfix operators
	// Note: result is now an optional<ASTNode> (extracted from ParseResult) for compatibility with the postfix loop
	
	std::optional<ASTNode> result = prim_result.node();
	
	// Handle postfix operators in a loop
	constexpr int MAX_POSTFIX_ITERATIONS = 100;  // Safety limit to prevent infinite loops
	int postfix_iteration = 0;
	while (result.has_value() && !peek().is_eof() && postfix_iteration < MAX_POSTFIX_ITERATIONS) {
		++postfix_iteration;
		FLASH_LOG_FORMAT(Parser, Debug, "Postfix operator iteration {}: peek token type={}, value='{}'", 
			postfix_iteration, static_cast<int>(peek_info().type()), peek_info().value());
		if (peek().is_operator()) {
			std::string_view op = peek_info().value();
			if (op == "++" || op == "--") {
				Token operator_token = current_token_;
				advance(); // consume the postfix operator

				// Create a postfix unary operator node (is_prefix = false)
				result = emplace_node<ExpressionNode>(
					UnaryOperatorNode(operator_token, *result, false));
				continue;  // Check for more postfix operators
			}
		}

		// Check for function call operator () - for operator() overload or function pointer call
		if (peek().is_punctuator() && peek() == "("_tok) {
			// Check if the result is a member access to a function pointer
			// If so, we should create a function pointer call instead of operator() call
			bool is_function_pointer_call = false;
			const MemberAccessNode* member_access = nullptr;
			
			if (result->is<ExpressionNode>()) {
				const ExpressionNode& expr = result->as<ExpressionNode>();
				if (std::holds_alternative<MemberAccessNode>(expr)) {
					member_access = &std::get<MemberAccessNode>(expr);
					
					// Check if this member is a function pointer
					// We need to look up the struct type and find the member
					if (!member_function_context_stack_.empty()) {
						const auto& member_ctx = member_function_context_stack_.back();
						if (member_ctx.struct_type_index < gTypeInfo.size()) {
							const TypeInfo& struct_type_info = gTypeInfo[member_ctx.struct_type_index];
							const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
							if (struct_info) {
								std::string_view member_name = member_access->member_name();
								for (const auto& member : struct_info->members) {
									if (member.getName() == StringTable::getOrInternStringHandle(member_name)) {
										if (member.type == Type::FunctionPointer) {
											is_function_pointer_call = true;
										}
										break;
									}
								}
							}
						}
					}
				}
			}
			
			Token paren_token = peek_info();
			advance(); // consume '('

			// Parse function arguments using unified helper
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = false,
				.expand_simple_packs = false
			});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}
			ChunkedVector<ASTNode> args = std::move(args_result.args);

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			if (is_function_pointer_call && member_access) {
				// This is a call through a function pointer member (e.g., this->operation(value, x))
				// Create a FunctionPointerCallNode or use MemberFunctionCallNode with special handling
				// For now, we use MemberFunctionCallNode which will be handled in code generation
				
				// Create a placeholder function declaration with the member name
				Token member_token(Token::Type::Identifier, member_access->member_name(),
				                   paren_token.line(), paren_token.column(), paren_token.file_index());
				auto temp_type = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, member_token);
				auto temp_decl = emplace_node<DeclarationNode>(temp_type, member_token);
				auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>());

				// Create member function call node - code generation will detect this is a function pointer
				result = emplace_node<ExpressionNode>(
					MemberFunctionCallNode(*result, func_ref, std::move(args), member_token));
			} else {
				// Create operator() call as a member function call
				// The member function name is "operator()"
				Token operator_token(Token::Type::Identifier, "operator()"sv,
				                     paren_token.line(), paren_token.column(), paren_token.file_index());

				// Create a temporary function declaration for operator()
				// This will be resolved during code generation
				auto temp_type = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, operator_token);
				auto temp_decl = emplace_node<DeclarationNode>(temp_type, operator_token);
				auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>());

				// Create member function call node for operator()
				result = emplace_node<ExpressionNode>(
					MemberFunctionCallNode(*result, func_ref, std::move(args), operator_token));
			}
			continue;
		}

		// Check for array subscript operator []
		if (peek().is_punctuator() && peek() == "["_tok) {
			Token bracket_token = peek_info();
			advance(); // consume '['

			// Parse the index expression
			ParseResult index_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (index_result.is_error()) {
				return index_result;
			}

			// Expect closing ']'
			if (peek() != "]"_tok) {
				return ParseResult::error("Expected ']' after array index", current_token_);
			}
			advance(); // consume ']'

			// Create array subscript node
			if (auto index_node = index_result.node()) {
				result = emplace_node<ExpressionNode>(
					ArraySubscriptNode(*result, *index_node, bracket_token));
				continue;  // Check for more postfix operators (e.g., arr[i][j])
			} else {
				return ParseResult::error("Invalid array index expression", bracket_token);
			}
		}

		// Check for scope resolution operator :: (namespace/class member access)
		if (peek().is_punctuator() && peek() == "::"_tok) {
			// Handle namespace::member or class::static_member syntax
			// We have an identifier (in result), now parse :: and the member name
			
			// Special case: obj.Base::member() - qualified member access through base class
			// When result is a MemberAccessNode, the :: is qualifying the member, not
			// the expression. Rewrite as member access with the final qualified name.
			if (result->is<ExpressionNode>()) {
				const ExpressionNode& expr = result->as<ExpressionNode>();
				if (std::holds_alternative<MemberAccessNode>(expr)) {
					const auto& member_access = std::get<MemberAccessNode>(expr);
					ASTNode object = member_access.object();
					bool is_arrow = member_access.is_arrow();
					
					// Save position before consuming any tokens so we can restore the
					// entire chain if we hit a non-identifier after any '::' in the chain
					// (e.g., obj.Base::~Base(), obj.Base::Inner::~Inner(), obj.Base::operator==())
					auto saved_pos = save_token_position();
					advance(); // consume '::'
					
					// Skip 'template' keyword if present (dependent context disambiguator)
					if (peek() == "template"_tok) advance();
					
					// Consume all qualified parts: Base::Inner::member
					// Each iteration consumes one identifier; if followed by :: we loop again
					bool handled = false;
					while (peek().is_identifier()) {
						Token qualified_member_token = peek_info();
						advance();
						
						if (peek() == "::"_tok) {
							advance(); // consume '::'
							if (peek() == "template"_tok) advance();
							continue; // keep consuming qualified parts
						}
						
						// This is the final member name
						// Check if it's a member function call
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
							ChunkedVector<ASTNode> args = std::move(args_result.args);
							if (!consume(")"_tok)) {
								return ParseResult::error("Expected ')' after qualified member function call", current_token_);
							}
							auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, qualified_member_token);
							auto& member_decl = emplace_node<DeclarationNode>(type_spec, qualified_member_token).as<DeclarationNode>();
							auto& func_decl_node = emplace_node<FunctionDeclarationNode>(member_decl).as<FunctionDeclarationNode>();
							result = emplace_node<ExpressionNode>(
								MemberFunctionCallNode(object, func_decl_node, std::move(args), qualified_member_token));
						} else {
							// Simple qualified member access
							result = emplace_node<ExpressionNode>(
								MemberAccessNode(object, qualified_member_token, is_arrow));
						}
						handled = true;
						break;
					}
					
					// Handle qualified operator call on member: obj.Base::operator=()
					if (!handled && peek() == "operator"_tok) {
						advance(); // consume 'operator'
						Token operator_keyword_token = current_token_;
						std::string_view op_name;
						if (auto err = parse_operator_name(operator_keyword_token, op_name)) {
							discard_saved_token(saved_pos);
							return std::move(*err);
						}
						Token op_token(Token::Type::Identifier, op_name,
							operator_keyword_token.line(), operator_keyword_token.column(), operator_keyword_token.file_index());
						if (peek() == "("_tok) {
							advance(); // consume '('
							auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
								.handle_pack_expansion = true,
								.collect_types = true,
								.expand_simple_packs = false
							});
							if (!args_result.success) {
								discard_saved_token(saved_pos);
								return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
							}
							if (!consume(")"_tok)) {
								discard_saved_token(saved_pos);
								return ParseResult::error("Expected ')' after qualified operator member call", current_token_);
							}
							auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, op_token);
							auto& member_decl = emplace_node<DeclarationNode>(type_spec, op_token).as<DeclarationNode>();
							auto& func_decl_node = emplace_node<FunctionDeclarationNode>(member_decl).as<FunctionDeclarationNode>();
							result = emplace_node<ExpressionNode>(
								MemberFunctionCallNode(object, func_decl_node, std::move(args_result.args), op_token));
							handled = true;
						}
					}
					
					if (handled) {
						discard_saved_token(saved_pos);
						continue;
					}
					
					// Non-identifier after :: (e.g., ~, operator) — restore entire chain
					// and fall through to the normal :: handler
					restore_token_position(saved_pos);
				}
			}

			advance(); // consume '::'
			
			// Handle qualified operator call: Type::operator=()
			if (peek() == "operator"_tok) {
				// Get the namespace/class name from the current result
				std::string_view namespace_name;
				if (result->is<ExpressionNode>()) {
					const ExpressionNode& expr = result->as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(expr)) {
						namespace_name = std::get<IdentifierNode>(expr).name();
					} else {
						return ParseResult::error("Invalid left operand for '::'" , current_token_);
					}
				} else {
					return ParseResult::error("Expected identifier before '::'" , current_token_);
				}
				advance(); // consume 'operator'
				std::vector<StringType<32>> namespaces;
				namespaces.emplace_back(StringType<32>(namespace_name));
				return parse_qualified_operator_call(current_token_, namespaces);
			}

			// Expect an identifier after ::
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected identifier after '::'", current_token_);
			}
			
			// Get the namespace/class name from the current result
			std::string_view namespace_name;
			if (result->is<ExpressionNode>()) {
				const ExpressionNode& expr = result->as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(expr)) {
					namespace_name = std::get<IdentifierNode>(expr).name();
				} else {
					return ParseResult::error("Invalid left operand for '::'", current_token_);
				}
			} else {
				return ParseResult::error("Expected identifier before '::'", current_token_);
			}
			
			// Now parse the rest as a qualified identifier
			std::vector<StringType<32>> namespaces;
			namespaces.emplace_back(StringType<32>(namespace_name));
			
			Token final_identifier = peek_info();
			advance(); // consume the identifier after ::
			
			// Check if there are more :: following (e.g., A::B::C)
			while (peek() == "::"_tok) {
				namespaces.emplace_back(StringType<32>(final_identifier.value()));
				advance(); // consume ::
				
				// Handle qualified operator call: A::B::operator=()
				if (peek() == "operator"_tok) {
					advance(); // consume 'operator'
					return parse_qualified_operator_call(current_token_, namespaces);
				}

				if (!peek().is_identifier()) {
					return ParseResult::error("Expected identifier after '::'", current_token_);
				}
				final_identifier = peek_info();
				advance(); // consume identifier
			}
			
			// Look up the qualified identifier
			auto qualified_symbol = gSymbolTable.lookup_qualified(namespaces, final_identifier.value());
			
			// Check if this is followed by template arguments: ns::func<Args>
			std::optional<std::vector<TemplateTypeArg>> template_args;
			if (peek() == "<"_tok) {
				template_args = parse_explicit_template_arguments();
				// If parsing failed, it might be a less-than operator, continue normally
			}
			
			// Check if this is a brace initialization: ns::Class<Args>{}
			if (template_args.has_value() && peek() == "{"_tok) {
				// Build the qualified name for lookup
				std::string_view qualified_name = buildQualifiedNameFromStrings(namespaces, final_identifier.value());
				
				// Try to instantiate the class template
				try_instantiate_class_template(qualified_name, *template_args);
				
				// Parse the brace initialization using the helper
				ParseResult brace_init_result = parse_template_brace_initialization(*template_args, qualified_name, final_identifier);
				if (brace_init_result.is_error()) {
					// If parsing failed, fall through to error handling
					FLASH_LOG_FORMAT(Parser, Debug, "Brace initialization parsing failed: {}", brace_init_result.error_message());
				} else if (brace_init_result.node().has_value()) {
					result = brace_init_result.node();
					continue; // Check for more postfix operators
				}
			}
			
			// Check if this is a function call
			if (peek() == "("_tok) {
				advance(); // consume '('
				
				// Parse function arguments using unified helper (collect types for template deduction)
				auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
					.handle_pack_expansion = true,
					.collect_types = true,
					.expand_simple_packs = false
				});
				if (!args_result.success) {
					return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
				}
				ChunkedVector<ASTNode> args = std::move(args_result.args);
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function call arguments", current_token_);
				}
				
				// Get the DeclarationNode
				auto getDeclarationNode = [](const ASTNode& node) -> const DeclarationNode* {
					if (node.is<DeclarationNode>()) {
						return &node.as<DeclarationNode>();
					} else if (node.is<FunctionDeclarationNode>()) {
						return &node.as<FunctionDeclarationNode>().decl_node();
					} else if (node.is<VariableDeclarationNode>()) {
						return &node.as<VariableDeclarationNode>().declaration();
					} else if (node.is<TemplateFunctionDeclarationNode>()) {
						// Handle template function declarations - extract the inner function declaration
						return &node.as<TemplateFunctionDeclarationNode>().function_declaration().as<FunctionDeclarationNode>().decl_node();
					}
					return nullptr;
				};
				
				const DeclarationNode* decl_ptr = qualified_symbol.has_value() ? getDeclarationNode(*qualified_symbol) : nullptr;
				if (qualified_symbol.has_value() && qualified_symbol->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = qualified_symbol->as<FunctionDeclarationNode>();
					if (!func_decl.get_definition().has_value()) {
						StringBuilder class_scope_builder;
						for (size_t i = 0; i < namespaces.size(); ++i) {
							if (i > 0) {
								class_scope_builder.append("::");
							}
							class_scope_builder.append(std::string_view(namespaces[i]));
						}
						std::string_view class_scope = class_scope_builder.commit();
						StringHandle class_name_handle = StringTable::getOrInternStringHandle(class_scope);
						auto class_type_it = gTypesByName.find(class_name_handle);
						if (class_type_it != gTypesByName.end() && class_type_it->second->isTemplateInstantiation()) {
							StringHandle member_name_handle = final_identifier.handle();
							if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(class_name_handle, member_name_handle)) {
								auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(class_name_handle, member_name_handle);
								if (lazy_info_opt.has_value()) {
									auto instantiated_func = instantiateLazyMemberFunction(*lazy_info_opt);
									if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
										qualified_symbol = instantiated_func;
										decl_ptr = &instantiated_func->as<FunctionDeclarationNode>().decl_node();
										LazyMemberInstantiationRegistry::getInstance().markInstantiated(class_name_handle, member_name_handle);
									}
								}
							}
						}
					}
				}
				
				// If symbol not found and we're not in extern "C", try template instantiation
				if (!decl_ptr && current_linkage_ != Linkage::C) {
					// Build qualified template name (e.g., "std::move")
					std::string_view qualified_name = buildQualifiedNameFromStrings(namespaces, final_identifier.value());
					
					// Try explicit template instantiation first if template arguments were provided
					// (e.g., ns::func<true>(args) should use try_instantiate_template_explicit)
					if (template_args.has_value()) {
						std::optional<ASTNode> template_inst = try_instantiate_template_explicit(qualified_name, *template_args);
						if (!template_inst.has_value()) {
							// Also try without namespace prefix
							template_inst = try_instantiate_template_explicit(final_identifier.value(), *template_args);
						}
						if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
							decl_ptr = &template_inst->as<FunctionDeclarationNode>().decl_node();
							FLASH_LOG(Parser, Debug, "Successfully instantiated qualified template with explicit args: ", qualified_name);
						}
					}
					
					// Fall back to argument-type-based deduction
					if (!decl_ptr) {
						// Apply lvalue reference for forwarding deduction on arg_types
						std::vector<TypeSpecifierNode> arg_types = apply_lvalue_reference_deduction(args, args_result.arg_types);
						
						// Try to instantiate the qualified template function
						if (!arg_types.empty()) {
							std::optional<ASTNode> template_inst = try_instantiate_template(qualified_name, arg_types);
							if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
								decl_ptr = &template_inst->as<FunctionDeclarationNode>().decl_node();
								FLASH_LOG(Parser, Debug, "Successfully instantiated qualified template: ", qualified_name);
							}
						}
					}
				}
				
				if (!decl_ptr) {
					// Validate that the namespace path actually exists before creating a forward declaration.
					// This catches errors like f2::func() when only namespace f exists.
					NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
					if (!validateQualifiedNamespace(ns_handle, final_identifier, parsing_template_body_)) {
						return ParseResult::error(
							std::string(StringBuilder().append("Use of undeclared identifier '")
								.append(buildQualifiedNameFromStrings(namespaces, final_identifier.value()))
								.append("'").commit()),
							final_identifier);
					}
					// Namespace exists — create forward declaration for external functions (e.g., std::print)
					auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, final_identifier);
					auto forward_decl = emplace_node<DeclarationNode>(type_node, final_identifier);
					decl_ptr = &forward_decl.as<DeclarationNode>();
				}
				
				// Create function call node
				auto function_call_node = emplace_node<ExpressionNode>(
					FunctionCallNode(*decl_ptr, std::move(args), final_identifier));
				
				// If the function has a pre-computed mangled name, set it on the FunctionCallNode
				if (qualified_symbol.has_value() && qualified_symbol->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = qualified_symbol->as<FunctionDeclarationNode>();
					if (func_decl.has_mangled_name()) {
						std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
						FLASH_LOG(Parser, Debug, "Set mangled name on qualified FunctionCallNode (postfix path): {}", func_decl.mangled_name());
					}
				}
				
				result = function_call_node;
				continue; // Check for more postfix operators
			}
			
			// DEBUG: Log what we have at this point
			if (!peek().is_eof()) {
				FLASH_LOG(Templates, Info, "After function call check: template_args.has_value()=", template_args.has_value(), 
				          ", peek='", peek_info().value(), "', peek.empty()=", peek_info().value().empty());
			}
			
			if (template_args.has_value() && !peek_info().value().empty() && peek() != "("_tok) {
				// This might be a variable template usage with qualified name: ns::var_template<Args>
				// Build the qualified name for lookup
				std::string_view qualified_name = buildQualifiedNameFromStrings(namespaces, final_identifier.value());
				FLASH_LOG(Templates, Info, "Checking for qualified template: ", qualified_name, ", peek='", peek_info().value(), "'");
				
				auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
				if (var_template_opt.has_value()) {
					FLASH_LOG(Templates, Info, "Found variable template: ", qualified_name);
					auto instantiated_var = try_instantiate_variable_template(qualified_name, *template_args);
					if (instantiated_var.has_value()) {
						// Get the instantiated variable name
						std::string_view inst_name;
						if (instantiated_var->is<VariableDeclarationNode>()) {
							const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
							const auto& decl = var_decl.declaration();
							inst_name = decl.identifier_token().value();
						} else if (instantiated_var->is<DeclarationNode>()) {
							const auto& decl = instantiated_var->as<DeclarationNode>();
							inst_name = decl.identifier_token().value();
						} else {
							inst_name = qualified_name;  // Fallback
						}
						
						// Return identifier reference to the instantiated variable
						Token inst_token(Token::Type::Identifier, inst_name, 
						                final_identifier.line(), final_identifier.column(), final_identifier.file_index());
						result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
						FLASH_LOG(Templates, Debug, "Successfully instantiated qualified variable template: ", qualified_name);
						continue; // Check for more postfix operators
					}
				}
				
				// Not a variable template - check if it's a class template that needs instantiation
				// If we have template args, try to instantiate the class template
				// This handles patterns like: std::is_integral<int>::value
				if (!var_template_opt.has_value()) {
					FLASH_LOG(Templates, Info, "Attempting class template instantiation for: ", qualified_name);
					auto instantiation_result = try_instantiate_class_template(qualified_name, *template_args);
					// Update the type_name to use the fully instantiated name (with defaults filled in)
					if (instantiation_result.has_value() && instantiation_result->is<StructDeclarationNode>()) {
						const StructDeclarationNode& inst_struct = instantiation_result->as<StructDeclarationNode>();
						std::string_view instantiated_name = StringTable::getStringView(inst_struct.name());
						// Replace the base template name in namespaces with the instantiated name
						if (!namespaces.empty()) {
							namespaces.back() = StringType<32>(instantiated_name);
							FLASH_LOG(Templates, Debug, "Updated namespace to use instantiated name: ", instantiated_name);
						}
					}
				}
				
				// Fall through to handle as regular qualified identifier if not a variable template
			}
			
			// Check if this might be accessing a static member (e.g., MyClass::value)
			// Try this before checking qualified_symbol, as static member access might not be in symbol table
			std::string_view type_name = namespaces.empty() ? std::string_view() : std::string_view(namespaces.back());
			std::string_view member_name = final_identifier.value();
			
			// Try to resolve the type and trigger lazy static member instantiation if needed
			if (!type_name.empty()) {
				auto type_handle = StringTable::getOrInternStringHandle(type_name);
				auto type_it = gTypesByName.find(type_handle);
				if (type_it != gTypesByName.end()) {
					const TypeInfo* type_info = type_it->second;
					FLASH_LOG(Parser, Debug, "Found type '", type_name, "' with type=", (int)type_info->type_, 
					          " type_index=", type_info->type_index_);
					
					// For type aliases, resolve to the actual type
					if (type_info->type_ == Type::Struct && type_info->type_index_ < gTypeInfo.size()) {
						const TypeInfo& actual_type = gTypeInfo[type_info->type_index_];
						const StructTypeInfo* struct_info = actual_type.getStructInfo();
						if (struct_info) {
							StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
							FLASH_LOG(Parser, Debug, "Triggering lazy instantiation for ", 
							          StringTable::getStringView(struct_info->name), "::", member_name);
							// Trigger lazy static member instantiation if needed
							instantiateLazyStaticMember(struct_info->name, member_handle);
						}
					} else if (type_info->isStruct()) {
						// Direct struct type (not an alias)
						const StructTypeInfo* struct_info = type_info->getStructInfo();
						if (struct_info) {
							StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
							FLASH_LOG(Parser, Debug, "Triggering lazy instantiation for ", 
							          StringTable::getStringView(struct_info->name), "::", member_name);
							// Trigger lazy static member instantiation if needed
							instantiateLazyStaticMember(struct_info->name, member_handle);
						}
					}
				}
			}
			
			if (qualified_symbol.has_value()) {
				// Just a qualified identifier reference (e.g., Namespace::globalValue or Class::staticMember)
				NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
				auto qualified_node_ast = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
				result = emplace_node<ExpressionNode>(qualified_node_ast.as<QualifiedIdentifierNode>());
				continue; // Check for more postfix operators
			} else {
				return ParseResult::error("Undefined qualified identifier", final_identifier);
			}
		}
		
		// Check for member access operator . or -> (or pointer-to-member .* or ->*)
		bool is_arrow_access = false;
		Token operator_start_token;  // Track the operator token for error reporting
		
		if (peek() == "."_tok) {
			operator_start_token = peek_info();
			advance(); // consume '.'
			is_arrow_access = false;
			
			// Check for pointer-to-member operator .*
			if (peek() == "*"_tok) {
				advance(); // consume '*'
				
				// Parse the RHS expression (pointer to member)
				// Pointer-to-member operators have precedence similar to multiplicative operators (17)
				// But we need to stop at lower precedence operators, so use precedence 17
				ParseResult member_ptr_result = parse_expression(17, ExpressionContext::Normal);
				
				if (member_ptr_result.is_error()) {
					return member_ptr_result;
				}
				if (!member_ptr_result.node().has_value()) {
					return ParseResult::error("Expected expression after '.*' operator", current_token_);
				}
				
				// Create PointerToMemberAccessNode
				result = emplace_node<ExpressionNode>(
					PointerToMemberAccessNode(*result, *member_ptr_result.node(), operator_start_token, false));
				continue;  // Check for more postfix operators
			}
		} else if (peek() == "->"_tok) {
			operator_start_token = peek_info();
			advance(); // consume '->'
			is_arrow_access = true;
			
			// Check for pointer-to-member operator ->*
			if (peek() == "*"_tok) {
				advance(); // consume '*'
				
				// Parse the RHS expression (pointer to member)
				// Pointer-to-member operators have precedence similar to multiplicative operators (17)
				// But we need to stop at lower precedence operators, so use precedence 17
				ParseResult member_ptr_result = parse_expression(17, ExpressionContext::Normal);
				if (member_ptr_result.is_error()) {
					return member_ptr_result;
				}
				if (!member_ptr_result.node().has_value()) {
					return ParseResult::error("Expected expression after '->*' operator", current_token_);
				}
				
				// Create PointerToMemberAccessNode
				result = emplace_node<ExpressionNode>(
					PointerToMemberAccessNode(*result, *member_ptr_result.node(), operator_start_token, true));
				continue;  // Check for more postfix operators
			}
			
			// Note: We don't transform ptr->member to (*ptr).member here anymore.
			// Instead, we pass the is_arrow flag to MemberAccessNode, and CodeGen will
			// handle operator-> overload resolution. For raw pointers, it will generate
			// the equivalent of (*ptr).member; for objects with operator->, it will call that.
		} else {
			if (!peek().is_eof()) {
				FLASH_LOG_FORMAT(Parser, Debug, "Postfix loop: breaking, peek token type={}, value='{}'",
					static_cast<int>(peek_info().type()), peek_info().value());
			} else {
				FLASH_LOG(Parser, Debug, "Postfix loop: breaking, no more tokens");
			}
			break;  // No more postfix operators
		}

		// Expect an identifier (member name) OR ~ for pseudo-destructor call
		// Pseudo-destructor pattern: obj.~Type() or ptr->~Type()
		if (peek() == "~"_tok) {
			advance(); // consume '~'
			
			// The destructor name follows the ~
			// This can be a simple identifier (e.g., ~int) or a qualified name (e.g., ~std::string)
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected type name after '~' in pseudo-destructor call", current_token_);
			}
			
			Token destructor_type_token = peek_info();
			advance(); // consume type name
			
			// Build qualified type name if present (e.g., std::string -> handle ~std::string)
			std::string qualified_type_name(destructor_type_token.value());
			while (peek() == "::"_tok) {
				advance(); // consume '::'
				if (!peek().is_identifier()) {
					return ParseResult::error("Expected identifier after '::' in pseudo-destructor type", current_token_);
				}
				qualified_type_name += "::";
				qualified_type_name += peek_info().value();
				advance(); // consume identifier
			}
			
			// Skip template arguments if present (e.g., ~_Rb_tree_node<_Val>())
			if (peek() == "<"_tok) {
				skip_template_arguments();
			}
			
			// Expect '(' for the destructor call
			if (peek() != "("_tok) {
				return ParseResult::error("Expected '(' after destructor name", current_token_);
			}
			advance(); // consume '('
			
			// Expect ')' - destructors take no arguments
			if (peek() != ")"_tok) {
				return ParseResult::error("Expected ')' - pseudo-destructor takes no arguments", current_token_);
			}
			advance(); // consume ')'
			
			FLASH_LOG(Parser, Debug, "Parsed pseudo-destructor call: ~", qualified_type_name);
			
			// Create a PseudoDestructorCallNode to properly represent this expression
			// The result type is always void
			result = emplace_node<ExpressionNode>(
				PseudoDestructorCallNode(*result, qualified_type_name, destructor_type_token, is_arrow_access));
			continue;
		}
		
		// Handle member operator call syntax: obj.operator<=>(...) or ptr->operator++(...)
		// This is valid C++ syntax for calling an operator as a member function by name
		if (peek() == "operator"_tok) {
			Token operator_keyword_token = peek_info();
			advance(); // consume 'operator'
			
			// Parse the operator symbol (can be multiple tokens like ==, <=>, () etc.)
			StringBuilder operator_name_builder;
			operator_name_builder.append("operator");
			
			if (peek().is_eof()) {
				return ParseResult::error("Expected operator symbol after 'operator' keyword", operator_keyword_token);
			}
			
			// Handle various operator symbols including multi-character ones
			std::string_view op = peek_info().value();
			operator_name_builder.append(op);
			advance();
			
			// Handle multi-character operators like >>=, <<=, <=>, (), [], etc.
			while (!peek().is_eof()) {
				std::string_view next = peek_info().value();
				if (next == "=" || next == ">" || next == "<") {
					// Could be part of >>=, <<=, <=>, ==, !=, etc.
					if (op == ">" && (next == ">" || next == "=")) {
						operator_name_builder.append(next);
						advance();
						op = next;
					} else if (op == "<" && (next == "<" || next == "=" || next == ">")) {
						operator_name_builder.append(next);
						advance();
						op = next;
					} else if (op == "=" && next == ">") {
						// Complete <=> operator (we already have operator<= from above)
						operator_name_builder.append(next);
						advance();
						break;
					} else if ((op == ">" || op == "<" || op == "!" || op == "=") && next == "=") {
						operator_name_builder.append(next);
						advance();
						break;
					} else {
						break;
					}
				} else if (op == ")" && next == "(") {
					// operator()
					operator_name_builder.append(next);
					advance();
					break;
				} else if (op == "]" && next == "[") {
					// operator[]
					operator_name_builder.append(next);
					advance();
					break;
				} else {
					break;
				}
			}
			
			std::string_view operator_name = operator_name_builder.commit();
			Token member_operator_name_token(Token::Type::Identifier, operator_name,
			                                  operator_keyword_token.line(), operator_keyword_token.column(),
			                                  operator_keyword_token.file_index());
			
			// Expect '(' for the operator call
			if (peek() != "("_tok) {
				return ParseResult::error("Expected '(' after operator name in member operator call", current_token_);
			}
			advance(); // consume '('
			
			// Parse function arguments
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = true,
				.expand_simple_packs = false
			});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}
			ChunkedVector<ASTNode> args = std::move(args_result.args);
			
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after member operator call arguments", current_token_);
			}
			
			// Create a member function call node for the operator
			// The operator is treated as a regular member function with a special name
			auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, member_operator_name_token);
			auto& operator_decl = emplace_node<DeclarationNode>(type_spec, member_operator_name_token).as<DeclarationNode>();
			auto& func_decl_node = emplace_node<FunctionDeclarationNode>(operator_decl).as<FunctionDeclarationNode>();
			
			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(*result, func_decl_node, std::move(args), member_operator_name_token));
			continue;  // Continue checking for more postfix operators
		}
		
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected member name after '.' or '->'", current_token_);
		}

		Token member_name_token = peek_info();
		advance(); // consume member name

		// Check for explicit template arguments: obj.method<T>(args)
		std::optional<std::vector<TemplateTypeArg>> explicit_template_args;
		if (peek() == "<"_tok) {
			explicit_template_args = parse_explicit_template_arguments();
			if (!explicit_template_args.has_value()) {
				return ParseResult::error("Failed to parse template arguments for member function", current_token_);
			}
		}

		// Check if this is a member function call (followed by '(')
		if (peek() == "("_tok) {
			// This is a member function call: obj.method(args)

			advance(); // consume '('

			// Parse function arguments using unified helper (collect types for template deduction)
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = true,
				.expand_simple_packs = false
			});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}
			ChunkedVector<ASTNode> args = std::move(args_result.args);
			std::vector<TypeSpecifierNode> arg_types = std::move(args_result.arg_types);

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			// Try to get the object's type to check for member function templates
			std::optional<std::string_view> object_struct_name;
			
			// Try to deduce the object type from the result expression
			if (result->is<ExpressionNode>()) {
				const ExpressionNode& expr = result->as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(expr)) {
					const auto& ident = std::get<IdentifierNode>(expr);
					auto symbol = lookup_symbol(ident.nameHandle());
					if (symbol.has_value()) {
						if (const DeclarationNode* decl = get_decl_from_symbol(*symbol)) {
							const auto& type_spec = decl->type_node().as<TypeSpecifierNode>();
							if (type_spec.type() == Type::UserDefined || type_spec.type() == Type::Struct) {
								TypeIndex type_idx = type_spec.type_index();
								if (type_idx < gTypeInfo.size()) {
									object_struct_name = StringTable::getStringView(gTypeInfo[type_idx].name());
									
									// Phase 2: Ensure the struct is instantiated to Full phase for member access
									// This ensures all members are instantiated before accessing them
									StringHandle type_name = gTypeInfo[type_idx].name();
									instantiateLazyClassToPhase(type_name, ClassInstantiationPhase::Full);
								}
							}
						}
					}
				}
			}

			// SFINAE: resolve template parameter types to concrete struct names and validate member existence
			if (in_sfinae_context_ && object_struct_name.has_value() && !sfinae_type_map_.empty()) {
				// The object_struct_name may be a template parameter name (e.g., "U").
				// Resolve it to the concrete struct name using sfinae_type_map_.
				StringHandle obj_name_handle = StringTable::getOrInternStringHandle(*object_struct_name);
				auto subst_it = sfinae_type_map_.find(obj_name_handle);
				if (subst_it != sfinae_type_map_.end()) {
					TypeIndex concrete_idx = subst_it->second;
					if (concrete_idx < gTypeInfo.size()) {
						object_struct_name = StringTable::getStringView(gTypeInfo[concrete_idx].name());
					}
				}
				// Verify the member exists on the resolved struct
				bool member_found = false;
				for (auto& node : ast_nodes_) {
					if (node.is<StructDeclarationNode>()) {
						auto& sn = node.as<StructDeclarationNode>();
						if (sn.name() == *object_struct_name) {
							for (const auto& member : sn.members()) {
								if (member.declaration.is<DeclarationNode>()) {
									if (member.declaration.as<DeclarationNode>().identifier_token().value() == member_name_token.value()) {
										member_found = true;
										break;
									}
								}
							}
							if (!member_found) {
								for (const auto& mf : sn.member_functions()) {
									if (mf.is_constructor || mf.is_destructor) continue;
									if (mf.function_declaration.is<FunctionDeclarationNode>()) {
										const auto& func = mf.function_declaration.as<FunctionDeclarationNode>();
										if (func.decl_node().identifier_token().value() == member_name_token.value()) {
											member_found = true;
											break;
										}
									}
								}
							}
							break;
						}
					}
				}
				if (!member_found) {
					return ParseResult::error("SFINAE: member not found on concrete type", member_name_token);
				}
			}

			// Try to instantiate member function template if applicable
			std::optional<ASTNode> instantiated_func;
			
			// If we have explicit template arguments, use them for instantiation
			if (object_struct_name.has_value() && explicit_template_args.has_value()) {
				instantiated_func = try_instantiate_member_function_template_explicit(
					*object_struct_name,
					member_name_token.value(),
					*explicit_template_args
				);
			}
			// Otherwise, try argument type deduction
			else if (object_struct_name.has_value() && !arg_types.empty()) {
				instantiated_func = try_instantiate_member_function_template(
					*object_struct_name,
					member_name_token.value(),
					arg_types
				);
			}

			// Check for lazy template instantiation
			// If the member function is registered for lazy instantiation, instantiate it now
			if (object_struct_name.has_value() && !instantiating_lazy_member_) {
				std::string_view func_name = member_name_token.value();
				
				if (!func_name.empty()) {
					StringHandle class_name_handle = StringTable::getOrInternStringHandle(*object_struct_name);
					StringHandle func_name_handle = StringTable::getOrInternStringHandle(func_name);
					
					// Check if this function needs lazy instantiation
					if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(class_name_handle, func_name_handle)) {
						FLASH_LOG(Templates, Debug, "Lazy instantiation triggered for: ", *object_struct_name, "::", func_name);
						
						// Get the lazy member info
						auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(class_name_handle, func_name_handle);
						if (lazy_info_opt.has_value()) {
							const LazyMemberFunctionInfo& lazy_info = *lazy_info_opt;
							
							// Set flag to prevent recursive instantiation
							instantiating_lazy_member_ = true;
							
							// Instantiate the function body now
							instantiated_func = instantiateLazyMemberFunction(lazy_info);
							
							// Clear flag
							instantiating_lazy_member_ = false;
							
							// Mark as instantiated
							LazyMemberInstantiationRegistry::getInstance().markInstantiated(class_name_handle, func_name_handle);
							
							FLASH_LOG(Templates, Debug, "Lazy instantiation completed for: ", *object_struct_name, "::", func_name);
						}
					}
				}
			}

			// Use the instantiated function if available, otherwise create temporary placeholder
			FunctionDeclarationNode* func_ref_ptr = nullptr;
			if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
				func_ref_ptr = &instantiated_func->as<FunctionDeclarationNode>();
			} else {
				// Create a temporary function declaration node for the member function
				auto temp_type = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, member_name_token);
				auto temp_decl = emplace_node<DeclarationNode>(temp_type, member_name_token);
				auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(temp_decl.as<DeclarationNode>());
				func_ref_ptr = &func_ref;
			}

			// Create member function call node
			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(*result, *func_ref_ptr, std::move(args), member_name_token));
			continue;
		}

		// Regular member access (not a function call)
		result = emplace_node<ExpressionNode>(
			MemberAccessNode(*result, member_name_token, is_arrow_access));
		continue;  // Check for more postfix operators (e.g., obj.member1.member2)
	}

	// Check if we hit the iteration limit (indicates potential infinite loop)
	if (postfix_iteration >= MAX_POSTFIX_ITERATIONS) {
		FLASH_LOG_FORMAT(Parser, Error, "Hit MAX_POSTFIX_ITERATIONS limit ({}) - possible infinite loop in postfix operator parsing", MAX_POSTFIX_ITERATIONS);
		return ParseResult::error("Parser error: too many postfix operator iterations", current_token_);
	}

	if (result.has_value())
		return ParseResult::success(*result);

	// No result was produced - this should not happen in a well-formed expression
	return ParseResult();  // Return monostate instead of empty success
}

