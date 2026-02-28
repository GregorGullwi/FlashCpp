ParseResult Parser::parse_concept_declaration() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'concept' keyword
	Token concept_token = peek_info();
	if (!consume("concept"_tok)) {
		return ParseResult::error("Expected 'concept' keyword", peek_info());
	}

	// Parse the concept name
	if (!peek().is_identifier()) {
		return ParseResult::error("Expected concept name after 'concept'", current_token_);
	}
	Token concept_name_token = peek_info();
	advance(); // consume concept name

	// For now, we'll support simple concepts without explicit template parameters
	// In full C++20, concepts can have template parameters: template<typename T> concept Name = ...
	// But the simplified syntax is: concept Name = ...;
	// We'll parse the simplified form for now

	// Expect '=' before the constraint expression
	if (peek() != "="_tok) {
		return ParseResult::error("Expected '=' after concept name", current_token_);
	}
	advance(); // consume '='

	// Parse the constraint expression
	// This is typically a requires expression, a type trait, or a boolean expression
	// For now, we'll accept any expression
	auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	if (constraint_result.is_error()) {
		return constraint_result;
	}

	// Expect ';' at the end
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after concept definition", current_token_);
	}

	// Create the ConceptDeclarationNode
	// For simplified concepts (without template<>), we use an empty template parameter list
	std::vector<TemplateParameterNode> template_params;
	
	auto concept_node = emplace_node<ConceptDeclarationNode>(
		concept_name_token,
		std::move(template_params),
		*constraint_result.node(),
		concept_token
	);

	// Register the concept in the global concept registry
	// This will be done in the semantic analysis phase
	// For now, we just return the node

	return saved_position.success(concept_node);
}

// Parse C++20 requires expression: requires(params) { requirements; } or requires { requirements; }
ParseResult Parser::parse_requires_expression() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'requires' keyword
	Token requires_token = current_token_;
	if (!consume("requires"_tok)) {
		return ParseResult::error("Expected 'requires' keyword", current_token_);
	}

	// Enter a new scope for the requires expression parameters
	gSymbolTable.enter_scope(ScopeType::Block);
	
	// RAII guard to ensure scope is exited on all code paths (success or error)
	ScopeGuard scope_guard([&]() { gSymbolTable.exit_scope(); });
	
	// Check if there are parameters: requires(T a, T b) { ... }
	// or no parameters: requires { ... }
	std::vector<ASTNode> parameters;
	if (peek() == "("_tok) {
		advance(); // consume '('
		
		// Parse parameter list (similar to function parameters)
		// For now, we'll accept a simple parameter list
		// Full implementation would parse: Type name, Type name, ...
		while (peek() != ")"_tok) {
			// Parse type
			auto type_result = parse_type_specifier();
			if (type_result.is_error()) {
				return type_result;
			}
			
			// Parse pointer/reference modifiers after the type
			TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
			
			// Check for parenthesized declarator: type(&name)(params) or type(*name)(params)
			// This is used for function pointer/reference parameters
			if (peek() == "("_tok) {
				advance(); // consume '('
				
				// Expect & or * for function reference/pointer
				if (peek() == "&"_tok) {
					advance(); // consume '&'
					type_spec.set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference
				} else if (peek() == "*"_tok) {
					advance(); // consume '*'
					type_spec.add_pointer_level(CVQualifier::None);
				} else {
					return ParseResult::error("Expected '&' or '*' in function declarator", current_token_);
				}
				
				// Parse parameter name
				if (!peek().is_identifier()) {
					return ParseResult::error("Expected identifier in function declarator", current_token_);
				}
				Token param_name = peek_info();
				advance();
				
				// Expect closing ')'
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function declarator name", current_token_);
				}
				
				// Parse function parameter list: (params)
				if (!consume("("_tok)) {
					return ParseResult::error("Expected '(' for function parameter list", current_token_);
				}
				
				// Skip function parameters (we don't need them for requires expressions)
				int paren_depth = 1;
				while (paren_depth > 0 && !peek().is_eof()) {
					if (peek() == "("_tok) {
						paren_depth++;
					} else if (peek() == ")"_tok) {
						paren_depth--;
					}
					if (paren_depth > 0) advance();
				}
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function parameter list", current_token_);
				}
				
				// Create a declaration node for the parameter
				auto decl_node = emplace_node<DeclarationNode>(*type_result.node(), param_name);
				parameters.push_back(decl_node);
				
				// Add parameter to the scope so it can be used in the requires body
				gSymbolTable.insert(param_name.value(), decl_node);
				
				// Check for comma (more parameters) or end
				if (peek() == ","_tok) {
					advance(); // consume ','
				}
				
				continue; // Skip the rest of the loop for this parameter
			}
			
			// Parse cv-qualifiers (const, volatile)
			CVQualifier cv = parse_cv_qualifiers();
			type_spec.add_cv_qualifier(cv);
			
			// Parse pointer/reference declarators (ptr-operator in C++20 grammar)
			consume_pointer_ref_modifiers(type_spec);
			
			// Parse parameter name
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected parameter name in requires expression", current_token_);
			}
			Token param_name = peek_info();
			advance();
			
			// Check if this is a function reference/pointer: type(&name)(params) or type(*name)(params)
			// After the parameter name, if we see '(', it's a function declarator
			if (peek() == "("_tok) {
				// Pattern: void(&f)(T) means f is a reference to function taking T
				advance(); // consume '('
				
				// Parse the function parameter list (simplified - just skip to ')')
				// We don't need the full parameter info for requires expressions
				int paren_depth = 1;
				while (paren_depth > 0 && !peek().is_eof()) {
					if (peek() == "("_tok) {
						paren_depth++;
					} else if (peek() == ")"_tok) {
						paren_depth--;
					}
					if (paren_depth > 0) advance();
				}
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function declarator parameter list", current_token_);
				}
			}
			
			// Create a declaration node for the parameter
			auto decl_node = emplace_node<DeclarationNode>(*type_result.node(), param_name);
			parameters.push_back(decl_node);
			
			// Add parameter to the scope so it can be used in the requires body
			gSymbolTable.insert(param_name.value(), decl_node);
			
			// Check for comma (more parameters) or end
			if (peek() == ","_tok) {
				advance(); // consume ','
			}
		}
		
		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after requires expression parameters", current_token_);
		}
	}

	// Expect '{'
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' to begin requires expression body", current_token_);
	}

	// Enable SFINAE context for the requires expression body
	// In requires expressions, function lookup failures and type errors should not produce errors -
	// they indicate that the constraint is not satisfied (the expression is invalid)
	bool prev_sfinae_context = in_sfinae_context_;
	in_sfinae_context_ = true;
	
	// RAII guard to restore SFINAE context on all code paths
	ScopeGuard sfinae_guard([&]() { in_sfinae_context_ = prev_sfinae_context; });

	// Parse requirements (expressions that must be valid)
	std::vector<ASTNode> requirements;
	while (peek() != "}"_tok) {
		// Check for different types of requirements:
		// 1. Type requirement: typename TypeName;
		// 2. Compound requirement: { expression } -> Type; or just { expression };
		// 3. Nested requirement: requires constraint;
		// 4. Simple requirement: expression;
		
		if (peek().is_keyword() && peek() == "typename"_tok) {
			// Type requirement: typename T::type; or typename Op<Args...>;
			advance(); // consume 'typename'
			
			// Parse the type name - can be identifier, qualified name, or template instantiation
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected type name after 'typename' in requires expression", current_token_);
			}
			Token type_name = peek_info();
			advance();
			
			// Handle qualified names (T::type) and template arguments (Op<Args...>)
			// Only continue parsing if we see :: or < 
			while (!peek().is_eof() && 
			       (peek() == "::"_tok || peek() == "<"_tok)) {
				if (peek() == "::"_tok) {
					advance(); // consume '::'
					if (peek().is_identifier()) {
						advance(); // consume qualified name part
					}
				} else if (peek() == "<"_tok) {
					// Parse template arguments using balanced bracket parsing
					advance(); // consume '<'
					int angle_depth = 1;
					while (angle_depth > 0 && !peek().is_eof()) {
						if (peek() == "<"_tok) {
							angle_depth++;
						} else if (peek() == ">"_tok) {
							angle_depth--;
						} else if (peek() == ">>"_tok) {
							// Handle >> as two >
							angle_depth -= 2;
						}
						advance();
					}
				}
			}
			
			// Create an identifier node for the type requirement
			auto type_req_node = emplace_node<IdentifierNode>(type_name);
			requirements.push_back(type_req_node);
			
			// Expect ';' after type requirement
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after type requirement in requires expression", current_token_);
			}
			continue;
		}
		
		if (peek() == "{"_tok) {
			// Compound requirement: { expression } noexcept_opt -> type-constraint_opt ;
			Token lbrace_token = peek_info();
			advance(); // consume '{'
			
			// Parse the expression - in SFINAE context, failures mean the requirement is not satisfied
			auto expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (expr_result.is_error()) {
				// In a requires expression, expression failure means the requirement is not satisfied
				// Skip the rest of this compound requirement: } noexcept_opt -> type-constraint_opt ;
				int brace_depth = 1;
				while (brace_depth > 0 && !peek().is_eof()) {
					if (peek() == "{"_tok) brace_depth++;
					else if (peek() == "}"_tok) brace_depth--;
					if (brace_depth > 0) advance();
				}
				if (peek() == "}"_tok) advance(); // consume '}'
				// Skip optional noexcept
				if (peek() == "noexcept"_tok) advance();
				// Skip optional -> type-constraint
				if (peek() == "->"_tok) {
					advance(); // consume '->'
					// Skip to semicolon
					while (!peek().is_eof() && peek() != ";"_tok) advance();
				}
				if (peek() == ";"_tok) advance(); // consume ';'
				
				// Create a false boolean literal to indicate unsatisfied requirement
				Token false_token(Token::Type::Keyword, "false"sv, lbrace_token.line(), lbrace_token.column(), lbrace_token.file_index());
				auto false_node = emplace_node<ExpressionNode>(BoolLiteralNode(false_token, false));
				requirements.push_back(false_node);
				continue;
			}
			
			// Expect '}'
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' after compound requirement expression", current_token_);
			}
			
			// Check for optional noexcept specifier
			bool is_noexcept = false;
			if (peek() == "noexcept"_tok) {
				advance(); // consume 'noexcept'
				is_noexcept = true;
			}
			
			// Check for optional return type constraint: -> ConceptName or -> Type
			std::optional<ASTNode> return_type_constraint;
			if (peek() == "->"_tok) {
				advance(); // consume '->'
				
				// Parse the return type constraint (concept name or type)
				// This can be a concept name (identifier) or a type specifier
				auto type_result = parse_type_specifier();
				if (type_result.is_error()) {
					return type_result;
				}
				return_type_constraint = *type_result.node();
			}
			
			// Create CompoundRequirementNode
			auto compound_req = emplace_node<CompoundRequirementNode>(
				*expr_result.node(),
				return_type_constraint,
				is_noexcept,
				lbrace_token
			);
			requirements.push_back(compound_req);
			
			// Expect ';' after compound requirement
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after compound requirement in requires expression", current_token_);
			}
			continue;
		}
		
		if (peek().is_keyword() && peek() == "requires"_tok) {
			// Nested requirement: requires constraint;
			Token nested_requires_token = peek_info();
			advance(); // consume 'requires'
			
			// Parse the nested constraint expression
			auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (constraint_result.is_error()) {
				return constraint_result;
			}
			
			// Create a RequiresClauseNode for the nested requirement
			auto nested_req = emplace_node<RequiresClauseNode>(
				*constraint_result.node(),
				nested_requires_token
			);
			requirements.push_back(nested_req);
			
			// Expect ';' after nested requirement
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after nested requirement in requires expression", current_token_);
			}
			continue;
		}
		
		// Simple requirement: just an expression
		auto req_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (req_result.is_error()) {
			// In a requires expression, expression failure means the requirement is not satisfied
			// Skip to the next ';' and add a false requirement
			while (!peek().is_eof() && peek() != ";"_tok && peek() != "}"_tok) advance();
			if (peek() == ";"_tok) advance();
			
			Token false_token(Token::Type::Keyword, "false"sv, requires_token.line(), requires_token.column(), requires_token.file_index());
			auto false_node = emplace_node<ExpressionNode>(BoolLiteralNode(false_token, false));
			requirements.push_back(false_node);
			continue;
		}
		requirements.push_back(*req_result.node());
		
		// Expect ';' after each requirement
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after requirement in requires expression", current_token_);
		}
	}

	// Expect '}'
	if (!consume("}"_tok)) {
		return ParseResult::error("Expected '}' to end requires expression", current_token_);
	}

	// Scope will be exited automatically by scope_guard

	// Create RequiresExpressionNode
	auto requires_expr_node = emplace_node<RequiresExpressionNode>(
		std::move(requirements),
		requires_token
	);

	return saved_position.success(requires_expr_node);
}

// Parse template parameter list: typename T, int N, ...
