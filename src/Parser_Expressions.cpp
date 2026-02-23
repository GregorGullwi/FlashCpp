ParseResult Parser::parse_return_statement()
{
	auto current_token_opt = peek_info();
	if (current_token_opt.type() != Token::Type::Keyword ||
		current_token_opt.value() != "return") {
		return ParseResult::error(ParserError::UnexpectedToken,
			current_token_opt);
	}
	Token return_token = current_token_opt;
	FLASH_LOG_FORMAT(Parser, Debug, "parse_return_statement: About to consume 'return'. current_token={}, peek={}", 
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	advance(); // Consume the 'return' keyword

	FLASH_LOG_FORMAT(Parser, Debug, "parse_return_statement: Consumed 'return'. current_token={}, peek={}", 
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");

	// Parse the return expression (if any)
	ParseResult return_expr_result;
	auto next_token_opt = peek_info();
	if ((next_token_opt.type() != Token::Type::Punctuator ||
			next_token_opt.value() != ";")) {
		FLASH_LOG_FORMAT(Parser, Debug, "parse_return_statement: About to parse_expression. current_token={}, peek={}", 
			std::string(current_token_.value()),
			!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		return_expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (return_expr_result.is_error()) {
			return return_expr_result;
		}
	}

	// Consume the semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error(ParserError::MissingSemicolon,
			peek_info());
	}

	if (return_expr_result.has_value()) {
		return ParseResult::success(
			emplace_node<ReturnStatementNode>(return_expr_result.node(), return_token));
	}
	else {
		return ParseResult::success(emplace_node<ReturnStatementNode>(std::nullopt, return_token));
	}
}

// Helper function for parsing C++ cast operators: static_cast, dynamic_cast, const_cast, reinterpret_cast
// Consolidates the duplicated parsing logic for all four cast types
ParseResult Parser::parse_cpp_cast_expression(CppCastKind kind, std::string_view cast_name, const Token& cast_token)
{
	// Expect '<'
	if (peek() != "<"_tok) {
		return ParseResult::error(std::string(StringBuilder().append("Expected '<' after '").append(cast_name).append("'").commit()), current_token_);
	}
	advance(); // consume '<'

	// Parse the target type
	ParseResult type_result = parse_type_specifier();
	if (type_result.is_error() || !type_result.node().has_value()) {
		return ParseResult::error(std::string(StringBuilder().append("Expected type in ").append(cast_name).commit()), current_token_);
	}

	// Parse pointer/reference declarators: *, **, &, && (ptr-operator in C++20 grammar)
	TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
	consume_pointer_ref_modifiers(type_spec);

	// Expect '>'
	if (peek() != ">"_tok) {
		return ParseResult::error(std::string(StringBuilder().append("Expected '>' after type in ").append(cast_name).commit()), current_token_);
	}
	advance(); // consume '>'

	// Expect '('
	if (!consume("("_tok)) {
		return ParseResult::error(std::string(StringBuilder().append("Expected '(' after ").append(cast_name).append("<Type>").commit()), current_token_);
	}

	// Parse the expression to cast
	ParseResult expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	if (expr_result.is_error() || !expr_result.node().has_value()) {
		return ParseResult::error(std::string(StringBuilder().append("Expected expression in ").append(cast_name).commit()), current_token_);
	}

	// Expect ')'
	if (!consume(")"_tok)) {
		return ParseResult::error(std::string(StringBuilder().append("Expected ')' after ").append(cast_name).append(" expression").commit()), current_token_);
	}

	// Create the appropriate cast node based on the kind
	ASTNode cast_expr;
	switch (kind) {
		case CppCastKind::Static:
			cast_expr = emplace_node<ExpressionNode>(
				StaticCastNode(*type_result.node(), *expr_result.node(), cast_token));
			break;
		case CppCastKind::Dynamic:
			cast_expr = emplace_node<ExpressionNode>(
				DynamicCastNode(*type_result.node(), *expr_result.node(), cast_token));
			break;
		case CppCastKind::Const:
			cast_expr = emplace_node<ExpressionNode>(
				ConstCastNode(*type_result.node(), *expr_result.node(), cast_token));
			break;
		case CppCastKind::Reinterpret:
			cast_expr = emplace_node<ExpressionNode>(
				ReinterpretCastNode(*type_result.node(), *expr_result.node(), cast_token));
			break;
	}

	// Apply postfix operators (e.g., .operator<=>(), .member, etc.)
	return apply_postfix_operators(cast_expr);
}

ParseResult Parser::parse_unary_expression(ExpressionContext context)
{
	
	// Check for 'static_cast' keyword
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "static_cast") {
		Token cast_token = current_token_;
		advance(); // consume 'static_cast'
		return parse_cpp_cast_expression(CppCastKind::Static, "static_cast", cast_token);
	}

	// Check for 'dynamic_cast' keyword
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "dynamic_cast") {
		Token cast_token = current_token_;
		advance(); // consume 'dynamic_cast'
		return parse_cpp_cast_expression(CppCastKind::Dynamic, "dynamic_cast", cast_token);
	}

	// Check for 'const_cast' keyword
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "const_cast") {
		Token cast_token = current_token_;
		advance(); // consume 'const_cast'
		return parse_cpp_cast_expression(CppCastKind::Const, "const_cast", cast_token);
	}

	// Check for 'reinterpret_cast' keyword
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "reinterpret_cast") {
		Token cast_token = current_token_;
		advance(); // consume 'reinterpret_cast'
		return parse_cpp_cast_expression(CppCastKind::Reinterpret, "reinterpret_cast", cast_token);
	}

	// Check for C-style cast: (Type)expression
	// This must be checked before parse_primary_expression() which handles parenthesized expressions
	if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == "(") {
		// Save position to potentially backtrack if this isn't a cast
		SaveHandle saved_pos = save_token_position();
		advance(); // consume '('

		// Save the position and build the qualified type name for concept checking
		// This is needed because parse_type_specifier() may parse a qualified name
		// like std::__detail::__class_or_enum but only return the last component in the token
		SaveHandle pre_type_pos = save_token_position();
		StringBuilder qualified_type_name;
		
		// Build qualified name by collecting identifiers and :: tokens
		while (!peek().is_eof()) {
			if (peek().is_identifier()) {
				qualified_type_name.append(peek_info().value());
				advance();
				// Check for :: to continue qualified name
				if (peek() == "::"_tok) {
					qualified_type_name.append("::");
					advance();
				} else {
					break;
				}
			} else {
				break;
			}
		}
		std::string_view qualified_name_view = qualified_type_name.commit();
		
		// Restore position to parse the type properly
		restore_token_position(pre_type_pos);

		// Try to parse as type
		ParseResult type_result = parse_type_specifier();

		if (!type_result.is_error() && type_result.node().has_value()) {
			TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
			
			// Parse pointer/reference declarators (ptr-operator in C++20 grammar)
			consume_pointer_ref_modifiers(type_spec);

			// Check if followed by ')'
			if (consume(")"_tok)) {
				// Before treating this as a C-style cast, verify that the type is actually valid.
				// If type_spec is UserDefined with type_index 0, it means parse_type_specifier()
				// found an unknown identifier and created a placeholder. This is likely a variable
				// name in a parenthesized expression (e.g., "(x) < 8"), not a type cast.
				// We should backtrack and let parse_primary_expression handle it.
				bool is_valid_type = true;
				if (type_spec.type() == Type::UserDefined && type_spec.type_index() == 0) {
					// Check if the token looks like a known type or is in a template context
					// In template bodies, UserDefined with index 0 can be a valid template parameter placeholder
					if (!parsing_template_body_) {
						// Not in a template body, so this is likely a variable, not a type
						is_valid_type = false;
					}
				}
				
				// Check if this "type" is actually a concept - concepts evaluate to boolean
				// and should not be treated as C-style casts.
				// Example: (std::same_as<T, int>) && other_constraint
				// Here, same_as<T, int> is a concept, not a type to cast to.
				if (is_valid_type && type_spec.token().type() == Token::Type::Identifier) {
					std::string_view type_name = type_spec.token().value();
					auto concept_opt = gConceptRegistry.lookupConcept(type_name);
					if (!concept_opt.has_value() && !qualified_name_view.empty()) {
						// Try looking up by the full qualified name
						concept_opt = gConceptRegistry.lookupConcept(qualified_name_view);
					}
					if (concept_opt.has_value()) {
						// This is a concept, not a type - don't treat as C-style cast
						is_valid_type = false;
						FLASH_LOG_FORMAT(Parser, Debug, "Parenthesized expression is a concept '{}', not a C-style cast", 
						                 qualified_name_view.empty() ? type_name : qualified_name_view);
					}
				}
				
				if (is_valid_type) {
					// This is a C-style cast: (Type)expression
					Token cast_token = Token(Token::Type::Punctuator, "cast"sv,
											current_token_.line(), current_token_.column(),
											current_token_.file_index());

					// Parse the expression to cast
					ParseResult expr_result = parse_unary_expression(ExpressionContext::Normal);
					if (expr_result.is_error() || !expr_result.node().has_value()) {
						// Failed to parse expression after what looked like a cast.
						// This means (identifier) was actually a parenthesized expression,
						// not a C-style cast. Fall through to line 291 which restores position.
					} else {
						discard_saved_token(saved_pos);
						// Create a StaticCastNode (C-style casts behave like static_cast in most cases)
						auto cast_expr = emplace_node<ExpressionNode>(
							StaticCastNode(*type_result.node(), *expr_result.node(), cast_token));
						
						// Apply postfix operators (e.g., .operator<=>(), .member, etc.)
						return apply_postfix_operators(cast_expr);
					}
				}
				// If not a valid type, fall through to restore position and try as expression
			}
		}
		
		// Not a cast, restore position and continue to parse_primary_expression
		restore_token_position(saved_pos);
	}

	// Check for '::new' or '::delete' - globally qualified new/delete
	// This is used in standard library (e.g., concepts header) to call global operator new/delete
	[[maybe_unused]] bool is_global_scope_qualified = false;
	if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == "::") {
		// Check if the NEXT token is 'new' or 'delete' (use peek_token(1) to look ahead)
		auto next = peek_info(1);
		if (next.type() == Token::Type::Keyword &&
			(next.value() == "new" || next.value() == "delete")) {
			advance(); // consume '::'
			is_global_scope_qualified = true;
			// Fall through to handle 'new' or 'delete' below
		}
	}

	// Check for 'throw' keyword - throw expressions are valid unary expressions
	// Handles patterns like: (throw bad_optional_access()) or expr ? throw : value
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "throw") {
		Token throw_token = current_token_;
		advance(); // consume 'throw'
		
		// Check if this is a rethrow (throw followed by non-expression punctuator)
		// Rethrow: throw; or throw ) or throw : etc.
		auto next = peek_info();
		if ((next.type() == Token::Type::Punctuator && 
		     (next.value() == ";" || next.value() == ")" || next.value() == ":" || next.value() == ","))) {
			// Rethrow expression - no operand
			return ParseResult::success(emplace_node<ExpressionNode>(
				ThrowExpressionNode(throw_token)));
		}
		
		// Parse the expression to throw
		// Use assignment precedence (2) since throw is a unary operator
		ParseResult expr_result = parse_expression(2, ExpressionContext::Normal);
		if (expr_result.is_error()) {
			return expr_result;
		}
		
		return ParseResult::success(emplace_node<ExpressionNode>(
			ThrowExpressionNode(*expr_result.node(), throw_token)));
	}

	// Check for 'new' keyword (handles both 'new' and '::new')
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "new") {
		advance(); // consume 'new'

		// Check for placement new: new (args...) Type
		// Placement new can have multiple arguments: new (arg1, arg2, ...) Type
		std::optional<ASTNode> placement_address;
		if (peek() == "("_tok) {
			// This could be placement new or constructor call
			// We need to look ahead to distinguish:
			// - new (expr) Type      -> placement new (single arg)
			// - new (arg1, arg2) Type -> placement new (multiple args)
			// - new Type(args)       -> constructor call
			//
			// Strategy: Try to parse as placement new first
			// Parse comma-separated arguments until ')'
			// Then check if followed by a type keyword/identifier
			// If yes, it's placement new; otherwise, backtrack

			ScopedTokenPosition saved_position(*this);
			advance(); // consume '('

			// Parse placement arguments (comma-separated expressions)
			ChunkedVector<ASTNode, 128, 256> placement_args;
			bool parse_error = false;
			
			if (peek() != ")"_tok) {
				while (true) {
					ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (arg_result.is_error()) {
						parse_error = true;
						break;
					}
					
					if (auto arg_node = arg_result.node()) {
						placement_args.push_back(*arg_node);
					}
					
					if (peek() == ","_tok) {
						advance(); // consume ','
					} else {
						break;
					}
				}
			}
			
			// Check for closing ')' and then a type
			if (!parse_error &&
			    peek() == ")"_tok) {
				advance(); // consume ')'

				// Check if next token looks like a type (not end of expression)
				if (!peek().is_eof() &&
				    (peek().is_keyword() ||
				     peek().is_identifier())) {
					// This is placement new - commit the parse
					// For now, we only support single placement argument in NewExpressionNode
					// For multiple args, create a comma expression or handle specially
					if (placement_args.size() > 0) {
						if (placement_args.size() == 1) {
							placement_address = placement_args[0];
						} else {
							// Multiple placement arguments: create a function call style expression
							// For code generation, we'll need to handle this as multiple args to operator new
							// For now, store the first argument (this will need enhancement in IR generation)
							// FIXME: NewExpressionNode needs to support multiple placement args
							placement_address = placement_args[0];
						}
					}
					saved_position.success();  // Discard saved position

					// Emit warning if <new> header was not included
					if (!context_.hasIncludedHeader("new")) {
						FLASH_LOG(Parser, Warning, "placement new used without '#include <new>'. ",
						          "This is a compiler extension. ",
						          "Standard C++ requires: void* operator new(std::size_t, void*);");
					}
				}
				// If not a type, the destructor will restore the position
			}
			// If failed to parse, the destructor will restore the position
		}

		// Parse the type
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}

		auto type_node = type_result.node();
		if (!type_node.has_value()) {
			return ParseResult::error("Expected type after 'new'", current_token_);
		}

		// Check for array allocation: new Type[size] or new Type[size]{initializers}
		if (peek() == "["_tok) {
			advance(); // consume '['

			// Parse the size expression
			ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (size_result.is_error()) {
				return size_result;
			}

			if (!consume("]"_tok)) {
				return ParseResult::error("Expected ']' after array size", current_token_);
			}

			// C++11: Check for initializer list after array size: new Type[n]{init...}
			// This allows aggregate initialization of array elements
			ChunkedVector<ASTNode, 128, 256> array_initializers;
			if (peek() == "{"_tok) {
				advance(); // consume '{'
				
				// Parse initializer list (comma-separated expressions or nested braces)
				if (peek() != "}"_tok) {
					while (true) {
						// Check for nested braces (aggregate initializers for each element)
						if (peek() == "{"_tok) {
							// Parse nested brace initializer
							ParseResult init_result = parse_brace_initializer(type_node->as<TypeSpecifierNode>());
							if (init_result.is_error()) {
								return init_result;
							}
							if (auto init_node = init_result.node()) {
								array_initializers.push_back(*init_node);
							}
						} else {
							// Parse regular expression initializer
							ParseResult init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (init_result.is_error()) {
								return init_result;
							}
							if (auto init_node = init_result.node()) {
								array_initializers.push_back(*init_node);
							}
						}
						
						if (peek() == ","_tok) {
							advance(); // consume ','
						} else {
							break;
						}
					}
				}
				
				if (!consume("}"_tok)) {
					return ParseResult::error("Expected '}' after array initializer list", current_token_);
				}
			}

			// Pass array initializers to code generator
			auto new_expr = emplace_node<ExpressionNode>(
				NewExpressionNode(*type_node, true, size_result.node(), std::move(array_initializers), placement_address));
			return ParseResult::success(new_expr);
		}
		// Check for constructor call: new Type(args)
		else if (peek() == "("_tok) {
			advance(); // consume '('

			ChunkedVector<ASTNode, 128, 256> args;

			// Parse constructor arguments
			if (peek() != ")"_tok) {
				while (true) {
					ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (arg_result.is_error()) {
						return arg_result;
					}

					if (auto arg_node = arg_result.node()) {
						// Check for pack expansion (...) after the argument
						// This handles patterns like: new Type(__args...) in decltype contexts
						if (peek() == "..."_tok) {
							Token ellipsis_token = peek_info();
							advance(); // consume '...'
							
							// Wrap the argument in a PackExpansionExprNode
							auto pack_expr = emplace_node<ExpressionNode>(
								PackExpansionExprNode(*arg_node, ellipsis_token));
							args.push_back(pack_expr);
						} else {
							args.push_back(*arg_node);
						}
					}

					if (peek() == ","_tok) {
						advance(); // consume ','
					} else {
						break;
					}
				}
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after constructor arguments", current_token_);
			}

			auto new_expr = emplace_node<ExpressionNode>(
				NewExpressionNode(*type_node, false, std::nullopt, std::move(args), placement_address));
			return ParseResult::success(new_expr);
		}
		// Simple new: new Type
		else {
			auto new_expr = emplace_node<ExpressionNode>(
				NewExpressionNode(*type_node, false, std::nullopt, {}, placement_address));
			return ParseResult::success(new_expr);
		}
	}

	// Check for 'delete' keyword
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "delete") {
		advance(); // consume 'delete'

		// Check for array delete: delete[]
		bool is_array = false;
		if (peek() == "["_tok) {
			advance(); // consume '['
			if (!consume("]"_tok)) {
				return ParseResult::error("Expected ']' after 'delete['", current_token_);
			}
			is_array = true;
		}

		// Parse the expression to delete
		ParseResult expr_result = parse_unary_expression(ExpressionContext::Normal);
		if (expr_result.is_error()) {
			return expr_result;
		}

		if (auto expr_node = expr_result.node()) {
			auto delete_expr = emplace_node<ExpressionNode>(
				DeleteExpressionNode(*expr_node, is_array));
			return ParseResult::success(delete_expr);
		}
	}

	// Check for 'sizeof' keyword
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "sizeof"sv) {
		// Handle sizeof operator: sizeof(type) or sizeof(expression)
		// Also handle sizeof... operator: sizeof...(pack_name)
		Token sizeof_token = current_token_;
		advance(); // consume 'sizeof'

		// Check for ellipsis to determine if this is sizeof... (parameter pack)
		bool is_sizeof_pack = false;
		if (!peek().is_eof() && 
		    (peek().is_operator() || peek().is_punctuator()) &&
		    peek() == "..."_tok) {
			advance(); // consume '...'
			is_sizeof_pack = true;
		}

		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after 'sizeof'", current_token_);
		}

		if (is_sizeof_pack) {
			// Parse sizeof...(pack_name)
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected parameter pack name after 'sizeof...('", current_token_);
			}
			
			Token pack_name_token = peek_info();
			std::string_view pack_name = pack_name_token.value();
			advance(); // consume pack name
			
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after sizeof... pack name", current_token_);
			}
			
			auto sizeof_pack_expr = emplace_node<ExpressionNode>(SizeofPackNode(pack_name, sizeof_token));
			return ParseResult::success(sizeof_pack_expr);
		}
		else {
			// Try to parse as a type first
			SaveHandle saved_pos = save_token_position();
			ParseResult type_result = parse_type_specifier();

			// If we successfully parsed a type, check for pointer/reference declarators
			// This handles sizeof(void *), sizeof(int **), sizeof(Foo &), etc.
			bool is_complete_type = false;
			if (!type_result.is_error() && type_result.node().has_value()) {
				// Parse pointer/reference declarators (ptr-operator in C++20 grammar)
				TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
				consume_pointer_ref_modifiers(type_spec);
				
				// Now check if ')' follows
				if (peek() == ")"_tok) {
					is_complete_type = true;
				}
			}
			
			if (is_complete_type) {
				// Successfully parsed as type with declarators and ')' follows
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after sizeof type", current_token_);
				}
				discard_saved_token(saved_pos);
				
				// Phase 2: Ensure the type is instantiated to Layout phase for sizeof
				// This ensures size/alignment are computed for lazily instantiated classes
				const TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
				if (type_spec.type() == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
					StringHandle type_name = gTypeInfo[type_spec.type_index()].name();
					instantiateLazyClassToPhase(type_name, ClassInstantiationPhase::Layout);
				}
				
				auto sizeof_expr = emplace_node<ExpressionNode>(SizeofExprNode(*type_result.node(), sizeof_token));
				return ParseResult::success(sizeof_expr);
			}
			else {
				// Not a type (or doesn't look like one), try parsing as expression
				restore_token_position(saved_pos);
				ParseResult expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (expr_result.is_error()) {
					discard_saved_token(saved_pos);
					return ParseResult::error("Expected type or expression after 'sizeof('", current_token_);
				}
				if (!consume(")"_tok)) {
					discard_saved_token(saved_pos);
					return ParseResult::error("Expected ')' after sizeof expression", current_token_);
				}
				discard_saved_token(saved_pos);
				auto sizeof_expr = emplace_node<ExpressionNode>(
					SizeofExprNode::from_expression(*expr_result.node(), sizeof_token));
				return ParseResult::success(sizeof_expr);
			}
		}
	}

	// Check for 'alignof' keyword or '__alignof__' identifier (GCC/Clang extension)
	bool is_alignof_keyword = current_token_.type() == Token::Type::Keyword && current_token_.value() == "alignof"sv;
	bool is_alignof_extension = current_token_.type() == Token::Type::Identifier && current_token_.value() == "__alignof__"sv;
	
	if (is_alignof_keyword || is_alignof_extension) {
		// Handle alignof/alignof operator: alignof(type) or alignof(expression)
		Token alignof_token = current_token_;
		std::string_view alignof_name = current_token_.value();
		advance(); // consume 'alignof' or '__alignof__'

		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after '" + std::string(alignof_name) + "'", current_token_);
		}

		// Try to parse as a type first
		SaveHandle saved_pos = save_token_position();
		ParseResult type_result = parse_type_specifier();

		// If we successfully parsed a type, check for pointer/reference declarators
		// This handles alignof(void *), alignof(int **), alignof(Foo &), etc.
		bool is_complete_type = false;
		if (!type_result.is_error() && type_result.node().has_value()) {
			// Parse pointer/reference declarators (ptr-operator in C++20 grammar)
			TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
			consume_pointer_ref_modifiers(type_spec);
			
			// Now check if ')' follows
			if (peek() == ")"_tok) {
				is_complete_type = true;
			}
		}
		
		if (is_complete_type) {
			// Successfully parsed as type with declarators and ')' follows
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after " + std::string(alignof_name) + " type", current_token_);
			}
			discard_saved_token(saved_pos);
			
			// Phase 2: Ensure the type is instantiated to Layout phase for alignof
			// This ensures size/alignment are computed for lazily instantiated classes
			const TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
			if (type_spec.type() == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
				StringHandle type_name = gTypeInfo[type_spec.type_index()].name();
				instantiateLazyClassToPhase(type_name, ClassInstantiationPhase::Layout);
			}
			
			auto alignof_expr = emplace_node<ExpressionNode>(AlignofExprNode(*type_result.node(), alignof_token));
			return ParseResult::success(alignof_expr);
		}
		else {
			// Not a type (or doesn't look like one), try parsing as expression
			restore_token_position(saved_pos);
			ParseResult expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (expr_result.is_error()) {
				discard_saved_token(saved_pos);
				return ParseResult::error("Expected type or expression after '" + std::string(alignof_name) + "('", current_token_);
			}
			if (!consume(")"_tok)) {
				discard_saved_token(saved_pos);
				return ParseResult::error("Expected ')' after " + std::string(alignof_name) + " expression", current_token_);
			}
			discard_saved_token(saved_pos);
			auto alignof_expr = emplace_node<ExpressionNode>(
				AlignofExprNode::from_expression(*expr_result.node(), alignof_token));
			return ParseResult::success(alignof_expr);
		}
	}

	// Check for 'noexcept' keyword (operator, not specifier)
	// noexcept(expression) returns true if expression is noexcept, false otherwise
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "noexcept"sv) {
		Token noexcept_token = current_token_;
		advance(); // consume 'noexcept'

		// noexcept operator always requires parentheses
		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after 'noexcept'", current_token_);
		}

		// Parse the expression inside noexcept(...)
		ParseResult expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (expr_result.is_error()) {
			return ParseResult::error("Expected expression after 'noexcept('", current_token_);
		}

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after noexcept expression", current_token_);
		}

		auto noexcept_expr = emplace_node<ExpressionNode>(NoexceptExprNode(*expr_result.node(), noexcept_token));
		return ParseResult::success(noexcept_expr);
	}

	// Check for 'typeid' keyword
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "typeid"sv) {
		// Handle typeid operator: typeid(type) or typeid(expression)
		Token typeid_token = current_token_;
		advance(); // consume 'typeid'

		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after 'typeid'", current_token_);
		}

		// Try to parse as a type first
		SaveHandle saved_pos = save_token_position();
		ParseResult type_result = parse_type_specifier();

		// Check if this is really a type by seeing if ')' follows
		// This disambiguates between "typeid(int)" and "typeid(x + 1)" where x might be
		// incorrectly parsed as a user-defined type
		bool is_type_followed_by_paren = !type_result.is_error() && type_result.node().has_value() && 
		                                 peek() == ")"_tok;
		
		if (is_type_followed_by_paren) {
			// Successfully parsed as type and ')' follows
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after typeid type", current_token_);
			}
			discard_saved_token(saved_pos);
			auto typeid_expr = emplace_node<ExpressionNode>(TypeidNode(*type_result.node(), true, typeid_token));
			return ParseResult::success(typeid_expr);
		}
		else {
			// Not a type (or doesn't look like one), try parsing as expression
			restore_token_position(saved_pos);
			ParseResult expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (expr_result.is_error()) {
				discard_saved_token(saved_pos);
				return ParseResult::error("Expected type or expression after 'typeid('", current_token_);
			}
			if (!consume(")"_tok)) {
				discard_saved_token(saved_pos);
				return ParseResult::error("Expected ')' after typeid expression", current_token_);
			}
			discard_saved_token(saved_pos);
			auto typeid_expr = emplace_node<ExpressionNode>(TypeidNode(*expr_result.node(), false, typeid_token));
			return ParseResult::success(typeid_expr);
		}
	}

	// Check for '__builtin_constant_p' intrinsic (GCC/Clang extension - not available in MSVC mode)
	// Returns 1 if the argument can be evaluated at compile time, 0 otherwise
	// Syntax: __builtin_constant_p(expr)
	if (NameMangling::g_mangling_style != NameMangling::ManglingStyle::MSVC &&
	    current_token_.type() == Token::Type::Identifier && current_token_.value() == "__builtin_constant_p"sv) {
		Token builtin_token = current_token_;
		advance(); // consume '__builtin_constant_p'

		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after '__builtin_constant_p'", current_token_);
		}

		// Parse argument: any expression
		ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (arg_result.is_error()) {
			return ParseResult::error("Expected expression as argument to __builtin_constant_p", current_token_);
		}

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after __builtin_constant_p argument", current_token_);
		}

		// Try to evaluate the expression at compile time
		// If it succeeds, __builtin_constant_p returns 1, otherwise 0
		int result = 0;
		if (arg_result.node().has_value()) {
			ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
			auto eval_result = ConstExpr::Evaluator::evaluate(*arg_result.node(), eval_ctx);
			if (eval_result.success()) {
				result = 1;
			}
		}

		// Return a numeric literal with the result (1 or 0)
		auto result_node = emplace_node<ExpressionNode>(
			NumericLiteralNode(builtin_token, static_cast<unsigned long long>(result), Type::Int, TypeQualifier::None, 32));

		return ParseResult::success(result_node);
	}

	// Check for '__builtin_va_arg' intrinsic
	// Special handling needed because second argument is a type, not an expression
	// Syntax: __builtin_va_arg(va_list_var, type)
	if (current_token_.type() == Token::Type::Identifier && current_token_.value() == "__builtin_va_arg"sv) {
		Token builtin_token = current_token_;
		advance(); // consume '__builtin_va_arg'

		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after '__builtin_va_arg'", current_token_);
		}

		// Parse first argument: va_list variable (expression)
		ParseResult first_arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (first_arg_result.is_error()) {
			return ParseResult::error("Expected va_list variable as first argument to __builtin_va_arg", current_token_);
		}

		if (!consume(","_tok)) {
			return ParseResult::error("Expected ',' after first argument to __builtin_va_arg", current_token_);
		}

		// Parse second argument: type specifier
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error() || !type_result.node().has_value()) {
			return ParseResult::error("Expected type as second argument to __builtin_va_arg", current_token_);
		}

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after __builtin_va_arg arguments", current_token_);
		}

		// Create a function call node with both arguments
		// The builtin_va_arg function was registered during initialization
		auto builtin_symbol = gSymbolTable.lookup("__builtin_va_arg");
		if (!builtin_symbol.has_value()) {
			return ParseResult::error("__builtin_va_arg not found in symbol table", builtin_token);
		}

		// The symbol contains a FunctionDeclarationNode, get its underlying DeclarationNode
		const FunctionDeclarationNode& func_decl_node = builtin_symbol->as<FunctionDeclarationNode>();
		const DeclarationNode& func_decl = func_decl_node.decl_node();
		
		// Create arguments vector with both the va_list expression and the type
		ChunkedVector<ASTNode> args;
		args.push_back(*first_arg_result.node());
		args.push_back(*type_result.node());  // Pass type node as second argument
		
		auto builtin_call = emplace_node<ExpressionNode>(
			FunctionCallNode(const_cast<DeclarationNode&>(func_decl), std::move(args), builtin_token));
		
		return ParseResult::success(builtin_call);
	}

	// Check for '__builtin_addressof' intrinsic
	// Returns the actual address of an object, bypassing any overloaded operator&
	// Syntax: __builtin_addressof(obj)
	// 
	// Implementation note: We create a UnaryOperatorNode with the & operator.
	// In FlashCpp's current implementation, unary & operators are not subject to
	// overload resolution (overloaded operators would require a separate overload
	// resolution phase). Therefore, this UnaryOperatorNode will always get the
	// true address, which is the correct behavior for __builtin_addressof.
	//
	// LIMITATION & FUTURE WORK:
	// Currently, FlashCpp does not perform overload resolution on unary operators,
	// so regular & operator also bypasses overloaded operator&. This means both
	// __builtin_addressof and & behave identically. For standard compliance:
	//
	// Plan for standard-compliant operator overloading:
	// 1. Add overload resolution phase after AST construction (before IR generation)
	// 2. For UnaryOperatorNode with &:
	//    a. Check if the operand type has an overloaded operator& (member or non-member)
	//    b. If overloaded operator& exists and applies to regular &:
	//       - Replace UnaryOperatorNode with FunctionCallNode to the overloaded operator
	//    c. If no overload or __builtin_addressof:
	//       - Keep UnaryOperatorNode for direct address-of operation
	// 3. Add a flag to UnaryOperatorNode: is_builtin_addressof
	//    - Set to true only for __builtin_addressof
	//    - Overload resolution will skip operators marked with this flag
	// 4. Implement in OverloadResolution.h:
	//    - resolveUnaryOperator(UnaryOperatorNode&, TypeContext&)
	//    - findOperatorOverload(operator_name, operand_type, is_member)
	// 5. Similar approach needed for other overloadable operators (++, --, etc.)
	//
	// Benefits of this approach:
	// - Standard-compliant: & calls overloaded operator&, __builtin_addressof doesn't
	// - Minimal AST changes: Just add is_builtin_addressof flag
	// - Enables other operator overloading (arithmetic, comparison, etc.)
	// - IR generation remains unchanged (operates on resolved nodes)
	if (current_token_.type() == Token::Type::Identifier && current_token_.value() == "__builtin_addressof"sv) {
		Token builtin_token = current_token_;
		advance(); // consume '__builtin_addressof'

		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after '__builtin_addressof'", current_token_);
		}

		// Parse argument: the object to get the address of
		ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (arg_result.is_error()) {
			return ParseResult::error("Expected expression as argument to __builtin_addressof", current_token_);
		}

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after __builtin_addressof argument", current_token_);
		}

		// Create a unary expression with the AddressOf operator
		// The true parameter indicates this is a prefix operator
		// The fourth parameter (is_builtin_addressof=true) marks this to bypass operator overload resolution
		// Note: __builtin_addressof always gets the true address, bypassing any overloaded operator&
		Token addressof_token = Token(Token::Type::Operator, "&"sv, 
		                               builtin_token.line(), builtin_token.column(), 
		                               builtin_token.file_index());
		
		auto addressof_expr = emplace_node<ExpressionNode>(
			UnaryOperatorNode(addressof_token, *arg_result.node(), true, true));
		
		return ParseResult::success(addressof_expr);
	}

	// Check for GCC complex number operators: __real__ and __imag__
	// These extract the real or imaginary part of a complex number (used in libstdc++ <complex>)
	// Since FlashCpp doesn't support complex arithmetic, treat them as identity operators
	if (current_token_.type() == Token::Type::Identifier) {
		std::string_view val = current_token_.value();
		if (val == "__real__" || val == "__imag__") {
			Token operator_token = current_token_;
			advance();

			// Parse the operand
			ParseResult operand_result = parse_unary_expression(ExpressionContext::Normal);
			if (operand_result.is_error()) {
				return operand_result;
			}

			if (auto operand_node = operand_result.node()) {
				// For now, treat __real__ and __imag__ as identity operators
				// since we don't support complex numbers yet
				// In the future, these would extract the respective components
				return ParseResult::success(*operand_node);
			}

			return ParseResult::error("Expected operand after " + std::string(val), operator_token);
		}
	}

	// Check if the current token is a unary operator
	if (current_token_.type() == Token::Type::Operator) {
		std::string_view op = current_token_.value();

		// Check for unary operators: !, ~, +, -, ++, --, * (dereference), & (address-of)
		if (op == "!" || op == "~" || op == "+" || op == "-" || op == "++" || op == "--" ||
		    op == "*" || op == "&") {
			Token operator_token = current_token_;
			advance();

			// Parse the operand (recursively handle unary expressions)
			ParseResult operand_result = parse_unary_expression(ExpressionContext::Normal);
			if (operand_result.is_error()) {
				return operand_result;
			}

			if (auto operand_node = operand_result.node()) {
				// Special handling for unary + on lambda: decay to function pointer
				if (op == "+" && operand_node->is<LambdaExpressionNode>()) {
					const auto& lambda = operand_node->as<LambdaExpressionNode>();

					// Only captureless lambdas can decay to function pointers
					if (!lambda.captures().empty()) {
						return ParseResult::error("Cannot convert lambda with captures to function pointer", operator_token);
					}

					// For now, just return the lambda itself
					// The code generator will handle the conversion to function pointer
					// TODO: Create a proper function pointer type node
					return ParseResult::success(*operand_node);
				}

				auto unary_op = emplace_node<ExpressionNode>(
					UnaryOperatorNode(operator_token, *operand_node, true));
				return ParseResult::success(unary_op);
			}

			// If operand_node is empty, return error
			return ParseResult::error("Expected operand after unary operator", operator_token);
		}
	}

	// Not a unary operator, parse as postfix expression (which starts with primary expression)
	// Phase 3: Changed to call parse_postfix_expression instead of parse_primary_expression
	// This allows postfix operators (++, --, [], (), ::, ., ->) to be handled in a separate layer
	ParseResult postfix_result = parse_postfix_expression(context);
	return postfix_result;
}

// Trait info for type trait intrinsics - shared between is_known_type_trait_name and parse_primary_expression.
// Keys use single underscore prefix (e.g. "_is_void") so both "__is_void" and "__builtin_is_void"
// can be normalized to the same key via string_view::substr() with zero allocation.
struct TraitInfo {
	TypeTraitKind kind = TypeTraitKind::IsVoid;
	bool is_binary = false;
	bool is_variadic = false;
	bool is_no_arg = false;
};

static const std::unordered_map<std::string_view, TraitInfo> trait_map = {
	{"_is_void", {TypeTraitKind::IsVoid, false, false, false}},
	{"_is_nullptr", {TypeTraitKind::IsNullptr, false, false, false}},
	{"_is_integral", {TypeTraitKind::IsIntegral, false, false, false}},
	{"_is_floating_point", {TypeTraitKind::IsFloatingPoint, false, false, false}},
	{"_is_array", {TypeTraitKind::IsArray, false, false, false}},
	{"_is_pointer", {TypeTraitKind::IsPointer, false, false, false}},
	{"_is_lvalue_reference", {TypeTraitKind::IsLvalueReference, false, false, false}},
	{"_is_rvalue_reference", {TypeTraitKind::IsRvalueReference, false, false, false}},
	{"_is_member_object_pointer", {TypeTraitKind::IsMemberObjectPointer, false, false, false}},
	{"_is_member_function_pointer", {TypeTraitKind::IsMemberFunctionPointer, false, false, false}},
	{"_is_enum", {TypeTraitKind::IsEnum, false, false, false}},
	{"_is_union", {TypeTraitKind::IsUnion, false, false, false}},
	{"_is_class", {TypeTraitKind::IsClass, false, false, false}},
	{"_is_function", {TypeTraitKind::IsFunction, false, false, false}},
	{"_is_reference", {TypeTraitKind::IsReference, false, false, false}},
	{"_is_arithmetic", {TypeTraitKind::IsArithmetic, false, false, false}},
	{"_is_fundamental", {TypeTraitKind::IsFundamental, false, false, false}},
	{"_is_object", {TypeTraitKind::IsObject, false, false, false}},
	{"_is_scalar", {TypeTraitKind::IsScalar, false, false, false}},
	{"_is_compound", {TypeTraitKind::IsCompound, false, false, false}},
	{"_is_base_of", {TypeTraitKind::IsBaseOf, true, false, false}},
	{"_is_same", {TypeTraitKind::IsSame, true, false, false}},
	{"_is_convertible", {TypeTraitKind::IsConvertible, true, false, false}},
	{"_is_nothrow_convertible", {TypeTraitKind::IsNothrowConvertible, true, false, false}},
	{"_is_polymorphic", {TypeTraitKind::IsPolymorphic, false, false, false}},
	{"_is_final", {TypeTraitKind::IsFinal, false, false, false}},
	{"_is_abstract", {TypeTraitKind::IsAbstract, false, false, false}},
	{"_is_empty", {TypeTraitKind::IsEmpty, false, false, false}},
	{"_is_aggregate", {TypeTraitKind::IsAggregate, false, false, false}},
	{"_is_standard_layout", {TypeTraitKind::IsStandardLayout, false, false, false}},
	{"_has_unique_object_representations", {TypeTraitKind::HasUniqueObjectRepresentations, false, false, false}},
	{"_is_trivially_copyable", {TypeTraitKind::IsTriviallyCopyable, false, false, false}},
	{"_is_trivial", {TypeTraitKind::IsTrivial, false, false, false}},
	{"_is_pod", {TypeTraitKind::IsPod, false, false, false}},
	{"_is_literal_type", {TypeTraitKind::IsLiteralType, false, false, false}},
	{"_is_const", {TypeTraitKind::IsConst, false, false, false}},
	{"_is_volatile", {TypeTraitKind::IsVolatile, false, false, false}},
	{"_is_signed", {TypeTraitKind::IsSigned, false, false, false}},
	{"_is_unsigned", {TypeTraitKind::IsUnsigned, false, false, false}},
	{"_is_bounded_array", {TypeTraitKind::IsBoundedArray, false, false, false}},
	{"_is_unbounded_array", {TypeTraitKind::IsUnboundedArray, false, false, false}},
	{"_is_constructible", {TypeTraitKind::IsConstructible, false, true, false}},
	{"_is_trivially_constructible", {TypeTraitKind::IsTriviallyConstructible, false, true, false}},
	{"_is_nothrow_constructible", {TypeTraitKind::IsNothrowConstructible, false, true, false}},
	{"_is_assignable", {TypeTraitKind::IsAssignable, true, false, false}},
	{"_is_trivially_assignable", {TypeTraitKind::IsTriviallyAssignable, true, false, false}},
	{"_is_nothrow_assignable", {TypeTraitKind::IsNothrowAssignable, true, false, false}},
	{"_is_destructible", {TypeTraitKind::IsDestructible, false, false, false}},
	{"_is_trivially_destructible", {TypeTraitKind::IsTriviallyDestructible, false, false, false}},
	{"_is_nothrow_destructible", {TypeTraitKind::IsNothrowDestructible, false, false, false}},
	{"_has_trivial_destructor", {TypeTraitKind::HasTrivialDestructor, false, false, false}},
	{"_has_virtual_destructor", {TypeTraitKind::HasVirtualDestructor, false, false, false}},
	{"_is_layout_compatible", {TypeTraitKind::IsLayoutCompatible, true, false, false}},
	{"_is_pointer_interconvertible_base_of", {TypeTraitKind::IsPointerInterconvertibleBaseOf, true, false, false}},
	{"_underlying_type", {TypeTraitKind::UnderlyingType, false, false, false}},
	{"_is_constant_evaluated", {TypeTraitKind::IsConstantEvaluated, false, false, true}},
	{"_is_complete_or_unbounded", {TypeTraitKind::IsCompleteOrUnbounded, false, false, false}},
};

// Normalize a type trait name to its single-underscore lookup key.
// "__is_void" -> "_is_void", "__builtin_is_void" -> "_is_void"
// Returns a string_view into the original name (zero allocation).
static std::string_view normalize_trait_name(std::string_view name) {
	if (name.starts_with("__builtin_"))
		return name.substr(9); // "__builtin_is_foo" -> "_is_foo"
	if (name.starts_with("_"))
		return name.substr(1); // "__is_foo" -> "_is_foo"
	return name;
}

// Helper: check if a name (possibly with __builtin_ prefix) is a known compiler type trait intrinsic.
// Used to distinguish type traits like __is_void(T) from regular functions like __is_single_threaded().
static bool is_known_type_trait_name(std::string_view name) {
	return trait_map.contains(normalize_trait_name(name));
}

ParseResult Parser::parse_expression(int precedence, ExpressionContext context)
{
	static thread_local int recursion_depth = 0;
	constexpr int MAX_RECURSION_DEPTH = 256;  // Allow deeper standard library expressions
	
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
							FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args_result.args), member_token));
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
	while (!peek().is_eof() && peek_info().value() == "__attribute__") {
		advance(); // consume "__attribute__"
		
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
			if (token.value() == "const") out_quals.is_const = true;
			else out_quals.is_volatile = true;
			advance();
			continue;
		}
		
		// Handle ref-qualifiers (& and &&)
		if (peek() == "&"_tok) {
			out_quals.is_lvalue_ref = true;
			advance();
			continue;
		}
		if (peek() == "&&"_tok) {
			out_quals.is_rvalue_ref = true;
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
		type_spec.set_reference(true);
	} else if (peek() == "&"_tok) {
		advance();
		type_spec.set_reference(false);
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

	DeclarationNode& decl_node = const_cast<DeclarationNode&>(type_and_name_result.node()->as<DeclarationNode>());

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
	                               member_quals.is_const, member_quals.is_volatile);
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
	registered.is_const = member_quals.is_const;
	registered.is_volatile = member_quals.is_volatile;

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
	bool is_const = false;
	bool is_static_constexpr = false;
	while (peek().is_keyword()) {
		std::string_view kw = peek_info().value();
		if (kw == "const") {
			is_const = true;
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
	// Calculate size and alignment for the static member
	size_t member_size = get_type_size_bits(type_spec.type()) / 8;
	size_t member_alignment = get_type_alignment(type_spec.type(), member_size);

	// Register the static member
	StringHandle static_member_name_handle = decl.identifier_token().handle();
	
	// Determine the access specifier to use
	AccessSpecifier access = current_access;
	if (use_struct_type_info) {
		// For template specializations that use struct_type_info.getStructInfo()
		// We need to get it from the global map
		auto type_it = gTypesByName.find(struct_name_handle);
		if (type_it != gTypesByName.end() && type_it->second->getStructInfo()) {
			const_cast<StructTypeInfo*>(type_it->second->getStructInfo())->addStaticMember(
				static_member_name_handle,
				type_spec.type(),
				type_spec.type_index(),
				member_size,
				member_alignment,
				AccessSpecifier::Public,  // Full specializations use Public
				init_expr_opt,
				is_const
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
			is_const
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
			advance(); // consume '::'
			
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
						if (class_scope.find('$') != std::string_view::npos) {
							StringHandle class_name_handle = StringTable::getOrInternStringHandle(class_scope);
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
					FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), final_identifier));
				
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

ParseResult Parser::parse_primary_expression(ExpressionContext context)
{
	std::optional<ASTNode> result;
	
	// Check for 'typename' keyword in expression context: typename T::type{} or typename T::type()
	// This handles dependent type constructor calls used as function arguments
	// Pattern: typename Result::__invoke_type{} creates a temporary of the dependent type
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "typename") {
		Token typename_token = current_token_;
		advance(); // consume 'typename'
		
		// Parse the dependent type name: T::type or Result::__invoke_type
		// This should be an identifier followed by :: and more identifiers
		if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
			return ParseResult::error("Expected type name after 'typename' keyword", typename_token);
		}
		
		// Build the full qualified type name using StringBuilder
		StringBuilder type_name_sb;
		type_name_sb.append(current_token_.value());
		Token first_type_token = current_token_;
		advance(); // consume first identifier
		
		// Handle template arguments after identifier: typename __promote<_Tp>::__type(0)
		if (current_token_.value() == "<") {
			type_name_sb.append("<");
			advance(); // consume '<'
			
			// Parse template arguments, handling nested template arguments
			int angle_bracket_depth = 1;
			while (!current_token_.kind().is_eof() && angle_bracket_depth > 0) {
				if (current_token_.value() == "<") {
					angle_bracket_depth++;
				} else if (current_token_.value() == ">") {
					angle_bracket_depth--;
					if (angle_bracket_depth == 0) {
						type_name_sb.append(">");
						advance(); // consume final '>'
						break;
					}
				}
				type_name_sb.append(current_token_.value());
				advance();
			}
		}
		
		// Parse :: and subsequent identifiers (with optional template args)
		while (current_token_.value() == "::") {
			type_name_sb.append("::");
			advance(); // consume '::'
			
			if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
				type_name_sb.reset(); // Must reset before early return
				return ParseResult::error("Expected identifier after '::' in typename", typename_token);
			}
			type_name_sb.append(current_token_.value());
			advance(); // consume identifier
			
			// Handle template arguments after the identifier
			if (current_token_.value() == "<") {
				type_name_sb.append("<");
				advance(); // consume '<'
				
				// Parse template arguments, handling nested template arguments
				int angle_bracket_depth = 1;
				while (!current_token_.kind().is_eof() && angle_bracket_depth > 0) {
					if (current_token_.value() == "<") {
						angle_bracket_depth++;
					} else if (current_token_.value() == ">") {
						angle_bracket_depth--;
						if (angle_bracket_depth == 0) {
							type_name_sb.append(">");
							advance(); // consume final '>'
							break;
						}
					}
					type_name_sb.append(current_token_.value());
					advance();
				}
			}
		}
		
		// Now we should have either '{}' (brace init) or '()' (paren init)
		ChunkedVector<ASTNode> args;
		Token init_token = typename_token;
		
		if (current_token_.value() == "{") {
			init_token = current_token_;
			advance(); // consume '{'
			
			// Parse brace initializer arguments
			while (current_token_.value() != "}") {
				auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (arg_result.is_error()) {
					type_name_sb.reset(); // Must reset before early return
					return arg_result;
				}
				if (auto arg = arg_result.node()) {
					args.push_back(*arg);
				}
				
				if (current_token_.value() == ",") {
					advance(); // consume ','
				} else if (current_token_.kind().is_eof() || current_token_.value() != "}") {
					type_name_sb.reset(); // Must reset before early return
					return ParseResult::error("Expected ',' or '}' in brace initializer", typename_token);
				}
			}
			
			if (!consume("}"_tok)) {
				type_name_sb.reset(); // Must reset before early return
				return ParseResult::error("Expected '}' after brace initializer", typename_token);
			}
		} else if (current_token_.value() == "(") {
			init_token = current_token_;
			advance(); // consume '('
			
			// Parse parenthesized arguments
			while (current_token_.value() != ")") {
				auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (arg_result.is_error()) {
					type_name_sb.reset(); // Must reset before early return
					return arg_result;
				}
				if (auto arg = arg_result.node()) {
					args.push_back(*arg);
				}
				
				if (current_token_.value() == ",") {
					advance(); // consume ','
				} else if (current_token_.kind().is_eof() || current_token_.value() != ")") {
					type_name_sb.reset(); // Must reset before early return
					return ParseResult::error("Expected ',' or ')' in constructor call", typename_token);
				}
			}
			
			if (!consume(")"_tok)) {
				type_name_sb.reset(); // Must reset before early return
				return ParseResult::error("Expected ')' after constructor arguments", typename_token);
			}
		} else {
			type_name_sb.reset(); // Must reset before early return
			return ParseResult::error("Expected '{' or '(' after typename type expression", typename_token);
		}
		
		// Create a TypeSpecifierNode for the dependent type
		// Store the full type name so it can be resolved during template instantiation
		std::string_view interned_type_name = StringTable::getOrInternStringHandle(type_name_sb.commit()).view();
		Token type_token(Token::Type::Identifier, interned_type_name, 
		                 first_type_token.line(), first_type_token.column(), first_type_token.file_index());
		
		// Create a dependent/placeholder type (Type::UserDefined with special marker)
		auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, type_token);
		
		// Create ConstructorCallNode with the dependent type
		result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), init_token));
		return ParseResult::success(*result);
	}
	
	// Check for functional-style cast with keyword type names: bool(x), int(x), etc.
	// This must come early because these are keywords, not identifiers
	if (current_token_.type() == Token::Type::Keyword) {
		std::string_view kw = current_token_.value();
		bool is_builtin_type = (kw == "bool" || kw == "char" || kw == "int" || 
		                        kw == "short" || kw == "long" || kw == "float" || 
		                        kw == "double" || kw == "void" || kw == "wchar_t" ||
		                        kw == "char8_t" || kw == "char16_t" || kw == "char32_t" ||
		                        kw == "signed" || kw == "unsigned");
		
		if (is_builtin_type) {
			Token type_token = current_token_;
			std::string_view type_kw = current_token_.value();
			advance(); // consume the type keyword
			
			// Check if followed by '(' for functional cast
			if (current_token_.value() == "(") {
				ParseResult cast_result = parse_functional_cast(type_kw, type_token);
				if (!cast_result.is_error() && cast_result.node().has_value()) {
					return cast_result;
				}
			} else {
				// Not a functional cast - restore and continue with normal keyword handling
				// Actually, we can't easily restore here. This is a problem.
				// For now, return an error
				return ParseResult::error("Unexpected keyword in expression context", type_token);
			}
		}
	}

	// Check for 'operator' keyword in expression context: operator==(other), operator+=(x), etc.
	// This is used to call operators as member functions by name, e.g., return !operator==(other);
	// This pattern is common in standard library headers like <typeinfo>
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "operator") {
		Token operator_keyword_token = current_token_;
		advance(); // consume 'operator'

		std::string operator_name = "operator";

		// Check for operator() - function call operator
		if (current_token_.type() == Token::Type::Punctuator &&
		    current_token_.value() == "(") {
			advance(); // consume '('
			if (current_token_.kind().is_eof() || current_token_.value() != ")") {
				return ParseResult::error("Expected ')' after 'operator('", operator_keyword_token);
			}
			advance(); // consume ')'
			operator_name = "operator()";
		}
		// Check for operator[] - subscript operator
		else if (current_token_.type() == Token::Type::Punctuator &&
		         current_token_.value() == "[") {
			advance(); // consume '['
			if (current_token_.kind().is_eof() || current_token_.value() != "]") {
				return ParseResult::error("Expected ']' after 'operator['", operator_keyword_token);
			}
			advance(); // consume ']'
			operator_name = "operator[]";
		}
		// Check for other operators
		else if (current_token_.type() == Token::Type::Operator) {
			std::string_view operator_symbol = current_token_.value();
			advance(); // consume operator symbol
			operator_name += std::string(operator_symbol);
		}
		else {
			return ParseResult::error("Expected operator symbol after 'operator' keyword", operator_keyword_token);
		}

		// Now expect '(' and arguments
		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after operator name in expression", operator_keyword_token);
		}

		// Parse arguments
		ChunkedVector<ASTNode> args;
		if (current_token_.value() != ")") {
			while (true) {
				auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (arg_result.is_error()) {
					return arg_result;
				}
				if (auto arg = arg_result.node()) {
					args.push_back(*arg);
				}

				if (current_token_.kind().is_eof()) {
					return ParseResult::error("Expected ',' or ')' in operator call", operator_keyword_token);
				}

				if (current_token_.value() == ")") {
					break;
				}

				if (!consume(","_tok)) {
					return ParseResult::error("Expected ',' between operator call arguments", operator_keyword_token);
				}
			}
		}

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after operator call arguments", operator_keyword_token);
		}

		// Create a token with the full operator name for the identifier
		Token operator_name_token(Token::Type::Identifier, StringBuilder().append(operator_name).commit(), 
		                          operator_keyword_token.line(), operator_keyword_token.column(), operator_keyword_token.file_index());

		// Check if we're inside a member function context
		if (!member_function_context_stack_.empty()) {
			// Inside a member function - this is a member operator call
			// Create this->operator_name(args) pattern
			// First create 'this' identifier
			Token this_token(Token::Type::Keyword, "this"sv, 
			                operator_keyword_token.line(), operator_keyword_token.column(), operator_keyword_token.file_index());
			auto this_node = emplace_node<ExpressionNode>(IdentifierNode(this_token));

			// Look up the operator function in the current struct type
			const auto& member_ctx = member_function_context_stack_.back();
			if (member_ctx.struct_type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[member_ctx.struct_type_index];
				if (type_info.struct_info_) {
					// Search for the operator member function
					for (const auto& member_func : type_info.struct_info_->member_functions) {
						if (StringTable::getStringView(member_func.name) == operator_name) {
							// Found the operator function - check if it's a FunctionDeclarationNode
							if (member_func.function_decl.is<FunctionDeclarationNode>()) {
								auto& func_decl = const_cast<FunctionDeclarationNode&>(member_func.function_decl.as<FunctionDeclarationNode>());
								result = emplace_node<ExpressionNode>(
									MemberFunctionCallNode(this_node, func_decl, std::move(args), operator_name_token));
								return ParseResult::success(*result);
							}
						}
					}
				}
			}

			// If we couldn't find the operator in the current type, create a generic member access + call
			// This handles cases where the operator might be inherited or template-dependent
			// Look up the function in symbol table as fallback
			auto func_lookup = gSymbolTable.lookup(operator_name);
			if (func_lookup.has_value() && func_lookup->is<FunctionDeclarationNode>()) {
				auto& func_decl = func_lookup->as<FunctionDeclarationNode>();
				result = emplace_node<ExpressionNode>(
					MemberFunctionCallNode(this_node, func_decl, std::move(args), operator_name_token));
				return ParseResult::success(*result);
			}

			// Create a deferred function call for template contexts
			// We create a MemberAccessNode followed by postfix call handling
			// The codegen will handle this as this->operator_name(args)
			auto member_access = emplace_node<ExpressionNode>(
				MemberAccessNode(this_node, operator_name_token, true)); // true = arrow access
			
			// Create a placeholder type spec and decl for the deferred call
			auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, operator_name_token);
			auto& operator_decl = emplace_node<DeclarationNode>(type_spec, operator_name_token).as<DeclarationNode>();
			auto& func_decl_node = emplace_node<FunctionDeclarationNode>(operator_decl).as<FunctionDeclarationNode>();
			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(this_node, func_decl_node, std::move(args), operator_name_token));
			return ParseResult::success(*result);
		}
		else {
			// Not in a member function context - create a free-standing operator call
			// This is valid C++ for calling operator as a function, commonly used in requires expressions
			// e.g., operator<=>(a, b) or requires { operator<=>(t, u); }
			
			// Look up the operator as a free function
			auto func_lookup = gSymbolTable.lookup(operator_name);
			if (func_lookup.has_value() && func_lookup->is<FunctionDeclarationNode>()) {
				auto& func_decl = func_lookup->as<FunctionDeclarationNode>();
				result = emplace_node<ExpressionNode>(
					FunctionCallNode(func_decl.decl_node(), std::move(args), operator_name_token));
				return ParseResult::success(*result);
			}
			
			// Operator function not found - create a deferred call that will be resolved at instantiation
			// This is common in template/requires contexts where the operator is dependent
			auto type_spec = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, operator_name_token);
			auto& operator_decl = emplace_node<DeclarationNode>(type_spec, operator_name_token).as<DeclarationNode>();
			result = emplace_node<ExpressionNode>(
				FunctionCallNode(operator_decl, std::move(args), operator_name_token));
			return ParseResult::success(*result);
		}
	}

	// Check for requires expression: requires(params) { requirements; } or requires { requirements; }
	if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "requires") {
		ParseResult requires_result = parse_requires_expression();
		if (requires_result.is_error()) {
			return requires_result;
		}
		result = requires_result.node();
		// Don't return here - continue to handle potential postfix operators
	}
	// Check for lambda expression first (starts with '[')
	else if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == "[") {
		ParseResult lambda_result = parse_lambda_expression();
		if (lambda_result.is_error()) {
			return lambda_result;
		}
		result = lambda_result.node();
		// Don't return here - continue to postfix operator handling
		// This allows immediately invoked lambdas: []() { ... }()
	}
	// Check for offsetof builtin first (before general identifier handling)
	else if (current_token_.type() == Token::Type::Identifier && current_token_.value() == "offsetof") {
		// Handle offsetof builtin: offsetof(struct_type, member)
		Token offsetof_token = current_token_;
		advance(); // consume 'offsetof'

		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after 'offsetof'", current_token_);
		}

		// Parse the struct type
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error() || !type_result.node().has_value()) {
			return ParseResult::error("Expected struct type in offsetof", current_token_);
		}

		if (!consume(","_tok)) {
			return ParseResult::error("Expected ',' after struct type in offsetof", current_token_);
		}

		// Parse the member name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected member name in offsetof", current_token_);
		}
		Token member_name = peek_info();
		advance(); // consume member name

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after offsetof arguments", current_token_);
		}

		result = emplace_node<ExpressionNode>(
			OffsetofExprNode(*type_result.node(), member_name, offsetof_token));
	}
	// Check for type trait intrinsics: __is_void(T), __is_integral(T), __has_unique_object_representations(T), etc.
	// Also support GCC/Clang __builtin_ prefix variants (e.g., __builtin_is_constant_evaluated)
	// But exclude regular builtin functions like __builtin_labs, __builtin_abs, etc.
	// IMPORTANT: Only treat as type trait intrinsic if followed by '(' - if followed by '<', it's a
	// template class name (e.g., __is_swappable<T> from the standard library)
	// ALSO: Skip if this identifier is already registered as a function template in the template registry
	// (e.g., __is_complete_or_unbounded is a library function template, not a compiler intrinsic)
	else if (current_token_.type() == Token::Type::Identifier && 
	         (current_token_.value().starts_with("__is_") || 
	          current_token_.value().starts_with("__has_") ||
	          (current_token_.value().starts_with("__builtin_") && 
	           (current_token_.value().starts_with("__builtin_is_") || 
	            current_token_.value().starts_with("__builtin_has_")))) &&
	         // Only parse as intrinsic if NEXT token is '(' - otherwise it's a template class name
	         peek(1) == "("_tok &&
	         // Only parse as intrinsic if the name is a KNOWN type trait.
	         // This prevents regular functions like __is_single_threaded() from being misidentified.
	         is_known_type_trait_name(current_token_.value())) {
		// Check if this is actually a declared function template (library function, not intrinsic)
		// If so, skip this branch and let it fall through to normal function call parsing
		std::string_view trait_name = current_token_.value();
		
		bool is_declared_template = gTemplateRegistry.lookupTemplate(trait_name).has_value();
		
		// Also check namespace-qualified name if in namespace
		if (!is_declared_template) {
			NamespaceHandle current_namespace_handle = gSymbolTable.get_current_namespace_handle();
			if (!current_namespace_handle.isGlobal()) {
				StringHandle trait_name_handle = StringTable::getOrInternStringHandle(trait_name);
				StringHandle qualified_name_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace_handle, trait_name_handle);
				is_declared_template = gTemplateRegistry.lookupTemplate(StringTable::getStringView(qualified_name_handle)).has_value();
			}
		}
		
		if (!is_declared_template) {
			// Parse type trait intrinsics
			Token trait_token = current_token_;
			advance(); // consume the trait name

			auto it = trait_map.find(normalize_trait_name(trait_name));
			if (it == trait_map.end()) {
				// Unknown type trait intrinsic - this shouldn't happen since we only reach here
				// if followed by '(' which means it was intended as a type trait call
				return ParseResult::error("Unknown type trait intrinsic", trait_token);
			}

			TypeTraitKind kind = it->second.kind;
			bool is_binary_trait = it->second.is_binary;
			bool is_variadic_trait = it->second.is_variadic;
			bool is_no_arg_trait = it->second.is_no_arg;

			if (!consume("("_tok)) {
				return ParseResult::error("Expected '(' after type trait intrinsic", current_token_);
			}

			if (is_no_arg_trait) {
				// No-argument trait like __is_constant_evaluated()
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' for no-argument type trait", current_token_);
				}

				result = emplace_node<ExpressionNode>(
					TypeTraitExprNode(kind, trait_token));
			} else {
				// Parse the first type argument
				ParseResult type_result = parse_type_specifier();
				if (type_result.is_error() || !type_result.node().has_value()) {
					return ParseResult::error("Expected type in type trait intrinsic", current_token_);
				}

				// Parse pointer/reference modifiers after the base type (ptr-operator in C++20 grammar)
				// e.g., int* or int&& in type trait arguments
				TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
				consume_pointer_ref_modifiers(type_spec);

				// Parse array specifications ([N] or [])
				if (peek() == "["_tok) {
					advance();  // consume '['
					
					// Check for array size expression or empty brackets
					std::optional<size_t> array_size_val;
					if (!peek().is_eof() && peek() != "]"_tok) {
						// Parse array size expression
						ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (size_result.is_error()) {
							return ParseResult::error("Expected array size expression", current_token_);
						}
						
						// Try to evaluate the array size as a constant expression
						if (size_result.node().has_value()) {
							ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*size_result.node(), eval_ctx);
							if (eval_result.success()) {
								array_size_val = static_cast<size_t>(eval_result.as_int());
							}
						}
					}
					
					if (!consume("]"_tok)) {
						return ParseResult::error("Expected ']' after array size", current_token_);
					}
					
					type_spec.set_array(true, array_size_val);
				}

				// Check for pack expansion (...) after the first type argument
				if (peek() == "..."_tok) {
					advance();  // consume '...'
					type_spec.set_pack_expansion(true);
				}

				if (is_variadic_trait) {
					// Variadic trait: parse comma-separated additional types
					std::vector<ASTNode> additional_types;
					while (peek() == ","_tok) {
						consume(","_tok);
						ParseResult arg_type_result = parse_type_specifier();
						if (arg_type_result.is_error() || !arg_type_result.node().has_value()) {
							return ParseResult::error("Expected type argument in variadic type trait", current_token_);
						}
						
						// Parse pointer/reference modifiers for additional type arguments (ptr-operator in C++20 grammar)
						TypeSpecifierNode& arg_type_spec = arg_type_result.node()->as<TypeSpecifierNode>();
						consume_pointer_ref_modifiers(arg_type_spec);
						
						// Parse array specifications ([N] or []) for variadic trait additional args
						std::optional<size_t> array_size_val;
						if (peek() == "["_tok) {
							advance();  // consume '['
							
							if (!peek().is_eof() && peek() != "]"_tok) {
								ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
								if (size_result.is_error()) {
									return ParseResult::error("Expected array size expression", current_token_);
								}
								if (size_result.node().has_value()) {
									ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
									auto eval_result = ConstExpr::Evaluator::evaluate(*size_result.node(), eval_ctx);
									if (eval_result.success()) {
										array_size_val = static_cast<size_t>(eval_result.as_int());
									}
								}
							}
							
							if (!consume("]"_tok)) {
								return ParseResult::error("Expected ']' after array size", current_token_);
							}
							
							arg_type_spec.set_array(true, array_size_val);
						}

						// Check for pack expansion (...) after the type argument
						if (peek() == "..."_tok) {
							advance();  // consume '...'
							arg_type_spec.set_pack_expansion(true);
						}
						
						additional_types.push_back(*arg_type_result.node());
					}

					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after type trait arguments", current_token_);
					}

					result = emplace_node<ExpressionNode>(
						TypeTraitExprNode(kind, *type_result.node(), std::move(additional_types), trait_token));
				} else if (is_binary_trait) {
					// Binary trait: parse comma and second type
					if (!consume(","_tok)) {
						return ParseResult::error("Expected ',' after first type in binary type trait", current_token_);
					}

					ParseResult second_type_result = parse_type_specifier();
					if (second_type_result.is_error() || !second_type_result.node().has_value()) {
						return ParseResult::error("Expected second type in binary type trait", current_token_);
					}

					// Parse pointer/reference modifiers for second type (ptr-operator in C++20 grammar)
					TypeSpecifierNode& second_type_spec = second_type_result.node()->as<TypeSpecifierNode>();
					consume_pointer_ref_modifiers(second_type_spec);

					// Parse array specifications ([N] or []) for binary trait second type
					std::optional<size_t> array_size_val;
					if (peek() == "["_tok) {
						advance();  // consume '['
						
						if (!peek().is_eof() && peek() != "]"_tok) {
							ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (size_result.is_error()) {
								return ParseResult::error("Expected array size expression", current_token_);
							}
							if (size_result.node().has_value()) {
								ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
								auto eval_result = ConstExpr::Evaluator::evaluate(*size_result.node(), eval_ctx);
								if (eval_result.success()) {
									array_size_val = static_cast<size_t>(eval_result.as_int());
								}
							}
						}
						
						if (!consume("]"_tok)) {
							return ParseResult::error("Expected ']' after array size", current_token_);
						}
						
						second_type_spec.set_array(true, array_size_val);
					}

					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after type trait arguments", current_token_);
					}

					result = emplace_node<ExpressionNode>(
						TypeTraitExprNode(kind, *type_result.node(), *second_type_result.node(), trait_token));
				} else {
					// Unary trait: just close paren
					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after type trait argument", current_token_);
					}

					result = emplace_node<ExpressionNode>(
						TypeTraitExprNode(kind, *type_result.node(), trait_token));
				}
			}
		} // end if (!is_declared_template)
	}
	// Check for global namespace scope operator :: at the beginning
	else if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == "::") {
		advance(); // consume ::

		// Handle ::operator new(...) and ::operator delete(...) as function call expressions
		// Used by libstdc++ allocators: static_cast<_Tp*>(::operator new(__n * sizeof(_Tp)))
		if (current_token_.type() == Token::Type::Keyword &&
		    current_token_.value() == "operator") {
			Token operator_token = current_token_;
			advance(); // consume 'operator'

			// Expect 'new' or 'delete'
			if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Keyword ||
			    (current_token_.value() != "new" && current_token_.value() != "delete")) {
				return ParseResult::error("Expected 'new' or 'delete' after '::operator'", current_token_);
			}

			StringBuilder op_name_sb;
			op_name_sb.append("operator ");
			op_name_sb.append(current_token_.value());
			advance(); // consume 'new' or 'delete'

			// Check for array variant: operator new[] or operator delete[]
			if (current_token_.value() == "[") {
				advance(); // consume '['
				if (current_token_.value() == "]") {
					advance(); // consume ']'
					op_name_sb.append("[]");
				}
			}

			std::string_view op_name = op_name_sb.commit();
			Token op_identifier(Token::Type::Identifier, op_name,
			                    operator_token.line(), operator_token.column(), operator_token.file_index());

			// Expect '(' for function call
			if (current_token_.kind().is_eof() || current_token_.value() != "(") {
				return ParseResult::error("Expected '(' after '::operator new/delete'", current_token_);
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

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after operator new/delete arguments", current_token_);
			}

			// Create a forward declaration for the operator (returns void* for new, void for delete)
			bool is_new = op_name.find("new") != std::string_view::npos;
			auto type_node = emplace_node<TypeSpecifierNode>(Type::Void, TypeQualifier::None, 0, Token());
			if (is_new) {
				type_node.as<TypeSpecifierNode>().add_pointer_level(); // void* return type for new
			}
			auto forward_decl = emplace_node<DeclarationNode>(type_node, op_identifier);
			DeclarationNode& decl_ref = forward_decl.as<DeclarationNode>();

			auto call_node = emplace_node<ExpressionNode>(
				FunctionCallNode(decl_ref, std::move(args_result.args), op_identifier));
			return ParseResult::success(call_node);
		}

		// Expect an identifier after ::
		if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
			return ParseResult::error("Expected identifier after '::'", current_token_);
		}

		Token first_identifier = current_token_;
		advance(); // consume identifier

		// Helper to get DeclarationNode from either DeclarationNode, FunctionDeclarationNode, VariableDeclarationNode, or TemplateFunctionDeclarationNode
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

		// Check if there are more :: following (e.g., ::ns::func)
		std::vector<StringType<32>> namespaces;
		Token final_identifier = first_identifier;

		while (current_token_.value() == "::"sv) {
			// Current identifier is a namespace part
			namespaces.emplace_back(StringType<32>(final_identifier.value()));
			advance(); // consume ::

			// Get next identifier
			if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
				return ParseResult::error("Expected identifier after '::'", current_token_);
			}
			final_identifier = current_token_;
			advance(); // consume the identifier
		}

		// Create a QualifiedIdentifierNode with namespace handle
		// If namespaces is empty, it means ::identifier (global namespace)
		// If namespaces is not empty, it means ::ns::identifier
		// force_global=true because :: prefix means resolve from global namespace
		NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces, /*force_global=*/true);
		auto qualified_node = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
		const QualifiedIdentifierNode& qual_id = qualified_node.as<QualifiedIdentifierNode>();

		// Try to look up the qualified identifier
		// For global namespace (empty namespaces), lookup_qualified handles it correctly
		// by looking in the global namespace (namespace_symbols_[empty_path])
		std::optional<ASTNode> identifierType;
		// Always use lookup_symbol_qualified - it handles both cases:
		// - Global namespace (handle index 0) -> looks in global namespace only
		// - Non-global namespace -> looks in specified namespace
		identifierType = lookup_symbol_qualified(qual_id.namespace_handle(), qual_id.name());

		// Check if followed by '(' for function call
		if (current_token_.value() == "(") {
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

			// If not found and we're not in extern "C", try template instantiation
			if (!identifierType && current_linkage_ != Linkage::C) {
				// Build qualified template name (e.g., "::move" or "::std::move")
				std::string_view qualified_name = buildQualifiedNameFromStrings(namespaces, qual_id.name());
				
				// Apply lvalue reference for forwarding deduction on arg_types
				std::vector<TypeSpecifierNode> arg_types = apply_lvalue_reference_deduction(args, args_result.arg_types);
				
				// Try to instantiate the qualified template function
				if (!arg_types.empty()) {
					std::optional<ASTNode> template_inst = try_instantiate_template(qualified_name, arg_types);
					if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
						identifierType = *template_inst;
						FLASH_LOG(Parser, Debug, "Successfully instantiated qualified template: ", qualified_name);
					}
				}
			}
			
			// If still not found, create a forward declaration
			if (!identifierType) {
				// Validate namespace exists before creating forward declaration (catches f2::func when f2 undeclared)
				if (!validateQualifiedNamespace(qual_id.namespace_handle(), qual_id.identifier_token(), parsing_template_body_)) {
					return ParseResult::error(
						std::string(StringBuilder().append("Use of undeclared identifier '")
							.append(buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name()))
							.append("'").commit()),
						qual_id.identifier_token());
				}
				auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
				auto forward_decl = emplace_node<DeclarationNode>(type_node, qual_id.identifier_token());
				identifierType = forward_decl;
			}

			// Get the DeclarationNode (works for both DeclarationNode and FunctionDeclarationNode)
			const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
			if (!decl_ptr) {
				return ParseResult::error("Invalid function declaration (global namespace path)", qual_id.identifier_token());
			}

			// Create function call node with the qualified identifier
			auto function_call_node = emplace_node<ExpressionNode>(
				FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), qual_id.identifier_token()));
			// If the function has a pre-computed mangled name, set it on the FunctionCallNode
			if (identifierType->is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
				FLASH_LOG(Parser, Debug, "Qualified function has mangled name: {}, name: {}", func_decl.has_mangled_name(), func_decl.mangled_name());
				if (func_decl.has_mangled_name()) {
					std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
					FLASH_LOG(Parser, Debug, "Set mangled name on qualified FunctionCallNode: {}", func_decl.mangled_name());
				}
			}
			result = function_call_node;
		} else {
			// Just a qualified identifier reference (e.g., ::globalValue)
			result = emplace_node<ExpressionNode>(qual_id);
		}

		if (result.has_value())
			return ParseResult::success(*result);
	}
	else if (current_token_.type() == Token::Type::Identifier) {
		Token idenfifier_token = current_token_;

		// Check for __func__, __PRETTY_FUNCTION__ (compiler builtins)
		if (idenfifier_token.value() == "__func__"sv ||
		    idenfifier_token.value() == "__PRETTY_FUNCTION__"sv) {

			if (!current_function_) {
				return ParseResult::error(
					std::string(idenfifier_token.value()) + " can only be used inside a function",
					idenfifier_token);
			}

			// Create a string literal with the function name or signature
			// For __PRETTY_FUNCTION__, use the full signature; for others, use simple name
			std::string_view persistent_name;
			if (idenfifier_token.value() == "__PRETTY_FUNCTION__"sv) {
				persistent_name = context_.storeFunctionNameLiteral(buildPrettyFunctionSignature(*current_function_));
			} else {
				// For __func__, just use the simple function name
				persistent_name = current_function_->decl_node().identifier_token().value();
			}

			// Store the function name string in CompileContext so it persists
			// Note: Unlike string literals from source code (which include quotes in the token),
			// __func__/__PRETTY_FUNCTION__ are predefined identifiers that expand
			// to the string content directly, without quotes. This matches MSVC/GCC/Clang behavior.
			Token string_token(Token::Type::StringLiteral,
			                   persistent_name,
			                   idenfifier_token.line(),
			                   idenfifier_token.column(),
			                   idenfifier_token.file_index());

			result = emplace_node<ExpressionNode>(StringLiteralNode(string_token));
			advance();

			if (result.has_value())
				return ParseResult::success(*result);
		}

		// Check if this is a qualified identifier (namespace::identifier)
		// Helper to get DeclarationNode from either DeclarationNode, FunctionDeclarationNode, or VariableDeclarationNode
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

		// We need to consume the identifier first to check what comes after it
		advance();
		
		// Check for functional-style cast: Type(expression)
		// This is needed for patterns like bool(x), int(y), etc.
		// Check if this identifier is a type name and followed by '('
		// NOTE: Only treat BUILT-IN types as functional casts here.
		// User-defined types with Type(args) syntax are constructor calls, not casts,
		// and should be handled by the normal identifier/function call path below.
		if (current_token_.value() == "(" &&
		    !current_token_.value().starts_with("::")) {
			std::string_view id_name = idenfifier_token.value();
			
			// Only check for built-in type names (not user-defined types)
			// User-defined Type(args) is a constructor call, not a functional cast
			auto type_info = get_builtin_type_info(id_name);
			if (type_info.has_value()) {
				// This is a built-in type followed by '(' - parse as functional cast
				ParseResult cast_result = parse_functional_cast(id_name, idenfifier_token);
				if (!cast_result.is_error() && cast_result.node().has_value()) {
					return cast_result;
				}
			}
		}
		
		if (current_token_.value() == "::"sv) {
			// Build the qualified identifier manually
			std::vector<StringType<32>> namespaces;
			Token final_identifier = idenfifier_token;

			// Collect namespace parts
			while (current_token_.value() == "::"sv) {
				// Current identifier is a namespace part
				namespaces.emplace_back(StringType<32>(final_identifier.value()));
				advance(); // consume ::

				// Get next identifier
				if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
					return ParseResult::error("Expected identifier after '::'", current_token_);
				}
				final_identifier = current_token_;
				advance(); // consume the identifier to check for the next ::
			}

		// current_token_ is now the token after the final identifier

		// Create a QualifiedIdentifierNode
		NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
		auto qualified_node = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
		const QualifiedIdentifierNode& qual_id = qualified_node.as<QualifiedIdentifierNode>();

		// Check for std::forward intrinsic
		// std::forward<T>(arg) is a compiler intrinsic for perfect forwarding
		// Check if namespace is "std" (single-level namespace with name "std")
		std::string_view ns_qualified_name = gNamespaceRegistry.getQualifiedName(qual_id.namespace_handle());
		if (ns_qualified_name == "std" && qual_id.name() == "forward") {
			
			// Handle std::forward<T>(arg)
			// For now, we'll treat it as an identity function that preserves references
			// Skip template arguments if present
			if (current_token_.value() == "<") {
				// Skip template arguments: <T> or <iter_reference_t<_It>>
				int angle_bracket_depth = 1;
				advance(); // consume <
				
				while (angle_bracket_depth > 0 && !current_token_.kind().is_eof()) {
					if (current_token_.value() == "<") angle_bracket_depth++;
					else if (current_token_.value() == ">") angle_bracket_depth--;
					else if (current_token_.value() == ">>") angle_bracket_depth -= 2;
					advance();
				}
			}
			
			// Now expect (arg)
			if (current_token_.kind().is_eof() || current_token_.value() != "(") {
				return ParseResult::error("Expected '(' after std::forward", final_identifier);
			}
			advance(); // consume '('
			
			// Parse the single argument
			auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (arg_result.is_error()) {
				return arg_result;
			}
			
			if (current_token_.kind().is_eof() || current_token_.value() != ")") {
				return ParseResult::error("Expected ')' after std::forward argument", current_token_);
			}
			advance(); // consume ')'
			
			// std::forward<T>(arg) is essentially an identity function
			// Just return the argument expression itself
			// The type system already preserves the reference type
			result = arg_result.node();
			return ParseResult::success(*result);
		}

		// Check if qualified identifier is followed by template arguments: ns::Template<Args>
		// This must come BEFORE we try to use current_token_ as an operator
		// Phase 1: C++20 Template Argument Disambiguation - try to parse template arguments
		// after qualified identifiers, BUT check if the member is actually a template first
		// to avoid misinterpreting comparisons like _R1::num < _R2::num
		std::optional<std::vector<TemplateTypeArg>> template_args;
		std::vector<ASTNode> template_arg_nodes;  // Store the actual expression nodes
		if (current_token_.value() == "<") {
			// Build the qualified name from namespace handle
			std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
			std::string_view member_name = qual_id.name();
			
			// Check if the member is a known template before parsing < as template arguments
			// This prevents misinterpreting patterns like _R1::num < _R2::num> where < is comparison
			auto member_template_opt = gTemplateRegistry.lookupTemplate(member_name);
			auto member_var_template_opt = gTemplateRegistry.lookupVariableTemplate(member_name);
			auto member_alias_template_opt = gTemplateRegistry.lookup_alias_template(member_name);
			auto full_template_opt = gTemplateRegistry.lookupTemplate(qualified_name);
			auto full_var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
			auto full_alias_template_opt = gTemplateRegistry.lookup_alias_template(qualified_name);
			
			bool is_known_template = member_template_opt.has_value() || member_var_template_opt.has_value() ||
			                         member_alias_template_opt.has_value() ||
			                         full_template_opt.has_value() || full_var_template_opt.has_value() ||
			                         full_alias_template_opt.has_value();
			
			// Also check if the base is a template parameter - if so, the member is likely NOT a template
			bool base_is_template_param = false;
			if (!qual_id.namespace_handle().isGlobal()) {
				std::string_view base_name = gNamespaceRegistry.getRootNamespaceName(qual_id.namespace_handle());
				for (const auto& param_name : current_template_param_names_) {
					if (StringTable::getStringView(param_name) == base_name) {
						base_is_template_param = true;
						break;
					}
				}
			}
			
			// Decide whether to parse template arguments
			bool should_parse_template_args = true;
			if (!is_known_template && (context == ExpressionContext::TemplateArgument || base_is_template_param)) {
				// Member is NOT a known template and we're in a context where < is likely comparison
				FLASH_LOG_FORMAT(Parser, Debug, 
				    "Qualified identifier '{}' member is not a known template - treating '<' as comparison operator (context={}, base_is_param={})",
				    qualified_name, static_cast<int>(context), base_is_template_param);
				should_parse_template_args = false;
			}
			
			if (should_parse_template_args) {
				FLASH_LOG_FORMAT(Parser, Debug, "Qualified identifier '{}' followed by '<', attempting template argument parsing", qualified_name);
				template_args = parse_explicit_template_arguments(&template_arg_nodes);
			}
			
			if (template_args.has_value()) {
				FLASH_LOG_FORMAT(Parser, Debug, "Successfully parsed {} template arguments for '{}'", template_args->size(), qualified_name);
				
				// First, check if this is a variable template (most common case for traits like is_reference_v<T>)
				auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
				if (!var_template_opt.has_value()) {
					// Try with simple name
					var_template_opt = gTemplateRegistry.lookupVariableTemplate(qual_id.name());
				}
				
				// If still not found, check if the base is a struct/class name (not a namespace)
				// For patterns like StructName::member_template<Args>, we need to build the qualified name manually
				std::string_view struct_qualified_name;
				if (!var_template_opt.has_value() && !namespaces.empty()) {
					// Build struct-qualified name: "StructName::member"
					struct_qualified_name = buildQualifiedNameFromStrings(namespaces, qual_id.name());
					
					FLASH_LOG_FORMAT(Templates, Debug, "Trying struct-qualified variable template lookup: '{}'", struct_qualified_name);
					var_template_opt = gTemplateRegistry.lookupVariableTemplate(struct_qualified_name);
					if (var_template_opt.has_value()) {
						FLASH_LOG(Templates, Debug, "Found variable template with struct-qualified name!");
					} else {
						FLASH_LOG(Templates, Debug, "Variable template NOT found with struct-qualified name");
					}
				}
				
				if (var_template_opt.has_value()) {
					// Determine which name to use for instantiation
					std::string_view template_name_for_instantiation = qualified_name;
					if (!struct_qualified_name.empty()) {
						template_name_for_instantiation = struct_qualified_name;
					}
					
					FLASH_LOG(Templates, Debug, "Found variable template, instantiating: ", template_name_for_instantiation);
					// Try instantiation with determined name first, fall back to simple name
					auto instantiated_var = try_instantiate_variable_template(template_name_for_instantiation, *template_args);
					if (!instantiated_var.has_value()) {
						instantiated_var = try_instantiate_variable_template(qual_id.name(), *template_args);
					}
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
						
						FLASH_LOG(Templates, Debug, "Successfully instantiated variable template: ", qualified_name);
						
						// Return identifier reference to the instantiated variable
						Token inst_token(Token::Type::Identifier, inst_name, 
						                final_identifier.line(), final_identifier.column(), final_identifier.file_index());
						result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
						return ParseResult::success(*result);
					}
				}
				
				// Check if this is a concept application (e.g., std::same_as<T, U>)
				// Concepts evaluate to boolean values at compile time
				auto concept_opt = gConceptRegistry.lookupConcept(qualified_name);
				if (!concept_opt.has_value()) {
					// Try with simple name
					concept_opt = gConceptRegistry.lookupConcept(qual_id.name());
				}
				
				if (concept_opt.has_value()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Found concept '{}' with template arguments (qualified lookup)", qualified_name);
					
					// Evaluate the concept constraint with the provided template arguments
					auto constraint_result = evaluateConstraint(
						concept_opt->as<ConceptDeclarationNode>().constraint_expr(),
						*template_args,
						{}  // No template param names needed for concrete types
					);
					
					// Create a BoolLiteralNode with the result
					bool concept_satisfied = constraint_result.satisfied;
					Token bool_token(Token::Type::Keyword, concept_satisfied ? "true"sv : "false"sv,
					                final_identifier.line(), final_identifier.column(), final_identifier.file_index());
					result = emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, concept_satisfied));
					return ParseResult::success(*result);
				}
				
				// Check if this is an alias template (like detail::cref<int> -> int)
				// Alias templates should resolve to their underlying type
				auto alias_opt = gTemplateRegistry.lookup_alias_template(qualified_name);
				if (!alias_opt.has_value()) {
					// Try with simple name
					alias_opt = gTemplateRegistry.lookup_alias_template(qual_id.name());
				}
				
				if (alias_opt.has_value()) {
					FLASH_LOG(Templates, Debug, "Found alias template, resolving: ", qualified_name);
					const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
					
					// Get the target type of the alias
					// For a simple alias like `template<typename T> using cref = T;`, the target type is T
					// We need to substitute the template parameter with the actual argument
					const TypeSpecifierNode& target_type = alias_node.target_type_node();
					const auto& param_names = alias_node.template_param_names();
					
					// Check if the target type is one of the template parameters
					Token target_token = target_type.token();
					if (target_token.type() == Token::Type::Identifier) {
						std::string_view target_name = target_token.value();
						for (size_t i = 0; i < param_names.size() && i < template_args->size(); ++i) {
							if (target_name == param_names[i].view()) {
								// The target type is the i-th template parameter
								// Substitute it with the actual argument
								const TemplateTypeArg& arg = (*template_args)[i];
								if (!arg.is_value && arg.type_index < gTypeInfo.size()) {
									// It's a type argument - get the type name and create an identifier
									StringHandle type_name_handle = gTypeInfo[arg.type_index].name();
									std::string_view type_name = StringTable::getStringView(type_name_handle);
									FLASH_LOG_FORMAT(Templates, Debug, "Alias template parameter '{}' resolved to type '{}'", target_name, type_name);
									
									// Return an IdentifierNode for the resolved type
									Token resolved_token(Token::Type::Identifier, type_name, 
									                     final_identifier.line(), final_identifier.column(), final_identifier.file_index());
									result = emplace_node<ExpressionNode>(IdentifierNode(resolved_token));
									return ParseResult::success(*result);
								}
								break;
							}
						}
					}
					
					// If the target type is not a direct parameter reference, fall through to other handling
					FLASH_LOG(Templates, Debug, "Alias template target is not a direct parameter, continuing with class template instantiation");
				}
				
				// Try to instantiate the template with these arguments
				// Note: try_instantiate_class_template returns nullopt on success (type registered in gTypesByName)
				// Try class template instantiation first (for struct/class templates)
				auto instantiation_result = try_instantiate_class_template(qual_id.name(), *template_args);
				if (instantiation_result.has_value()) {
					// Simple name failed, try with qualified name
					instantiation_result = try_instantiate_class_template(qualified_name, *template_args);
					if (instantiation_result.has_value()) {
						// Class instantiation didn't work, try function template
						instantiation_result = try_instantiate_template_explicit(qual_id.name(), *template_args);
						if (instantiation_result.has_value()) {
							instantiation_result = try_instantiate_template_explicit(qualified_name, *template_args);
							if (instantiation_result.has_value()) {
								// Template instantiation failed - this might not be a template after all
								// But we successfully parsed template arguments, so continue with the parsed args
								FLASH_LOG_FORMAT(Parser, Warning, "Parsed template arguments but instantiation failed for '{}'", qualified_name);
							}
						}
					}
				}
				// If we reach here, instantiation succeeded (returned nullopt)
				
				// Check if followed by :: for member access (Template<T>::member)
				if (current_token_.value() == "::") {
					// Fill in default template arguments to get the actual instantiated name
					std::vector<TemplateTypeArg> filled_template_args = *template_args;
					auto template_lookup_result = gTemplateRegistry.lookupTemplate(qual_id.name());
					if (template_lookup_result.has_value() && template_lookup_result->is<TemplateClassDeclarationNode>()) {
						const auto& template_class = template_lookup_result->as<TemplateClassDeclarationNode>();
						const auto& template_params = template_class.template_parameters();
						
						// Helper lambda to build instantiated template name suffix
						// Fill in defaults for missing parameters
						for (size_t param_idx = filled_template_args.size(); param_idx < template_params.size(); ++param_idx) {
							const TemplateParameterNode& param = template_params[param_idx].as<TemplateParameterNode>();
							if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
								const ASTNode& default_node = param.default_value();
								if (default_node.is<TypeSpecifierNode>()) {
									const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
									filled_template_args.push_back(TemplateTypeArg(default_type));
								}
							} else if (param.has_default() && param.kind() == TemplateParameterKind::NonType) {
								const ASTNode& default_node = param.default_value();
								if (default_node.is<ExpressionNode>()) {
									const ExpressionNode& expr_default = default_node.as<ExpressionNode>();
									
									if (std::holds_alternative<QualifiedIdentifierNode>(expr_default)) {
										const QualifiedIdentifierNode& qual_id_default = std::get<QualifiedIdentifierNode>(expr_default);
										
									if (!qual_id_default.namespace_handle().isGlobal()) {
										std::string_view type_name_sv = gNamespaceRegistry.getName(qual_id_default.namespace_handle());
										std::string_view default_member_name = qual_id_default.name();
										
										// Check for dependent placeholder using TypeInfo-based detection
										auto [is_dependent_placeholder, template_base_name] = isDependentTemplatePlaceholder(type_name_sv);
										if (is_dependent_placeholder && !filled_template_args.empty()) {
											// Build the instantiated template name using hash-based naming
											std::string_view inst_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
												
												try_instantiate_class_template(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
												
												auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
												if (type_it != gTypesByName.end()) {
													const TypeInfo* type_info = type_it->second;
													if (type_info->getStructInfo()) {
														const StructTypeInfo* struct_info = type_info->getStructInfo();
														for (const auto& static_member : struct_info->static_members) {
															if (StringTable::getStringView(static_member.getName()) == default_member_name) {
																if (static_member.initializer.has_value()) {
																	const ASTNode& init_node = *static_member.initializer;
																	if (init_node.is<ExpressionNode>()) {
																		const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
																		if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
																			bool val = std::get<BoolLiteralNode>(init_expr).value();
																			filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL, Type::Bool));
																		} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
																			const NumericLiteralNode& lit = std::get<NumericLiteralNode>(init_expr);
																			const auto& val = lit.value();
																			if (std::holds_alternative<unsigned long long>(val)) {
																				filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
																			}
																		}
																	}
																}
																break;
															}
														}
													}
												}
											}
										}
									} else if (std::holds_alternative<NumericLiteralNode>(expr_default)) {
										const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr_default);
										const auto& val = lit.value();
										if (std::holds_alternative<unsigned long long>(val)) {
											filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
										} else if (std::holds_alternative<double>(val)) {
											filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<double>(val))));
										}
									} else if (std::holds_alternative<BoolLiteralNode>(expr_default)) {
										const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr_default);
										filled_template_args.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL, Type::Bool));
									}
								}
							}
						}
					}
					
					// Get the instantiated class name to use in qualified identifier (with defaults filled in)
					std::string_view instantiated_name = get_instantiated_class_name(qual_id.name(), filled_template_args);
					
					// Build the full namespace path including the instantiated template name
					// For "my_ns::Wrapper<int>::value", we want namespace path "my_ns::Wrapper_int" and name="value"
					// Build namespace path from the original namespace handle plus the instantiated template name
					NamespaceHandle base_ns = qual_id.namespace_handle();
					StringHandle instantiated_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
					NamespaceHandle full_ns_handle = gNamespaceRegistry.getOrCreateNamespace(base_ns, instantiated_name_handle);
					
					// Parse the :: and the member name
					advance(); // consume ::
					if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
						return ParseResult::error("Expected identifier after '::'", current_token_);
					}
					
					Token member_token = current_token_;
					advance(); // consume member identifier
					
					// Handle additional :: if present (nested member access)
					while (current_token_.value() == "::") {
						// Add current member to namespace path
						StringHandle member_handle = member_token.handle();
						full_ns_handle = gNamespaceRegistry.getOrCreateNamespace(full_ns_handle, member_handle);
						advance(); // consume ::
						if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
							return ParseResult::error("Expected identifier after '::'", current_token_);
						}
						member_token = current_token_;
						advance(); // consume identifier
					}
					
					// Create QualifiedIdentifierNode with the complete path
					auto full_qualified_node = emplace_node<QualifiedIdentifierNode>(full_ns_handle, member_token);
					
					// Look up the member in the instantiated struct's symbol table
					auto member_lookup = gSymbolTable.lookup_qualified(full_ns_handle, member_token.value());
					
					// If followed by '(', handle as function call (e.g., Template<T>::method())
					if (current_token_.value() == "(") {
						advance(); // consume '('
						
						auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
							.handle_pack_expansion = true,
							.collect_types = true,
							.expand_simple_packs = true
						});
						if (!args_result.success) {
							return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
						}
						ChunkedVector<ASTNode> args = std::move(args_result.args);
						
						if (!consume(")"_tok)) {
							return ParseResult::error("Expected ')' after function call arguments", current_token_);
						}
						
						// Get the declaration node for the function
						const DeclarationNode* decl_ptr = nullptr;
						if (member_lookup.has_value()) {
							decl_ptr = getDeclarationNode(*member_lookup);
						}
						if (!decl_ptr) {
							// Member may not be in namespace symbol table - resolve from instantiated struct members.
							auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_name));
							if (type_it != gTypesByName.end() && type_it->second) {
								const StructTypeInfo* struct_info = type_it->second->getStructInfo();
								if (struct_info) {
									StringHandle member_name_handle = member_token.handle();
									const FunctionDeclarationNode* first_name_match = nullptr;
									size_t call_arg_count = args.size();
									for (const auto& member_func : struct_info->member_functions) {
										if (member_func.getName() == member_name_handle && member_func.function_decl.is<FunctionDeclarationNode>()) {
											const FunctionDeclarationNode& candidate = member_func.function_decl.as<FunctionDeclarationNode>();
											if (!first_name_match) {
												first_name_match = &candidate;
											}
											if (candidate.parameter_nodes().size() == call_arg_count) {
												member_lookup = member_func.function_decl;
												decl_ptr = &candidate.decl_node();
												break;
											}
										}
									}
									if (!decl_ptr && first_name_match) {
										decl_ptr = &first_name_match->decl_node();
									}
								}
							}
						}
						if (member_lookup.has_value() && member_lookup->is<FunctionDeclarationNode>()) {
							const FunctionDeclarationNode& func_decl = member_lookup->as<FunctionDeclarationNode>();
							if (!func_decl.get_definition().has_value() && instantiated_name.find('$') != std::string_view::npos) {
								StringHandle class_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
								StringHandle member_name_handle = member_token.handle();
								if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(class_name_handle, member_name_handle)) {
									auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(class_name_handle, member_name_handle);
									if (lazy_info_opt.has_value()) {
										auto instantiated_func = instantiateLazyMemberFunction(*lazy_info_opt);
										if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
											member_lookup = instantiated_func;
											decl_ptr = &instantiated_func->as<FunctionDeclarationNode>().decl_node();
											LazyMemberInstantiationRegistry::getInstance().markInstantiated(class_name_handle, member_name_handle);
										}
									}
								}
							}
						}
						if (!decl_ptr) {
							// Create a forward declaration
							auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
							auto forward_decl = emplace_node<DeclarationNode>(type_node, member_token);
							member_lookup = forward_decl;
							decl_ptr = &forward_decl.as<DeclarationNode>();
						}
						
						result = emplace_node<ExpressionNode>(
							FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), member_token));
						
						// Set mangled name if available
						if (member_lookup.has_value() && member_lookup->is<FunctionDeclarationNode>()) {
							const FunctionDeclarationNode& func_decl = member_lookup->as<FunctionDeclarationNode>();
							if (func_decl.has_mangled_name()) {
								std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
							}
						}
						
						return ParseResult::success(*result);
					}
					
					result = emplace_node<ExpressionNode>(full_qualified_node.as<QualifiedIdentifierNode>());
					return ParseResult::success(*result);
				}
				
				// Template instantiation succeeded
				// Don't return early - let it fall through to normal lookup which will find the instantiated type
			}
			// Not a template - let it fall through to be parsed as operator<
		}

		// Try to look up the qualified identifier
		auto identifierType = gSymbolTable.lookup_qualified(qual_id.qualifiedIdentifier());
		
		// Check if this is a brace initialization: ns::Template<Args>{}
		if (template_args.has_value() && current_token_.value() == "{") {
			// Parse the brace initialization using the helper
			ParseResult brace_init_result = parse_template_brace_initialization(*template_args, qual_id.name(), final_identifier);
			if (!brace_init_result.is_error() && brace_init_result.node().has_value()) {
				return brace_init_result;
			}
			// If parsing failed, fall through to function call check
		}

		// Check if this is a non-template brace initialization: ns::Type{args}
		if (!template_args.has_value() && current_token_.value() == "{") {
			std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
			StringHandle qualified_handle = StringTable::getOrInternStringHandle(qualified_name);
			auto type_it = gTypesByName.find(qualified_handle);
			if (type_it == gTypesByName.end()) {
				type_it = gTypesByName.find(final_identifier.handle());
			}
			if (type_it != gTypesByName.end()) {
				const TypeInfo* type_info_ptr = type_it->second;
				const StructTypeInfo* struct_info = type_info_ptr->getStructInfo();
				TypeIndex type_index = type_info_ptr->type_index_;

				advance(); // consume '{'

				ChunkedVector<ASTNode> args;
				while (!current_token_.kind().is_eof() && current_token_.value() != "}") {
					auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (argResult.is_error()) {
						return argResult;
					}
					if (auto node = argResult.node()) {
						args.push_back(*node);
					}
					if (current_token_.value() == ",") {
						advance();
					} else if (current_token_.value() != "}") {
						return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
					}
				}

				if (!consume("}"_tok)) {
					return ParseResult::error("Expected '}' after brace initializer", current_token_);
				}

				int type_size = struct_info ? static_cast<int>(struct_info->total_size * 8) : 0;
				auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, final_identifier);
				result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), final_identifier));
				return ParseResult::success(*result);
			}
		}
		
		// Check if followed by '(' for function call
		if (current_token_.value() == "(") {
			advance(); // consume '('

			// Parse function arguments using unified helper (expand simple packs for qualified calls)
			auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
				.handle_pack_expansion = true,
				.collect_types = true,
				.expand_simple_packs = true
			});
			if (!args_result.success) {
				return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
			}
			ChunkedVector<ASTNode> args = std::move(args_result.args);
			
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			// If not found OR if it's a template (not an instantiated function), try template instantiation
			// Also try if explicit template arguments were provided (to handle overload resolution)
			if (((!identifierType.has_value() || identifierType->is<TemplateFunctionDeclarationNode>()) ||
			     (template_args.has_value() && !template_args->empty())) && 
			    current_linkage_ != Linkage::C) {
				// Build qualified template name
				std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
				
				// Phase 1 C++20: If we have explicit template arguments, use them instead of deducing
				if (template_args.has_value() && !template_args->empty()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Using explicit template arguments for function call to '{}'", qualified_name);
					// Try to instantiate with explicit template arguments
					std::optional<ASTNode> template_inst = try_instantiate_template_explicit(qualified_name, *template_args);
					if (!template_inst.has_value()) {
						// Try with simple name
						template_inst = try_instantiate_template_explicit(qual_id.name(), *template_args);
					}
					
					if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
						identifierType = *template_inst;
						FLASH_LOG_FORMAT(Parser, Debug, "Successfully instantiated function template '{}' with explicit arguments", qualified_name);
					}
				}
				
				// If still not found and no explicit template arguments, try deducing from function arguments
				// Apply lvalue reference for forwarding deduction on arg_types
				if (!identifierType.has_value() || identifierType->is<TemplateFunctionDeclarationNode>()) {
					std::vector<TypeSpecifierNode> arg_types = apply_lvalue_reference_deduction(args, args_result.arg_types);
					
					// Try to instantiate the qualified template function
					if (!arg_types.empty()) {
						std::optional<ASTNode> template_inst = try_instantiate_template(qualified_name, arg_types);
						if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
							identifierType = *template_inst;
						}
					}
				}
			}
			
			// If still not found, create a forward declaration
			if (!identifierType) {
				// Validate namespace exists before creating forward declaration (catches f2::func when f2 undeclared)
				if (!validateQualifiedNamespace(qual_id.namespace_handle(), qual_id.identifier_token(), parsing_template_body_)) {
					return ParseResult::error(
						std::string(StringBuilder().append("Use of undeclared identifier '")
							.append(buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name()))
							.append("'").commit()),
						qual_id.identifier_token());
				}
				auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
				auto forward_decl = emplace_node<DeclarationNode>(type_node, qual_id.identifier_token());
				identifierType = forward_decl;
			}

			// Get the DeclarationNode (works for both DeclarationNode and FunctionDeclarationNode)
			const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
			if (!decl_ptr) {
				return ParseResult::error("Invalid function declaration (template args path)", current_token_);
			}

				FLASH_LOG(Parser, Debug, "Creating FunctionCallNode for qualified identifier with template args");
				// Create function call node with the qualified identifier
				result = emplace_node<ExpressionNode>(
					FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), qual_id.identifier_token()));
				
				// If explicit template arguments were provided, store them in the FunctionCallNode
				// This is needed for deferred template-dependent expressions (e.g., decltype(base_trait<T>()))
				bool has_explicit_template_args = template_args.has_value() && !template_args->empty() && !template_arg_nodes.empty();
				if (has_explicit_template_args) {
					FunctionCallNode& func_call = std::get<FunctionCallNode>(result->as<ExpressionNode>());
					func_call.set_template_arguments(std::move(template_arg_nodes));
					FLASH_LOG(Templates, Debug, "Stored ", template_arg_nodes.size(), " template argument nodes in FunctionCallNode (path 1)");
				}
				
				// Store the qualified source name for template lookup during constexpr evaluation
				std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
				FunctionCallNode& func_call = std::get<FunctionCallNode>(result->as<ExpressionNode>());
				func_call.set_qualified_name(qualified_name);
				FLASH_LOG(Parser, Debug, "Set qualified name on FunctionCallNode: ", qualified_name);
				
				// If the function has a pre-computed mangled name, set it on the FunctionCallNode
				if (identifierType->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
					if (func_decl.has_mangled_name()) {
						func_call.set_mangled_name(func_decl.mangled_name());
					}
				}
			} else {
				// Just a qualified identifier reference
				result = emplace_node<ExpressionNode>(qual_id);
			}

			if (result.has_value())
				return ParseResult::success(*result);
		}

		// Get the identifier's type information from the symbol table
		// Use template-aware lookup if we're parsing a template body OR if we have template parameters
		// in scope (e.g., when parsing template parameter defaults that reference earlier parameters)
		std::optional<ASTNode> identifierType;
		if (!current_template_param_names_.empty()) {
			// Template-aware lookup: checks if identifier is a template parameter first
			identifierType = gSymbolTable.lookup(idenfifier_token.handle(), gSymbolTable.get_current_scope_handle(), &current_template_param_names_);
			FLASH_LOG_FORMAT(Parser, Debug, "Template-aware lookup for '{}', template_params_count={}", idenfifier_token.value(), current_template_param_names_.size());
		} else {
			identifierType = lookup_symbol(idenfifier_token.handle());
		}

		FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' lookup result: {}, peek='{}', member_function_context_stack_ size={}",
			idenfifier_token.value(), identifierType.has_value() ? "found" : "not found",
			!peek().is_eof() ? peek_info().value() : "N/A",
			member_function_context_stack_.size());

		// BUGFIX: Check if we're in a member function context and this identifier is a member function
		// This handles the case where register_member_functions_in_scope already added the function to the symbol table
		// so identifierType is set, but we still need to detect it as a member function call with implicit 'this'
		// Declare this flag here so it's visible throughout the rest of the function
		bool found_member_function_in_context = false;
		if (!member_function_context_stack_.empty() && identifierType.has_value() &&
		    identifierType->is<FunctionDeclarationNode>() && peek() == "("_tok) {
			const auto& mf_ctx = member_function_context_stack_.back();
			const StructDeclarationNode* struct_node = mf_ctx.struct_node;
			if (struct_node) {
				// Check if this function is a member function of the current struct
				for (const auto& member_func : struct_node->member_functions()) {
					if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
						const auto& func_decl = member_func.function_declaration.as<FunctionDeclarationNode>();
						if (func_decl.decl_node().identifier_token().value() == idenfifier_token.value()) {
							found_member_function_in_context = true;
							FLASH_LOG_FORMAT(Parser, Debug, "EARLY CHECK: Detected member function call '{}' with implicit 'this'", idenfifier_token.value());
							break;
						}
					}
				}
				
				// If not found in current struct, search in base classes
				if (!found_member_function_in_context) {
					// Get the struct's base classes and search recursively
					TypeIndex struct_type_index = mf_ctx.struct_type_index;
					if (struct_type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[struct_type_index];
						const StructTypeInfo* struct_info = type_info.getStructInfo();
						if (struct_info) {
							// Collect base classes to search (breadth-first to handle multiple inheritance)
							std::vector<TypeIndex> base_classes_to_search;
							for (const auto& base : struct_info->base_classes) {
								base_classes_to_search.push_back(base.type_index);
							}
							
							// Search through base classes
							for (size_t i = 0; i < base_classes_to_search.size() && !found_member_function_in_context; ++i) {
								TypeIndex base_idx = base_classes_to_search[i];
								if (base_idx >= gTypeInfo.size()) continue;
								
								const TypeInfo& base_type_info = gTypeInfo[base_idx];
								const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
								if (!base_struct_info) continue;
								
								// Check member functions in this base class
								// StructMemberFunction has function_decl which is an ASTNode
								for (const auto& member_func : base_struct_info->member_functions) {
									if (member_func.getName() == idenfifier_token.handle()) {
										// Found matching member function in base class
										if (member_func.function_decl.is<FunctionDeclarationNode>()) {
											// Update identifierType to point to the base class function
											gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
											identifierType = member_func.function_decl;
											found_member_function_in_context = true;
											FLASH_LOG_FORMAT(Parser, Debug, "EARLY CHECK: Detected base class member function call '{}' with implicit 'this'", idenfifier_token.value());
											break;
										}
									}
								}
								
								// Add this base's base classes to search list (for multi-level inheritance)
								for (const auto& nested_base : base_struct_info->base_classes) {
									// Avoid duplicates (relevant for diamond inheritance)
									bool already_in_list = false;
									for (TypeIndex existing : base_classes_to_search) {
										if (existing == nested_base.type_index) {
											already_in_list = true;
											break;
										}
									}
									if (!already_in_list) {
										base_classes_to_search.push_back(nested_base.type_index);
									}
								}
							}
						}
					}
				}
			}
		}

		// BUGFIX: If we detected a member function call with implicit 'this', handle it here
		// This must be done BEFORE the `if (!identifierType)` block, because identifierType IS set
		if (found_member_function_in_context && peek() == "("_tok) {
			FLASH_LOG_FORMAT(Parser, Debug, "Handling member function call '{}' with implicit 'this'", idenfifier_token.value());
			advance(); // consume '('

			// Parse function arguments
			ChunkedVector<ASTNode> args;
			while (!current_token_.kind().is_eof() &&
			       (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")")) {
				ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (argResult.is_error()) {
					return argResult;
				}
				if (auto node = argResult.node()) {
					args.push_back(*node);
				}

				if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
					advance(); // consume ','
				} else if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
					return ParseResult::error("Expected ',' or ')' in function arguments", current_token_);
				}
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function arguments", current_token_);
			}

			// Create implicit 'this' expression
			Token this_token(Token::Type::Keyword, "this"sv, idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
			auto this_node = emplace_node<ExpressionNode>(IdentifierNode(this_token));

			// Get the FunctionDeclarationNode
			FunctionDeclarationNode& func_decl = const_cast<FunctionDeclarationNode&>(identifierType->as<FunctionDeclarationNode>());

			// Create MemberFunctionCallNode with implicit 'this'
			result = emplace_node<ExpressionNode>(
				MemberFunctionCallNode(this_node, func_decl, std::move(args), idenfifier_token));

			FLASH_LOG_FORMAT(Parser, Debug, "Created MemberFunctionCallNode for '{}'", idenfifier_token.value());
			return ParseResult::success(*result);
		}

		// BUGFIX: If identifier not found in symbol table, check static members of current struct first.
		// This handles cases like: static_assert(value == 42, "msg"); where value is a static member.
		// Static members should be visible in expressions within the same struct.
		bool found_as_type_alias = false;
		if (!identifierType && !struct_parsing_context_stack_.empty()) {
			StringHandle identifier_handle = idenfifier_token.handle();
			const auto& ctx = struct_parsing_context_stack_.back();
			FLASH_LOG_FORMAT(Parser, Debug, "Checking struct context for '{}': struct_node={}, local_struct_info={}", 
				idenfifier_token.value(), ctx.struct_node != nullptr, ctx.local_struct_info != nullptr);
			
			// Check the struct_node's static_members (for non-template structs)
			if (ctx.struct_node != nullptr) {
				for (const auto& static_member : ctx.struct_node->static_members()) {
					if (static_member.name == identifier_handle) {
						FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in current struct node (early lookup)", 
							idenfifier_token.value());
						found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
						break;
					}
				}
			}
			
			// Check local_struct_info (for template classes being parsed)
			if (!found_as_type_alias && ctx.local_struct_info != nullptr) {
				for (const auto& static_member : ctx.local_struct_info->static_members) {
					if (static_member.getName() == identifier_handle) {
						FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in local_struct_info (early lookup)", 
							idenfifier_token.value());
						found_as_type_alias = true;
						break;
					}
				}
			}
			
			// BUGFIX: Check for members imported via using-declarations
			// This handles cases like: using BaseClass::__value;
			// where the base class is a dependent template type that can't be resolved yet
			if (!found_as_type_alias) {
				for (const auto& imported_member : ctx.imported_members) {
					if (imported_member == identifier_handle) {
						FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as imported member via using-declaration", 
							idenfifier_token.value());
						found_as_type_alias = true;
						break;
					}
				}
			}
			
			// Also search base classes for static members (if base classes are resolved)
			// This handles using-declarations like: using BaseClass::__value;
			// which make base class static members accessible by their simple name
			if (!found_as_type_alias && ctx.local_struct_info != nullptr && !ctx.local_struct_info->base_classes.empty()) {
				FLASH_LOG_FORMAT(Parser, Debug, "Searching base classes for '{}', num_bases={}", 
					idenfifier_token.value(), ctx.local_struct_info->base_classes.size());
				auto [base_static_member, owner_struct] = ctx.local_struct_info->findStaticMemberRecursive(identifier_handle);
				if (base_static_member) {
					FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in base class '{}'", 
						idenfifier_token.value(), StringTable::getStringView(owner_struct->getName()));
					found_as_type_alias = true;  // Found it, suppress "Missing identifier" error
				}
			}
		}
		
		// BUGFIX: If identifier not found in symbol table, check if it's a type alias in gTypesByName
		// This allows type aliases like false_type, true_type, enable_if_t to be used in specific contexts
		// Only apply this fallback when the identifier is followed by '::' or '(' to ensure
		// we don't break legitimate cases where an identifier should be an error
		// ENHANCED: In TemplateArgument context, also check for ',' or '>' or '<' because type aliases
		// and template class names are commonly used as template arguments in <type_traits>
		if (!identifierType && !found_as_type_alias && !peek().is_eof()) {
			std::string_view peek = peek_info().value();
			// Check gTypesByName if identifier is followed by :: (qualified name), ( (constructor call), or { (brace init)
			bool should_check_types = (peek == "::" || peek == "(" || peek == "{");
			
			// In template argument context, also check for various tokens that indicate a type context.
			// Type aliases and template class names are commonly used as template arguments
			// (e.g., first_t<false_type, ...>, __or_<is_reference<T>, is_function<T>>, declval<_Tp&>())
			// The '&' and '&&' handle reference type declarators like T& or T&&
			if (!should_check_types && context == ExpressionContext::TemplateArgument) {
				should_check_types = (peek == "," || peek == ">" || peek == ">>" || peek == "<" || 
				                      peek == "&" || peek == "&&");
			}
			
			if (should_check_types) {
				StringHandle identifier_handle = idenfifier_token.handle();
				auto type_it = gTypesByName.find(identifier_handle);
				if (type_it != gTypesByName.end()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as type alias in gTypesByName (peek='{}', context={})", 
						idenfifier_token.value(), peek, context == ExpressionContext::TemplateArgument ? "TemplateArgument" : "other");
					found_as_type_alias = true;
					// Mark that we found it as a type so it can be used for type references
					// The actual type info will be retrieved later when needed
				} else {
					// Try namespace-qualified lookup: if we're inside a namespace, the type alias
					// might be registered with a qualified name (e.g., "std::size_t")
					NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
					if (!current_namespace.isGlobal()) {
						StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, identifier_handle);
						auto qualified_type_it = gTypesByName.find(qualified_handle);
						if (qualified_type_it != gTypesByName.end()) {
							FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as namespace-qualified type alias '{}' in gTypesByName", 
								idenfifier_token.value(), StringTable::getStringView(qualified_handle));
							found_as_type_alias = true;
						}
					}
					
					// If still not found, check for member type aliases in the current struct/class being parsed
					// This handles cases like: using inner_type = int; using outer_type = wrapper<inner_type>;
					if (!found_as_type_alias) {
						// Try member_function_context_stack_ first (for code inside member function bodies)
						if (!member_function_context_stack_.empty()) {
							const auto& ctx = member_function_context_stack_.back();
							if (ctx.struct_node != nullptr) {
								for (const auto& alias : ctx.struct_node->type_aliases()) {
									if (alias.alias_name == identifier_handle) {
										FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as member type alias in current struct (member func context)", 
											idenfifier_token.value());
										found_as_type_alias = true;
										break;
									}
								}
							}
						}
						
						// Then try struct_parsing_context_stack_ (for code inside struct body, e.g., type alias definitions)
						if (!found_as_type_alias && !struct_parsing_context_stack_.empty()) {
							const auto& ctx = struct_parsing_context_stack_.back();
							if (ctx.struct_node != nullptr) {
								for (const auto& alias : ctx.struct_node->type_aliases()) {
									if (alias.alias_name == identifier_handle) {
										FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as member type alias in current struct (struct parsing context)", 
											idenfifier_token.value());
										found_as_type_alias = true;
										break;
									}
								}
							}
						}
					}
					
					// If still not found, check for static data members in the current struct/class being parsed
					// This handles cases like: using type = typename aligned_storage<_S_len, alignment_value>::type;
					// where _S_len and alignment_value are static const members of the same struct
					if (!found_as_type_alias && !struct_parsing_context_stack_.empty()) {
						const auto& ctx = struct_parsing_context_stack_.back();
						// First try the struct_node's static_members (for member struct templates)
						if (ctx.struct_node != nullptr) {
							for (const auto& static_member : ctx.struct_node->static_members()) {
								if (static_member.name == identifier_handle) {
									FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in current struct node (struct parsing context)", 
										idenfifier_token.value());
									found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
									break;
								}
							}
						}
						
						// Then check local_struct_info (for template classes being parsed where static members are added)
						if (!found_as_type_alias && ctx.local_struct_info != nullptr) {
							for (const auto& static_member : ctx.local_struct_info->static_members) {
								if (static_member.getName() == identifier_handle) {
									FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in local_struct_info (struct parsing context)", 
										idenfifier_token.value());
									found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
									break;
								}
							}
						}
						
						// Finally check StructTypeInfo from gTypesByName (for already-registered types)
						if (!found_as_type_alias) {
							StringHandle struct_name_handle = StringTable::getOrInternStringHandle(ctx.struct_name);
							auto struct_type_it = gTypesByName.find(struct_name_handle);
							if (struct_type_it != gTypesByName.end() && struct_type_it->second->getStructInfo()) {
								const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
								for (const auto& static_member : struct_info->static_members) {
									if (static_member.getName() == identifier_handle) {
										FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as static member in StructTypeInfo (struct parsing context)", 
											idenfifier_token.value());
										found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
										break;
									}
								}
							}
						}
					}
				}
			}
		}
		
		// If identifier is followed by '<' and we're inside a struct context, check if it's a member struct template
		// This handles patterns like: template<typename T> struct Outer<_Tp, Inner<T>> { }
		// where Inner is a member struct template of the enclosing class
		if (!identifierType && !found_as_type_alias && peek() == "<"_tok) {
			if (!struct_parsing_context_stack_.empty()) {
				const auto& ctx = struct_parsing_context_stack_.back();
				// Build qualified name: EnclosingClass::MemberTemplate
				StringBuilder qualified_name;
				qualified_name.append(ctx.struct_name).append("::"sv).append(idenfifier_token.value());
				std::string_view qualified_name_sv = qualified_name.commit();
				auto member_template = gTemplateRegistry.lookupTemplate(qualified_name_sv);
				if (member_template.has_value()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Identifier '{}' found as member struct template '{}' in enclosing class", 
						idenfifier_token.value(), qualified_name_sv);
					found_as_type_alias = true;  // Reuse this flag to prevent "Missing identifier" error
				}
			}
		}
		
		// If identifier is followed by ::, it might be a namespace-qualified identifier
		// This handles both: 
		// 1. Identifier not found (might be namespace name)
		// 2. Identifier found but followed by :: (namespace or class scope resolution)
		if (peek() == "::"_tok) {
			// Parse as qualified identifier: Namespace::identifier
			// Even if we don't know if it's a namespace, try parsing it as a qualified identifier
			std::vector<StringType<32>> namespaces;
			Token final_identifier = idenfifier_token;
			
			// Collect the qualified path
			while (peek() == "::"_tok) {
				namespaces.emplace_back(StringType<32>(final_identifier.value()));
				advance(); // consume ::
				
				// Get next identifier
				if (!peek().is_identifier()) {
					return ParseResult::error("Expected identifier after '::'", peek_info());
				}
				final_identifier = peek_info();
				advance(); // consume the identifier
			}
			
			FLASH_LOG(Parser, Debug, "Qualified identifier: final name = '{}'", final_identifier.value());
			
			// Check if final identifier is followed by template arguments: ns::Template<Args>
			std::optional<std::vector<TemplateTypeArg>> template_args;
			std::vector<ASTNode> template_arg_nodes;  // Store the actual expression nodes
			if (peek() == "<"_tok) {
				// Before parsing < as template arguments, check if the identifier is actually a template
				// This prevents misinterpreting patterns like R1<T>::num < R2<T>::num> where < is comparison
				
				// Build the full qualified name for template lookup
				StringBuilder lookup_name_builder;
				for (const auto& ns : namespaces) {
					lookup_name_builder.append(ns.c_str()).append("::");
				}
				lookup_name_builder.append(final_identifier.value());
				std::string_view qualified_lookup_name = lookup_name_builder.preview();
				
				// Check if this is a known template (class or variable template)
				auto template_opt = gTemplateRegistry.lookupTemplate(qualified_lookup_name);
				auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_lookup_name);
				auto alias_template_opt = gTemplateRegistry.lookup_alias_template(qualified_lookup_name);
				
				// Also check with just the simple name
				auto simple_template_opt = gTemplateRegistry.lookupTemplate(final_identifier.value());
				auto simple_var_template_opt = gTemplateRegistry.lookupVariableTemplate(final_identifier.value());
				auto simple_alias_template_opt = gTemplateRegistry.lookup_alias_template(final_identifier.value());
				
				bool is_known_template = template_opt.has_value() || var_template_opt.has_value() ||
				                         alias_template_opt.has_value() ||
				                         simple_template_opt.has_value() || simple_var_template_opt.has_value() ||
				                         simple_alias_template_opt.has_value();
				
				lookup_name_builder.reset();
				
				if (is_known_template) {
					FLASH_LOG(Parser, Debug, "Qualified identifier followed by '<', attempting to parse template arguments");
					template_args = parse_explicit_template_arguments(&template_arg_nodes);
					// If parsing failed, it might be a less-than operator, continue normally
				} else if (context == ExpressionContext::TemplateArgument) {
					// In template argument context, if the identifier is NOT a known template,
					// treat '<' as a comparison operator (e.g., R1<T>::num < R2<T>::num>)
					FLASH_LOG_FORMAT(Parser, Debug, 
					    "In TemplateArgument context, qualified identifier '{}' is not a known template - treating '<' as comparison operator",
					    final_identifier.value());
					// Don't parse template arguments - let the binary operator loop handle '<' as comparison
				} else {
					// Not in template argument context and not a known template
					// Try parsing template arguments anyway (might be a forward-declared template)
					FLASH_LOG(Parser, Debug, "Qualified identifier followed by '<', attempting to parse template arguments (unknown template)");
					template_args = parse_explicit_template_arguments(&template_arg_nodes);
					// If parsing failed, it might be a less-than operator, continue normally
				}
			}
			
			// Create a QualifiedIdentifierNode with namespace handle
			NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
			auto qualified_node_ast = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
			const auto& qual_id = qualified_node_ast.as<QualifiedIdentifierNode>();
			
			// Look up the qualified identifier (either the template name or instantiated template)
			if (template_args.has_value()) {
				// Try to instantiate the template with namespace qualification
				// Build the qualified template name for lookup
				std::string_view qualified_template_name = buildQualifiedNameFromHandle(ns_handle, final_identifier.value());
				
				FLASH_LOG_FORMAT(Parser, Debug, "Looking up template '{}' with {} template arguments", qualified_template_name, template_args->size());
				
				// First, check if this is a variable template
				auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_template_name);
				if (var_template_opt.has_value()) {
					// Instantiate the variable template
					auto instantiated_var = try_instantiate_variable_template(qualified_template_name, *template_args);
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
							inst_name = qualified_template_name;  // Fallback
						}
						
						FLASH_LOG(Templates, Debug, "Successfully instantiated qualified variable template: ", qualified_template_name);
						
						// Return identifier reference to the instantiated variable
						Token inst_token(Token::Type::Identifier, inst_name, 
						                final_identifier.line(), final_identifier.column(), final_identifier.file_index());
						result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
						return ParseResult::success(*result);
					}
				}
				
				// Try to instantiate as class template with qualified name first
				auto instantiated = try_instantiate_class_template(qualified_template_name, *template_args);
				
				// If that didn't work, try with simple name (for backward compatibility)
				if (!instantiated.has_value()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Qualified name lookup failed, trying simple name '{}'", final_identifier.value());
					instantiated = try_instantiate_class_template(final_identifier.value(), *template_args);
				}
				
				if (instantiated.has_value()) {
					const auto& inst_struct = instantiated->as<StructDeclarationNode>();
					
					// Look up the instantiated template
					identifierType = gSymbolTable.lookup(StringTable::getStringView(inst_struct.name()));
					
					// Check for :: after template arguments (Template<T>::member)
					if (peek() == "::"_tok) {
						auto qualified_result = parse_qualified_identifier_after_template(final_identifier);
						if (!qualified_result.is_error() && qualified_result.node().has_value()) {
							auto qualified_node2 = qualified_result.node()->as<QualifiedIdentifierNode>();
							auto member_call_result = try_parse_member_template_function_call(
								StringTable::getStringView(inst_struct.name()),
								qualified_node2.name(),
								qualified_node2.identifier_token());
							if (member_call_result.has_value()) {
								if (member_call_result->is_error()) {
									return *member_call_result;
								}
								return ParseResult::success(*member_call_result->node());
							}
							result = emplace_node<ExpressionNode>(qualified_node2);
							return ParseResult::success(*result);
						}
					}
					
					// Check if this is a brace initialization: ns::Template<Args>{}
					if (peek() == "{"_tok) {
						advance(); // consume '{'
						
						ChunkedVector<ASTNode> args;
						while (!peek().is_eof() && peek() != "}"_tok) {
							auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (argResult.is_error()) {
								return argResult;
							}
							if (auto node = argResult.node()) {
								args.push_back(*node);
							}
							
							if (peek() == ","_tok) {
								advance(); // consume ','
							} else if (peek() != "}"_tok) {
								return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
							}
						}
						
						if (!consume("}"_tok)) {
							return ParseResult::error("Expected '}' after brace initializer", current_token_);
						}
						
						// Look up the instantiated type
						auto type_handle = StringTable::getOrInternStringHandle(StringTable::getStringView(inst_struct.name()));
						auto type_it = gTypesByName.find(type_handle);
						if (type_it != gTypesByName.end()) {
							// Create TypeSpecifierNode for the instantiated class
							const TypeInfo& type_info = *type_it->second;
							TypeIndex type_index = type_info.type_index_;
							int type_size = 0;
							if (type_info.struct_info_) {
								type_size = static_cast<int>(type_info.struct_info_->total_size * 8);
							}
							auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, final_identifier);
							
							// Create ConstructorCallNode
							result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), final_identifier));
							return ParseResult::success(*result);
						} else {
							return ParseResult::error("Failed to look up instantiated template type", final_identifier);
						}
					}
					
					// Return identifier reference to the instantiated template
					Token inst_token(Token::Type::Identifier, StringTable::getStringView(inst_struct.name()), 
					                final_identifier.line(), final_identifier.column(), final_identifier.file_index());
					result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
					return ParseResult::success(*result);
				}
				
				// If class/variable template instantiation failed, try function template instantiation
				// This handles cases like: ns::func<int, int>()
				if (!identifierType.has_value()) {
					FLASH_LOG_FORMAT(Templates, Debug, "Trying function template instantiation for '{}' with {} args", 
					                 qualified_template_name, template_args->size());
					auto func_template_inst = try_instantiate_template_explicit(qualified_template_name, *template_args);
					if (func_template_inst.has_value() && func_template_inst->is<FunctionDeclarationNode>()) {
						identifierType = *func_template_inst;
						FLASH_LOG(Templates, Debug, "Successfully instantiated function template with explicit arguments");
					}
				}
			} else {
				// No template arguments, lookup as regular qualified identifier
				identifierType = gSymbolTable.lookup_qualified(qual_id.qualifiedIdentifier());
			}
			
			FLASH_LOG(Parser, Debug, "Qualified lookup result: {}", identifierType.has_value() ? "found" : "not found");
			
			// Check if this is a function call (even if not found - might be a template)
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
				
				// If not found and we're not in extern "C", try template instantiation
				if (!identifierType.has_value() && current_linkage_ != Linkage::C) {
					// Build qualified template name
					std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
					
					// If explicit template arguments were provided, use them for instantiation
					if (template_args.has_value() && !template_args->empty()) {
						FLASH_LOG_FORMAT(Templates, Debug, "Instantiating function template '{}' with {} explicit template arguments", 
						                 qualified_name, template_args->size());
						std::optional<ASTNode> template_inst = try_instantiate_template_explicit(qualified_name, *template_args);
						if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
							identifierType = *template_inst;
							FLASH_LOG(Templates, Debug, "Successfully instantiated function template with explicit arguments");
						}
					} else {
						// Apply lvalue reference for forwarding deduction on arg_types
						std::vector<TypeSpecifierNode> arg_types = apply_lvalue_reference_deduction(args, args_result.arg_types);
						
						// Try to instantiate the qualified template function using argument deduction
						if (!arg_types.empty()) {
							std::optional<ASTNode> template_inst = try_instantiate_template(qualified_name, arg_types);
							if (template_inst.has_value() && template_inst->is<FunctionDeclarationNode>()) {
								identifierType = *template_inst;
							} else {
							}
						} else {
						}
					}
				}
				
				// Get the DeclarationNode
				if (identifierType.has_value() && identifierType->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
					if (!func_decl.get_definition().has_value()) {
						std::string_view qualified_scope = gNamespaceRegistry.getQualifiedName(qual_id.namespace_handle());
						if (qualified_scope.find('$') != std::string_view::npos) {
							StringHandle class_name_handle = StringTable::getOrInternStringHandle(qualified_scope);
							StringHandle member_name_handle = qual_id.identifier_token().handle();
							if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(class_name_handle, member_name_handle)) {
								auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(class_name_handle, member_name_handle);
								if (lazy_info_opt.has_value()) {
									auto instantiated_func = instantiateLazyMemberFunction(*lazy_info_opt);
									if (instantiated_func.has_value()) {
										identifierType = *instantiated_func;
										LazyMemberInstantiationRegistry::getInstance().markInstantiated(class_name_handle, member_name_handle);
									}
								}
							}
						}
					}
				}

				const DeclarationNode* decl_ptr = identifierType.has_value() ? getDeclarationNode(*identifierType) : nullptr;
				if (!decl_ptr) {
					return ParseResult::error("Invalid function declaration (qualified id path)", final_identifier);
				}
				
				// Create function call node with the qualified identifier
				auto function_call_node = emplace_node<ExpressionNode>(
					FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), final_identifier));
				
				// If explicit template arguments were provided, store them in the FunctionCallNode
				// This is needed for deferred template-dependent expressions (e.g., decltype(base_trait<T>()))
				if (template_args.has_value() && !template_args->empty() && !template_arg_nodes.empty()) {
					std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_template_arguments(std::move(template_arg_nodes));
					FLASH_LOG(Templates, Debug, "Stored ", template_arg_nodes.size(), " template argument nodes in FunctionCallNode");
				}
				
				// If the function has a pre-computed mangled name, set it on the FunctionCallNode
				if (identifierType->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
					FLASH_LOG(Parser, Debug, "Namespace-qualified function has mangled name: {}, name: {}", func_decl.has_mangled_name(), func_decl.mangled_name());
					if (func_decl.has_mangled_name()) {
						std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
						FLASH_LOG(Parser, Debug, "Set mangled name on namespace-qualified FunctionCallNode: {}", func_decl.mangled_name());
					}
				}
				
				result = function_call_node;
				return ParseResult::success(*result);
			} else if (identifierType.has_value()) {
				// Just a qualified identifier reference (e.g., Namespace::globalValue)
				result = emplace_node<ExpressionNode>(qual_id);
				return ParseResult::success(*result);
			}
			// If identifierType is still not found, fall through to error handling below
		}
		
		// If identifier not found in symbol table, check if it's a class/struct type name
		// This handles constructor calls like Widget(42)
		if (!identifierType.has_value()) {
			auto type_it = gTypesByName.find(idenfifier_token.handle());
			if (type_it != gTypesByName.end() && peek() == "("_tok) {
				// This is a constructor call - handle it directly here
				advance();  // consume '('
				
				// Parse constructor arguments
				ChunkedVector<ASTNode> args;
				while (!current_token_.kind().is_eof() && 
				       (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")")) {
					auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (argResult.is_error()) {
						return argResult;
					}
					if (auto node = argResult.node()) {
						args.push_back(*node);
					}
					
					if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
						advance();  // consume ','
					} else if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
						return ParseResult::error("Expected ',' or ')' in constructor arguments", current_token_);
					}
				}
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after constructor arguments", current_token_);
				}
				
				// Create TypeSpecifierNode for the class
				TypeIndex type_index = type_it->second->type_index_;
				int type_size = 0;
				if (type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_index];
					if (type_info.struct_info_) {
						type_size = static_cast<unsigned char>(type_info.struct_info_->total_size * 8);
					}
				}
				auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, idenfifier_token);
				
				// Create ConstructorCallNode
				result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
				return ParseResult::success(*result);
			}
		}
		
		// If the identifier is a template parameter reference, check for constructor calls
		// This handles both T(42) and T{} patterns for dependent type construction
		if (identifierType.has_value() && identifierType->is<TemplateParameterReferenceNode>()) {
			const auto& tparam_ref = identifierType->as<TemplateParameterReferenceNode>();
			
			// Check for brace initialization: T{} or T{args}
			if (peek() == "{"_tok) {
				advance(); // consume '{'
				
				// Parse brace initializer arguments
				ChunkedVector<ASTNode> args;
				while (current_token_.value() != "}") {
					auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (argResult.is_error()) {
						return argResult;
					}
					if (auto node = argResult.node()) {
						args.push_back(*node);
					}
					
					if (current_token_.value() == ",") {
						advance(); // consume ','
					} else if (current_token_.kind().is_eof() || current_token_.value() != "}") {
						return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
					}
				}
				
				if (!consume("}"_tok)) {
					return ParseResult::error("Expected '}' after brace initializer", current_token_);
				}
				
				// Create TypeSpecifierNode for the template parameter (dependent type)
				auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, idenfifier_token);
				
				// Create ConstructorCallNode for brace initialization
				result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
				return ParseResult::success(*result);
			}
			
			// Wrap it in an ExpressionNode, but continue checking for '(' constructor calls below
			result = emplace_node<ExpressionNode>(tparam_ref);
			// Don't return - let it fall through to check for '(' below
		}
		
		// Special case: if the identifier is not found but is followed by '...', 
		// it might be a pack parameter that was expanded (e.g., "args" -> "args_0", "args_1", etc.)
		// Allow it to proceed so pack expansion can handle it
		bool is_pack_expansion = false;
		if (!identifierType.has_value() && peek() == "..."_tok) {
			is_pack_expansion = true;
		}

		// Check if this is a template function call
		// First, check if the name matches a static member function of the current class
		// This implements C++ name resolution: class scope takes priority over enclosing namespace scope
		if (identifierType && identifierType->is<TemplateFunctionDeclarationNode>() &&
		    peek() == "("_tok) {
			auto check_class_members = [&](const StructDeclarationNode* struct_node) -> bool {
				if (!struct_node) return false;
				for (const auto& member_func : struct_node->member_functions()) {
					if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
						const auto& func_decl = member_func.function_declaration.as<FunctionDeclarationNode>();
						if (func_decl.decl_node().identifier_token().value() == idenfifier_token.value()) {
							identifierType = member_func.function_declaration;
							// Register in symbol table so overload resolution can find it
							gSymbolTable.insert(idenfifier_token.value(), member_func.function_declaration);
							// Mark that we found a static member to prevent MemberFunctionCallNode path
							found_member_function_in_context = false;
							FLASH_LOG_FORMAT(Parser, Debug, "Resolved '{}' as static member function of current class (overrides namespace template)", idenfifier_token.value());
							return true;
						}
					}
				}
				return false;
			};
			
			// Check struct_parsing_context_stack_ (inline member function parsing)
			if (!struct_parsing_context_stack_.empty()) {
				check_class_members(struct_parsing_context_stack_.back().struct_node);
			}
			// Check member_function_context_stack_ (delayed function body parsing)
			if (identifierType->is<TemplateFunctionDeclarationNode>() && !member_function_context_stack_.empty()) {
				check_class_members(member_function_context_stack_.back().struct_node);
			}
		}
		if (identifierType && identifierType->is<TemplateFunctionDeclarationNode>() &&
		    consume("("_tok)) {
			
			// Parse arguments to deduce template parameters
			if (peek().is_eof())
				return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

			ChunkedVector<ASTNode> args;
			std::vector<TypeSpecifierNode> arg_types;
			
			while (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
				ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (argResult.is_error()) {
					return argResult;
				}

				if (auto node = argResult.node()) {
					args.push_back(*node);
					
					// Try to deduce the type of this argument
					if (node->is<ExpressionNode>()) {
						const auto& expr = node->as<ExpressionNode>();
						std::optional<TypeSpecifierNode> arg_type_node_opt;
						Type arg_type = Type::Int;  // Default assumption
						bool is_lvalue = false;  // Track if this is an lvalue for perfect forwarding
					
						std::visit([&](const auto& inner) {
							using T = std::decay_t<decltype(inner)>;
							if constexpr (std::is_same_v<T, BoolLiteralNode>) {
								arg_type = Type::Bool;
								// Boolean literals are rvalues
							} else if constexpr (std::is_same_v<T, NumericLiteralNode>) {
								arg_type = inner.type();
								// Literals are rvalues
							} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
								arg_type = Type::Char;  // const char*
								// String literals are lvalues (but typically decay to pointers)
							} else if constexpr (std::is_same_v<T, IdentifierNode>) {
								// Look up the identifier's type
								auto id_type = lookup_symbol(StringTable::getOrInternStringHandle(inner.name()));
								if (id_type.has_value()) {
									if (const DeclarationNode* decl = get_decl_from_symbol(*id_type)) {
										if (decl->type_node().template is<TypeSpecifierNode>()) {
											// Preserve the full TypeSpecifierNode to retain type_index for structs
											const auto& type_spec = decl->type_node().template as<TypeSpecifierNode>();
											arg_type_node_opt = type_spec;
											arg_type = type_spec.type();
											// Named variables are lvalues
											is_lvalue = true;
										}
									}
								}
							}
						}, expr);
					
						TypeSpecifierNode arg_type_node = arg_type_node_opt.value_or(
							TypeSpecifierNode(arg_type, TypeQualifier::None, get_type_size_bits(arg_type), Token())
						);
						if (is_lvalue) {
							// Mark as lvalue reference for perfect forwarding template deduction
							arg_type_node.set_lvalue_reference(true);
						}
						arg_types.push_back(arg_type_node);
					}
				}
				
				// Check for pack expansion (...) after the argument in variadic template function calls
				// Only expand if the argument is an identifier matching a known pack parameter name
				if (peek() == "..."_tok && !pack_param_info_.empty() && !args.empty()) {
					// Check if the last argument is an identifier matching a pack parameter
					const PackParamInfo* matching_pack = nullptr;
					ASTNode& last_arg = args[args.size() - 1];
					if (last_arg.is<ExpressionNode>()) {
						const auto& expr = last_arg.as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(expr)) {
							const auto& id = std::get<IdentifierNode>(expr);
							for (const auto& pack_info : pack_param_info_) {
								if (id.name() == pack_info.original_name && pack_info.pack_size > 0) {
									matching_pack = &pack_info;
									break;
								}
							}
						}
					}
					
					if (matching_pack) {
						advance(); // consume '...'
						
						size_t pre_pack_size = args.size();
						bool first_element = true;
						for (size_t pi = 0; pi < matching_pack->pack_size; ++pi) {
							StringBuilder param_name_builder;
							param_name_builder.append(matching_pack->original_name);
							param_name_builder.append('_');
							param_name_builder.append(pi);
							std::string_view expanded_name = param_name_builder.commit();
							
							auto sym = lookup_symbol(StringTable::getOrInternStringHandle(expanded_name));
							if (sym.has_value()) {
								Token id_token(Token::Type::Identifier, expanded_name, 0, 0, 0);
								auto id_node = emplace_node<ExpressionNode>(IdentifierNode(id_token));
								
								if (first_element && pre_pack_size > 0) {
									// Overwrite the last element (the unexpanded pack name)
									args[pre_pack_size - 1] = id_node;
									if (!arg_types.empty()) {
										if (const DeclarationNode* decl = get_decl_from_symbol(*sym)) {
											if (decl->type_node().is<TypeSpecifierNode>()) {
												arg_types.back() = decl->type_node().as<TypeSpecifierNode>();
												arg_types.back().set_lvalue_reference(true);
											}
										}
									}
									first_element = false;
								} else {
									args.push_back(id_node);
									if (const DeclarationNode* decl = get_decl_from_symbol(*sym)) {
										if (decl->type_node().is<TypeSpecifierNode>()) {
											TypeSpecifierNode arg_type_node_pack = decl->type_node().as<TypeSpecifierNode>();
											arg_type_node_pack.set_lvalue_reference(true);
											arg_types.push_back(arg_type_node_pack);
										}
									}
								}
							}
						}
					}
				}
				
				if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
					advance(); // Consume comma
				}
				else if (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
					return ParseResult::error("Expected ',' or ')' after function argument", current_token_);
				}

				if (peek().is_eof())
					return ParseResult::error(ParserError::NotImplemented, Token());
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after function call arguments", current_token_);
			}

			// Try to instantiate the template function (skip in extern "C" contexts - C has no templates)
			std::optional<ASTNode> template_func_inst;
			if (current_linkage_ != Linkage::C) {
				template_func_inst = try_instantiate_template(idenfifier_token.value(), arg_types);
			}
			
			if (template_func_inst.has_value() && template_func_inst->is<FunctionDeclarationNode>()) {
				const auto& func = template_func_inst->as<FunctionDeclarationNode>();
				auto function_call_node = emplace_node<ExpressionNode>(
					FunctionCallNode(const_cast<DeclarationNode&>(func.decl_node()), std::move(args), idenfifier_token));
				
				// Set the mangled name on the function call if the instantiated function has one
				if (func.has_mangled_name()) {
					std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_mangled_name(func.mangled_name());
				}
				
				result = function_call_node;
				return ParseResult::success(*result);
			} else {
				// Template instantiation failed - always return error.
				// In SFINAE context (e.g., requires expression), the caller
				// (parse_requires_expression) handles errors by marking the
				// requirement as unsatisfied (false node).
				FLASH_LOG(Parser, Error, "Template instantiation failed");
				return ParseResult::error("Failed to instantiate template function", idenfifier_token);
			}
		}

		if (!identifierType) {
			// Check if this is a template function before treating it as missing
			if (current_token_.value() == "(" &&
			    gTemplateRegistry.lookupTemplate(idenfifier_token.value()).has_value()) {
				// Don't set identifierType - fall through to the function call handling below
				// which will trigger template instantiation
			}
			// If we're inside a member function, check if this is a member variable
			else if (!member_function_context_stack_.empty()) {
				const auto& member_func_ctx = member_function_context_stack_.back();
				const StructDeclarationNode* struct_node = member_func_ctx.struct_node;

				// Check if this identifier matches any data member in the struct (including inherited members)
				// First try AST node members (for regular structs), then fall back to TypeInfo (for template instantiations)
				bool found_in_ast = false;
				if (struct_node && !struct_node->members().empty()) {
					// First check direct members
					for (const auto& member_decl : struct_node->members()) {
						const ASTNode& member_node = member_decl.declaration;
						if (member_node.is<DeclarationNode>()) {
							const DeclarationNode& decl = member_node.as<DeclarationNode>();
							if (decl.identifier_token().value() == idenfifier_token.value()) {
								// This is a member variable! Transform it into this->member
								// Create a "this" token with the correct value
								Token this_token(Token::Type::Keyword, "this"sv,
								                 idenfifier_token.line(), idenfifier_token.column(),
								                 idenfifier_token.file_index());
								auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

								// Create member access node: this->member
								result = emplace_node<ExpressionNode>(
									MemberAccessNode(this_ident, idenfifier_token));

								// Don't return - let it fall through to postfix operator parsing
								found_in_ast = true;
								goto found_member_variable;
							}
						}
					}

					// Also check base class members
					for (const auto& base : struct_node->base_classes()) {
						// Look up the base class type
						auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base.name));
						if (base_type_it != gTypesByName.end()) {
							const TypeInfo* base_type_info = base_type_it->second;
							TypeIndex base_type_index = base_type_info->type_index_;

							// Check if the identifier is a member of the base class (recursively)
							auto member_result = FlashCpp::gLazyMemberResolver.resolve(base_type_index, idenfifier_token.handle());
							if (member_result) {
								// This is an inherited member variable! Transform it into this->member
								Token this_token(Token::Type::Keyword, "this"sv,
								                 idenfifier_token.line(), idenfifier_token.column(),
								                 idenfifier_token.file_index());
								auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

								// Create member access node: this->member
								result = emplace_node<ExpressionNode>(
									MemberAccessNode(this_ident, idenfifier_token));

								// Don't return - let it fall through to postfix operator parsing
								found_in_ast = true;
								goto found_member_variable;
							}
						}
					}
				}
				
				// If not found in AST, try TypeInfo (or local_struct_info for static member initializers)
				// This handles template class instantiations and static member initializers
				if (!found_in_ast) {
					// First try local_struct_info (for static member initializers where TypeInfo::struct_info_ isn't populated yet)
					const StructTypeInfo* struct_info = member_func_ctx.local_struct_info;
					
					// Fall back to TypeInfo lookup if no local_struct_info
					if (!struct_info && member_func_ctx.struct_type_index != 0 && member_func_ctx.struct_type_index < gTypeInfo.size()) {
						const TypeInfo& struct_type_info = gTypeInfo[member_func_ctx.struct_type_index];
						struct_info = struct_type_info.getStructInfo();
					}
					
					if (struct_info) {
						// FIRST check static members (these don't use this->)
						// Use findStaticMemberRecursive to also search base classes
						
						// Trigger lazy static member instantiation if needed
						StringHandle member_name_handle = idenfifier_token.handle();
						instantiateLazyStaticMember(struct_info->name, member_name_handle);
						
						auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_name_handle);
						if (static_member) {
							// Found static member! Create a simple identifier node
							// Static members are accessed directly, not via this->
							result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
							// Set identifierType to prevent "Missing identifier" error
							identifierType = emplace_node<DeclarationNode>(
								emplace_node<TypeSpecifierNode>(
									static_member->type,
									static_member->type_index,
									static_cast<unsigned char>(static_member->size * 8),
									idenfifier_token
								),
								idenfifier_token
							);
							goto found_member_variable;
						}
						
						// Check instance members (these use this->)
						for (const auto& member : struct_info->members) {
							if (member.getName() == idenfifier_token.handle()) {
								// This is a member variable! Transform it into this->member
								Token this_token(Token::Type::Keyword, "this"sv,
								                 idenfifier_token.line(), idenfifier_token.column(),
								                 idenfifier_token.file_index());
								auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

								// Create member access node: this->member
								result = emplace_node<ExpressionNode>(
									MemberAccessNode(this_ident, idenfifier_token));

								// Don't return - let it fall through to postfix operator parsing
								goto found_member_variable;
							}
						}
						
						// Also check base class members
						auto member_result = FlashCpp::gLazyMemberResolver.resolve(member_func_ctx.struct_type_index, idenfifier_token.handle());
						if (member_result) {
							// This is an inherited member variable! Transform it into this->member
							Token this_token(Token::Type::Keyword, "this"sv,
							                 idenfifier_token.line(), idenfifier_token.column(),
							                 idenfifier_token.file_index());
							auto this_ident = emplace_node<ExpressionNode>(IdentifierNode(this_token));

							// Create member access node: this->member
							result = emplace_node<ExpressionNode>(
								MemberAccessNode(this_ident, idenfifier_token));

							// Don't return - let it fall through to postfix operator parsing
							goto found_member_variable;
						}
					}
				}
			}

			// Check if this is a member function call (identifier not found but matches a member function)
			// This handles the complete-class context where member functions declared later can be called
			// We need to track if we found a member function so we can create MemberFunctionCallNode with implicit 'this'
			if (!member_function_context_stack_.empty() && peek() == "("_tok) {
				FLASH_LOG_FORMAT(Parser, Debug, "Checking member function context for '{}', stack size: {}",
					idenfifier_token.value(), member_function_context_stack_.size());
				const auto& mf_ctx = member_function_context_stack_.back();
				const StructDeclarationNode* struct_node = mf_ctx.struct_node;
				if (struct_node) {
					FLASH_LOG_FORMAT(Parser, Debug, "Struct node available, member_functions count: {}",
						struct_node->member_functions().size());
					// Helper lambda to search for member function in a struct and its base classes
					// Returns true if found and sets identifierType
					bool found = false;

					// First, check the current struct's member functions
					for (const auto& member_func : struct_node->member_functions()) {
						if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
							const auto& func_decl = member_func.function_declaration.as<FunctionDeclarationNode>();
							FLASH_LOG_FORMAT(Parser, Debug, "Comparing '{}' with member function '{}'",
								idenfifier_token.value(), func_decl.decl_node().identifier_token().value());
							if (func_decl.decl_node().identifier_token().value() == idenfifier_token.value()) {
								// Found matching member function - add it to symbol table and set identifierType
								FLASH_LOG_FORMAT(Parser, Debug, "FOUND member function '{}' in context!", idenfifier_token.value());
								gSymbolTable.insert(idenfifier_token.value(), member_func.function_declaration);
								identifierType = member_func.function_declaration;
								found = true;
								found_member_function_in_context = true;
								break;
							}
						}
					}
					FLASH_LOG_FORMAT(Parser, Debug, "After search: found={}, found_member_function_in_context={}",
						found, found_member_function_in_context);

					// If not found in current struct, search in base classes
					if (!found) {
						// Get the struct's base classes and search recursively
						TypeIndex struct_type_index = mf_ctx.struct_type_index;
						if (struct_type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[struct_type_index];
							const StructTypeInfo* struct_info = type_info.getStructInfo();
							if (struct_info) {
								// Collect base classes to search (breadth-first to handle multiple inheritance)
								std::vector<TypeIndex> base_classes_to_search;
								for (const auto& base : struct_info->base_classes) {
									base_classes_to_search.push_back(base.type_index);
								}
								
								// Search through base classes
								for (size_t i = 0; i < base_classes_to_search.size() && !found; ++i) {
									TypeIndex base_idx = base_classes_to_search[i];
									if (base_idx >= gTypeInfo.size()) continue;
									
									const TypeInfo& base_type_info = gTypeInfo[base_idx];
									const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
									if (!base_struct_info) continue;
									
									// Check member functions in this base class
									// StructMemberFunction has function_decl which is an ASTNode
									for (const auto& member_func : base_struct_info->member_functions) {
										if (member_func.getName() == idenfifier_token.handle()) {
											// Found matching member function in base class
											if (member_func.function_decl.is<FunctionDeclarationNode>()) {
												gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
												identifierType = member_func.function_decl;
												found = true;
												found_member_function_in_context = true;
												break;
											}
										}
									}
									
									// Add this base's base classes to search list (for multi-level inheritance)
									for (const auto& nested_base : base_struct_info->base_classes) {
										// Avoid duplicates (relevant for diamond inheritance)
										bool already_in_list = false;
										for (TypeIndex existing : base_classes_to_search) {
											if (existing == nested_base.type_index) {
												already_in_list = true;
												break;
											}
										}
										if (!already_in_list) {
											base_classes_to_search.push_back(nested_base.type_index);
										}
									}
								}
							}
						}
					}
				}
			}

			// Check if the identifier is a lambda variable
			// Lambda variables should not be treated as function calls here,
			// but should fall through to postfix operator parsing which will handle operator() calls
			bool is_lambda_variable = false;
			if (identifierType.has_value()) {
				// Check if this is a variable declaration with a lambda type
				if (identifierType->is<VariableDeclarationNode>()) {
					const auto& var_decl = identifierType->as<VariableDeclarationNode>();
					const DeclarationNode& decl = var_decl.declaration();
					const ASTNode& type_node = decl.type_node();
					if (type_node.is<TypeSpecifierNode>()) {
						const auto& type_spec = type_node.as<TypeSpecifierNode>();
						// Check if it's a struct type (lambdas are represented as structs)
						if (type_spec.type() == Type::Struct) {
							// Get the type index to look up the type name
							TypeIndex type_idx = type_spec.type_index();
							FLASH_LOG_FORMAT(Parser, Debug, "Checking if '{}' is lambda variable: type_idx={}, gTypeInfo.size()={}", 
								idenfifier_token.value(), type_idx, gTypeInfo.size());
							if (type_idx < gTypeInfo.size()) {
								const TypeInfo& type_info = gTypeInfo[type_idx];
								if (type_info.struct_info_) {
									// Check if the struct name starts with "__lambda_"
									std::string_view type_name = StringTable::getStringView(type_info.struct_info_->name);
									FLASH_LOG_FORMAT(Parser, Debug, "Type name for '{}': '{}', starts_with __lambda_: {}", 
										idenfifier_token.value(), type_name, type_name.starts_with("__lambda_"));
									if (type_name.starts_with("__lambda_")) {
										is_lambda_variable = true;
									}
								}
							}
						}
					}
				}
			}
			
			FLASH_LOG_FORMAT(Parser, Debug, "is_lambda_variable for '{}': {}", idenfifier_token.value(), is_lambda_variable);

			// Check if this is a function call or constructor call (forward reference)
			// Identifier already consumed at line 1621
			// Skip this check for lambda variables - they should be handled by postfix operator parsing
			if (!is_lambda_variable && consume("("_tok)) {
				// First, check if this is a type name (constructor call)
				auto type_it = gTypesByName.find(idenfifier_token.handle());
				if (type_it != gTypesByName.end()) {
					// This is a constructor call: TypeName(args)
					// Parse constructor arguments
					ChunkedVector<ASTNode> args;
					while (!current_token_.kind().is_eof() && 
					       (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")")) {
						ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (argResult.is_error()) {
							return argResult;
						}
						if (auto node = argResult.node()) {
							args.push_back(*node);
						}
						
						if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
							advance(); // Consume comma
						}
						else if (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
							return ParseResult::error("Expected ',' or ')' after constructor argument", current_token_);
						}
					}
					
					if (!consume(")"_tok)) {
						FLASH_LOG(Parser, Error, "Failed to consume ')' after constructor arguments, current token: ", 
						          current_token_.value());
						return ParseResult::error("Expected ')' after constructor arguments", current_token_);
					}
				
					// Create TypeSpecifierNode for the constructor call
					TypeIndex type_index = type_it->second->type_index_;
					int type_size = 0;
					// Look up the size
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						if (type_info.struct_info_) {
							type_size = static_cast<unsigned char>(type_info.struct_info_->total_size * 8);
						}
					}
					auto type_spec_node = emplace_node<TypeSpecifierNode>(
						Type::Struct, type_index, type_size, idenfifier_token);
				
					result = emplace_node<ExpressionNode>(
						ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
					return ParseResult::success(*result);
				}
				
				// Not a constructor - check if this is a template function that needs instantiation
				// Skip template lookup if we already found this as a member function in the class context
				// to avoid namespace-scope template functions shadowing class member function overloads
				std::optional<ASTNode> template_func_inst;
				if (!found_member_function_in_context && gTemplateRegistry.lookupTemplate(idenfifier_token.value()).has_value()) {
					// Parse arguments to deduce template parameters
					if (peek().is_eof())
						return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

					ChunkedVector<ASTNode> args;
					std::vector<TypeSpecifierNode> arg_types;
					
					while (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
						ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (argResult.is_error()) {
							return argResult;
						}

						if (auto node = argResult.node()) {
							args.push_back(*node);
							
							// Try to deduce the type of this argument
							// For now, we'll use a simple heuristic
							if (node->is<ExpressionNode>()) {
								const auto& expr = node->as<ExpressionNode>();
								Type arg_type = Type::Int;  // Default assumption
								
								std::visit([&](const auto& inner) {
									using T = std::decay_t<decltype(inner)>;
									if constexpr (std::is_same_v<T, BoolLiteralNode>) {
										arg_type = Type::Bool;
									} else if constexpr (std::is_same_v<T, NumericLiteralNode>) {
										arg_type = inner.type();
									} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
										arg_type = Type::Char;  // const char*
									} else if constexpr (std::is_same_v<T, IdentifierNode>) {
										// Look up the identifier's type
										auto id_type = lookup_symbol(StringTable::getOrInternStringHandle(inner.name()));
										if (id_type.has_value()) {
											if (const DeclarationNode* decl = get_decl_from_symbol(*id_type)) {
												if (decl->type_node().template is<TypeSpecifierNode>()) {
													arg_type = decl->type_node().template as<TypeSpecifierNode>().type();
												}
											}
										}
									}
								}, expr);
								
								arg_types.emplace_back(arg_type, TypeQualifier::None, get_type_size_bits(arg_type), Token());
							}
						}

						if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
							advance(); // Consume comma
						}
						else if (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
							return ParseResult::error("Expected ',' or ')' after function argument", current_token_);
						}

						if (peek().is_eof())
							return ParseResult::error(ParserError::NotImplemented, Token());
					}

					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after function call arguments", current_token_);
					}

					// Try to instantiate the template function (skip in extern "C" contexts - C has no templates)
					if (current_linkage_ != Linkage::C) {
						template_func_inst = try_instantiate_template(idenfifier_token.value(), arg_types);
					}
					
					if (template_func_inst.has_value() && template_func_inst->is<FunctionDeclarationNode>()) {
						const auto& func = template_func_inst->as<FunctionDeclarationNode>();
						result = emplace_node<ExpressionNode>(
							FunctionCallNode(const_cast<DeclarationNode&>(func.decl_node()), std::move(args), idenfifier_token));
						return ParseResult::success(*result);
					} else {
						FLASH_LOG(Parser, Error, "Template instantiation failed or didn't return FunctionDeclarationNode");
						// Fall through to forward declaration
					}
				}
				
				// Not a template function, or instantiation failed
				// Create a forward declaration for the function (only if we haven't already found it)
				// Skip if we already found this as a member function in the class context
				if (!found_member_function_in_context && !identifierType.has_value()) {
					// We'll assume it returns int for now (this is a simplification)
					auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
					auto forward_decl = emplace_node<DeclarationNode>(type_node, idenfifier_token);

					// Add to GLOBAL symbol table as a forward declaration
					// Using insertGlobal ensures it persists after scope exits
					gSymbolTable.insertGlobal(idenfifier_token.value(), forward_decl);
					identifierType = forward_decl;
				}

				if (peek().is_eof())
					return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

				ChunkedVector<ASTNode> args;
				while (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
					ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (argResult.is_error()) {
						return argResult;
					}

					// Check for pack expansion: expr...
					if (peek() == "..."_tok) {
						advance(); // consume '...'
						
						// Pack expansion: need to expand the expression for each pack element
						if (auto arg_node = argResult.node()) {
							// Simple case: if the expression is just a single identifier that looks
							// like a pack parameter, try to expand it
							if (arg_node->is<IdentifierNode>()) {
								std::string_view pack_name = arg_node->as<IdentifierNode>().name();
								
								// Try to find pack_name_0, pack_name_1, etc. in the symbol table
								size_t pack_size = 0;
								
								StringBuilder sb;
								for (size_t i = 0; i < 100; ++i) {  // reasonable limit
									// Use StringBuilder to create a persistent string
									std::string_view element_name = sb
										.append(pack_name)
										.append("_")
										.append(i)
										.preview();
									
									if (gSymbolTable.lookup(element_name).has_value()) {
										++pack_size;
									} else {
										break;
									}

									sb.reset();
								}
								sb.reset();
								
								if (pack_size > 0) {
									// Add each pack element as a separate argument
									for (size_t i = 0; i < pack_size; ++i) {
										// Use StringBuilder to create a persistent string for the token
										std::string_view element_name = sb
											.append(pack_name)
											.append("_")
											.append(i)
											.commit();
										
										Token elem_token(Token::Type::Identifier, element_name, 0, 0, 0);
										auto elem_node = emplace_node<ExpressionNode>(IdentifierNode(elem_token));
										args.push_back(elem_node);
									}
								} else {
									if (auto node = argResult.node()) {
										args.push_back(*node);
									}
								}
							} else {
								// TODO Complex expression: need full rewriting (not implemented yet)
								FLASH_LOG(Parser, Error, "Complex pack expansion not yet implemented");
								if (auto node = argResult.node()) {
									args.push_back(*node);
								}
							}
						}
					} else {
						// Regular argument
						if (auto node = argResult.node()) {
							args.push_back(*node);
						}
					}

					if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == ",") {
						advance(); // Consume comma
					}
					else if (current_token_.type() != Token::Type::Punctuator || current_token_.value() != ")") {
						return ParseResult::error("Expected ',' or ')' after function argument", current_token_);
					}

					if (peek().is_eof())
						return ParseResult::error(ParserError::NotImplemented, Token());
				}

				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function call arguments", current_token_);
				}

				// Get the DeclarationNode (works for both DeclarationNode and FunctionDeclarationNode)
				const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
				if (!decl_ptr) {
					return ParseResult::error("Invalid function declaration", idenfifier_token);
				}

				// If we found this member function in the current class context (or base class),
				// create a MemberFunctionCallNode with implicit 'this' as the object
				if (found_member_function_in_context && identifierType->is<FunctionDeclarationNode>()) {
					// Create implicit 'this' expression
					Token this_token(Token::Type::Keyword, "this"sv, idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					auto this_node = emplace_node<ExpressionNode>(IdentifierNode(this_token));
					
					// Get the FunctionDeclarationNode
					FunctionDeclarationNode& func_decl = const_cast<FunctionDeclarationNode&>(identifierType->as<FunctionDeclarationNode>());
					
					// Create MemberFunctionCallNode with implicit 'this'
					result = emplace_node<ExpressionNode>(
						MemberFunctionCallNode(this_node, func_decl, std::move(args), idenfifier_token));
				} else {
					auto function_call_node = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
					// If the function has a pre-computed mangled name, set it on the FunctionCallNode
					if (identifierType->is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
						FLASH_LOG(Parser, Debug, "Function has mangled name: {}, name: {}", func_decl.has_mangled_name(), func_decl.mangled_name());
						if (func_decl.has_mangled_name()) {
							std::get<FunctionCallNode>(function_call_node.as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
							FLASH_LOG(Parser, Debug, "Set mangled name on FunctionCallNode: {}", func_decl.mangled_name());
						}
					}
					result = function_call_node;
				}
			}
			else {
				// Lambda variables should create an identifier node and return immediately
				// so postfix operator parsing can handle the operator() call
				if (is_lambda_variable) {
					result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
					return ParseResult::success(*result);
				}
				
				// Not a function call - could be a template with `<` or just missing identifier
				// Check if this might be a template: identifier<...>
				// BUT: Don't attempt for regular variables (< could be comparison)
				bool should_try_template = true;  // Default: try template parsing
				if (identifierType) {
					// Check if it's a regular variable
					bool is_regular_var = identifierType->is<VariableDeclarationNode>() || 
					                     identifierType->is<DeclarationNode>();
					should_try_template = !is_regular_var;  // Don't try for variables
				}
				// If identifierType is null (not found), default to true (might be a template)
				
				if (should_try_template && peek() == "<"_tok) {
					// Try to parse as template instantiation with member access
					auto explicit_template_args = parse_explicit_template_arguments();
					
					if (explicit_template_args.has_value()) {
						// Store parsed template args in member variable for cross-function access
						// ONLY if the next token is '(' (function call) or '::' (qualified name that might lead to function call)
						// For other cases (brace init, etc.), the args will be consumed locally
						if (!peek().is_eof() && (peek() == "("_tok || peek() == "::"_tok)) {
							pending_explicit_template_args_ = explicit_template_args;
						}
						
						// Successfully parsed template arguments
						// Now check for :: to handle Template<T>::member syntax
						if (peek() == "::"_tok) {
							// Instantiate the template to get the actual instantiated name
							std::string_view template_name = idenfifier_token.value();
							
							// Fill in default template arguments to get the actual instantiated name
							std::vector<TemplateTypeArg> filled_template_args = *explicit_template_args;
							auto template_lookup_result = gTemplateRegistry.lookupTemplate(template_name);
							if (template_lookup_result.has_value() && template_lookup_result->is<TemplateClassDeclarationNode>()) {
								const auto& template_class = template_lookup_result->as<TemplateClassDeclarationNode>();
								const auto& template_params = template_class.template_parameters();
								
								// Helper lambda to build instantiated template name suffix
								// Fill in defaults for missing parameters
								for (size_t param_idx = filled_template_args.size(); param_idx < template_params.size(); ++param_idx) {
									const TemplateParameterNode& param = template_params[param_idx].as<TemplateParameterNode>();
									if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
										const ASTNode& default_node = param.default_value();
										if (default_node.is<TypeSpecifierNode>()) {
											const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
											filled_template_args.push_back(TemplateTypeArg(default_type));
										}
									} else if (param.has_default() && param.kind() == TemplateParameterKind::NonType) {
										const ASTNode& default_node = param.default_value();
										if (default_node.is<ExpressionNode>()) {
											const ExpressionNode& expr_default = default_node.as<ExpressionNode>();
											
											if (std::holds_alternative<QualifiedIdentifierNode>(expr_default)) {
												const QualifiedIdentifierNode& qual_id_default = std::get<QualifiedIdentifierNode>(expr_default);
												
												if (!qual_id_default.namespace_handle().isGlobal()) {
													std::string_view type_name_sv = gNamespaceRegistry.getName(qual_id_default.namespace_handle());
													std::string_view member_name = qual_id_default.name();
													
													// Check for dependent placeholder using TypeInfo-based detection
													auto [is_dependent_placeholder, template_base_name] = isDependentTemplatePlaceholder(type_name_sv);
													
													if (is_dependent_placeholder && !filled_template_args.empty()) {
														// Build instantiated name using hash-based naming
														std::string_view inst_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
														
														try_instantiate_class_template(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
														
														auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
														if (type_it != gTypesByName.end()) {
															const TypeInfo* type_info = type_it->second;
															if (type_info->getStructInfo()) {
																const StructTypeInfo* struct_info = type_info->getStructInfo();
																for (const auto& static_member : struct_info->static_members) {
																	if (StringTable::getStringView(static_member.getName()) == member_name) {
																		if (static_member.initializer.has_value()) {
																			const ASTNode& init_node = *static_member.initializer;
																			if (init_node.is<ExpressionNode>()) {
																				const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
																				if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
																					bool val = std::get<BoolLiteralNode>(init_expr).value();
																					filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL, Type::Bool));
																				} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
																					const NumericLiteralNode& lit = std::get<NumericLiteralNode>(init_expr);
																					const auto& val = lit.value();
																					if (std::holds_alternative<unsigned long long>(val)) {
																						filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
																					}
																				}
																			}
																		}
																		break;
																	}
																}
															}
														}
													}
												}
											} else if (std::holds_alternative<NumericLiteralNode>(expr_default)) {
												const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr_default);
												const auto& val = lit.value();
												if (std::holds_alternative<unsigned long long>(val)) {
													filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
												} else if (std::holds_alternative<double>(val)) {
													filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<double>(val))));
												}
											} else if (std::holds_alternative<BoolLiteralNode>(expr_default)) {
												const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr_default);
												filled_template_args.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL, Type::Bool));
											}
										}
									}
								}
							}
							
							std::string_view instantiated_name = get_instantiated_class_name(template_name, filled_template_args);
							try_instantiate_class_template(template_name, filled_template_args);
							
							// Parse qualified identifier after template, using the instantiated name
							// We need to collect the :: path ourselves since we have the instantiated name
							std::vector<StringType<32>> namespaces;
							Token final_identifier = idenfifier_token;
							
							// Collect the qualified path after ::
							while (peek() == "::"_tok) {
								// Current identifier becomes a namespace part (but use instantiated name for first part)
								if (namespaces.empty()) {
									namespaces.emplace_back(StringType<32>(instantiated_name));
								} else {
									namespaces.emplace_back(StringType<32>(final_identifier.value()));
								}
								advance(); // consume ::
								
								// Handle ::template syntax for dependent names (e.g., __xref<T>::template __type)
								if (peek() == "template"_tok) {
									advance(); // consume 'template' keyword
								}
								
								// Get next identifier
								if (!peek().is_identifier()) {
									pending_explicit_template_args_.reset(); // Clear pending to avoid leaking to unrelated calls
									return ParseResult::error("Expected identifier after '::'", peek_info());
								}
								final_identifier = peek_info();
								advance(); // consume the identifier
							}
							
							// Try to parse member template function call: Template<T>::member<U>()
							auto func_call_result = try_parse_member_template_function_call(
								instantiated_name, final_identifier.value(), final_identifier);
							if (func_call_result.has_value()) {
								if (func_call_result->is_error()) {
									return *func_call_result;
								}
								result = *func_call_result->node();
								pending_explicit_template_args_.reset();
								return ParseResult::success(*result);
							}
							
							// Create a QualifiedIdentifierNode with the instantiated type name
							NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
							auto qualified_node_ast = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
							const auto& qualified_node = qualified_node_ast.as<QualifiedIdentifierNode>();
							result = emplace_node<ExpressionNode>(qualified_node);
							// Clear pending template args since they were used for this qualified identifier
							pending_explicit_template_args_.reset();
							return ParseResult::success(*result);
						}
						
						// Template arguments parsed but NOT followed by ::
						// Check for template class brace initialization: Template<T>{}
						// This creates a temporary object using value-initialization or aggregate-initialization
						if (!identifierType && peek() == "{"_tok) {
							// This is template class brace initialization (e.g., type_identity<int>{})
							// Check if any template arguments are dependent
							bool has_dependent_args = false;
							for (const auto& arg : *explicit_template_args) {
								if (arg.is_dependent || arg.is_pack) {
									has_dependent_args = true;
									break;
								}
							}
							
							auto class_template_opt = gTemplateRegistry.lookupTemplate(idenfifier_token.value());
							if (class_template_opt.has_value()) {
								FLASH_LOG(Parser, Debug, "Template brace initialization detected for '", idenfifier_token.value(), "', has_dependent_args=", has_dependent_args);
								
								if (has_dependent_args) {
									// Dependent template arguments - create a placeholder for now
									// The actual instantiation will happen when the outer template is instantiated
									advance(); // consume '{'
									
									// Skip the brace content - should be empty {} for value-initialization
									ChunkedVector<ASTNode> args;
									while (!peek().is_eof() && peek() != "}"_tok) {
										auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (argResult.is_error()) {
											return argResult;
										}
										if (auto node = argResult.node()) {
											args.push_back(*node);
										}
										
										if (peek() == ","_tok) {
											advance(); // consume ','
										} else if (peek() != "}"_tok) {
											return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
										}
									}
									
									if (!consume("}"_tok)) {
										return ParseResult::error("Expected '}' after brace initializer", current_token_);
									}
									
									// For dependent args, create a placeholder ConstructorCallNode
									// The actual type will be resolved during template instantiation
									// Use a placeholder type for now
									auto placeholder_type_node = emplace_node<TypeSpecifierNode>(Type::Auto, 0, 0, idenfifier_token);
									result = emplace_node<ExpressionNode>(ConstructorCallNode(placeholder_type_node, std::move(args), idenfifier_token));
									return ParseResult::success(*result);
								}
								
								// Non-dependent template arguments - instantiate the class template
								try_instantiate_class_template(idenfifier_token.value(), *explicit_template_args);
								
								// Build the instantiated type name to look up the type
								std::string_view instantiated_name = get_instantiated_class_name(idenfifier_token.value(), *explicit_template_args);
								
								// Look up the instantiated type
								auto type_handle = StringTable::getOrInternStringHandle(instantiated_name);
								auto type_it = gTypesByName.find(type_handle);
								
								// If not found, the type may have been registered with filled-in default template args
								// (e.g., basic_string_view<char> → basic_string_view<char, char_traits<char>>)
								// Check the V2 cache for the instantiated struct node to get the correct name
								if (type_it == gTypesByName.end()) {
									auto cached = gTemplateRegistry.getInstantiationV2(
										StringTable::getOrInternStringHandle(idenfifier_token.value()),
										*explicit_template_args);
									if (cached.has_value() && cached->is<StructDeclarationNode>()) {
										StringHandle cached_name = cached->as<StructDeclarationNode>().name();
										auto cached_it = gTypesByName.find(cached_name);
										if (cached_it != gTypesByName.end()) {
											type_handle = cached_name;
											type_it = cached_it;
										}
									}
								}
								
								if (type_it != gTypesByName.end()) {
									// Found the instantiated type - now parse the brace initializer
									advance(); // consume '{'
									
									ChunkedVector<ASTNode> args;
									while (!peek().is_eof() && peek() != "}"_tok) {
										auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (argResult.is_error()) {
											return argResult;
										}
										if (auto node = argResult.node()) {
											args.push_back(*node);
										}
										
										if (peek() == ","_tok) {
											advance(); // consume ','
										} else if (peek() != "}"_tok) {
											return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
										}
									}
									
									if (!consume("}"_tok)) {
										return ParseResult::error("Expected '}' after brace initializer", current_token_);
									}
									
									// Create TypeSpecifierNode for the instantiated class
									const TypeInfo& type_info = *type_it->second;
									TypeIndex type_index = type_info.type_index_;
									int type_size = 0;
									if (type_info.struct_info_) {
										type_size = static_cast<int>(type_info.struct_info_->total_size * 8);
									}
									auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, idenfifier_token);
									
									// Create ConstructorCallNode
									result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
									return ParseResult::success(*result);
								}
							}
						}
						
						// Handle functional-style cast for class templates: Template<Args>()
						// This creates a temporary object of the instantiated class type
						// Pattern: hash<_Tp>() creates a temporary hash<_Tp> object
						if (!identifierType && peek() == "("_tok) {
							auto class_template_opt = gTemplateRegistry.lookupTemplate(idenfifier_token.value());
							if (class_template_opt.has_value() && class_template_opt->is<TemplateClassDeclarationNode>()) {
								FLASH_LOG_FORMAT(Parser, Debug, "Functional-style cast for class template '{}' with template args", idenfifier_token.value());
								
								// Build the instantiated type name using hash-based naming
								std::string_view instantiated_type_name = get_instantiated_class_name(idenfifier_token.value(), *explicit_template_args);
								
								// Try to instantiate the class template (may fail for dependent args, which is OK)
								try_instantiate_class_template(idenfifier_token.value(), *explicit_template_args);
								
								// Consume '(' and parse constructor arguments
								advance(); // consume '('
								
								// Parse constructor arguments
								ChunkedVector<ASTNode> args;
								if (current_token_.value() != ")") {
									while (true) {
										auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (arg_result.is_error()) {
											return arg_result;
										}
										if (auto arg = arg_result.node()) {
											args.push_back(*arg);
										}
										
										if (current_token_.kind().is_eof() || current_token_.value() != ",") {
											break;
										}
										advance(); // consume ','
									}
								}
								
								if (!consume(")"_tok)) {
									return ParseResult::error("Expected ')' after constructor arguments", current_token_);
								}
								
								// Create TypeSpecifierNode for the instantiated template type
								Token inst_type_token(Token::Type::Identifier, instantiated_type_name,
								                      idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
								auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, inst_type_token);
								
								// Create ConstructorCallNode for functional-style cast
								result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), inst_type_token));
								return ParseResult::success(*result);
							}
						}
						
						// Check if this is a template alias - if so, treat as valid dependent expression
						// This handles patterns like: __enable_if_t<...> in template argument contexts
						if (!identifierType) {
							auto alias_opt = gTemplateRegistry.lookup_alias_template(idenfifier_token.value());
							
							// If not found directly, try looking up as a member alias template of the enclosing class
							if (!alias_opt.has_value() && !struct_parsing_context_stack_.empty()) {
								const auto& sp_ctx = struct_parsing_context_stack_.back();
								StringBuilder qualified_alias_name;
								qualified_alias_name.append(sp_ctx.struct_name).append("::"sv).append(idenfifier_token.value());
								std::string_view qualified_alias_name_sv = qualified_alias_name.commit();
								alias_opt = gTemplateRegistry.lookup_alias_template(qualified_alias_name_sv);
								if (alias_opt.has_value()) {
									FLASH_LOG_FORMAT(Parser, Debug, "Found member template alias '{}' as '{}'", idenfifier_token.value(), qualified_alias_name_sv);
								}
							}
							
							if (alias_opt.has_value()) {
								FLASH_LOG_FORMAT(Parser, Debug, "Found template alias '{}' with template arguments (no ::)", idenfifier_token.value());
								// For template aliases used in expression/template contexts, create a simple identifier
								// The template instantiation will be handled during type resolution
								result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
								return ParseResult::success(*result);
							}
							
							// Check if this is a variable template (e.g., is_reference_v<T>)
							auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(idenfifier_token.value());
							
							// If not found directly, try namespace-qualified lookup
							if (!var_template_opt.has_value()) {
								NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
								if (!current_namespace.isGlobal()) {
									StringHandle name_handle = idenfifier_token.handle();
									StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, name_handle);
									std::string_view qualified_name = StringTable::getStringView(qualified_handle);
									var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
									if (var_template_opt.has_value()) {
										FLASH_LOG_FORMAT(Parser, Debug, "Found variable template '{}' as '{}'", idenfifier_token.value(), qualified_name);
										// Use the qualified name for instantiation
										auto instantiated_var = try_instantiate_variable_template(qualified_name, *explicit_template_args);
										if (instantiated_var.has_value()) {
											std::string_view inst_name;
											if (instantiated_var->is<VariableDeclarationNode>()) {
												const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
												const auto& decl = var_decl.declaration();
												inst_name = decl.identifier_token().value();
											} else if (instantiated_var->is<DeclarationNode>()) {
												const auto& decl = instantiated_var->as<DeclarationNode>();
												inst_name = decl.identifier_token().value();
											} else {
												inst_name = idenfifier_token.value();
											}
											Token inst_token(Token::Type::Identifier, inst_name, 
											                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
											result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
											return ParseResult::success(*result);
										} else {
											// Variable template found but couldn't instantiate (likely dependent args)
											// Create a placeholder identifier node
											FLASH_LOG_FORMAT(Parser, Debug, "Variable template '{}' (qualified as '{}') found but not instantiated (dependent args)", idenfifier_token.value(), qualified_name);
											result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
											return ParseResult::success(*result);
										}
									}
								}
							}
							
							if (var_template_opt.has_value()) {
								FLASH_LOG_FORMAT(Parser, Debug, "Found variable template '{}' with template arguments (no ::)", idenfifier_token.value());
								auto instantiated_var = try_instantiate_variable_template(idenfifier_token.value(), *explicit_template_args);
								if (instantiated_var.has_value()) {
									std::string_view inst_name;
									if (instantiated_var->is<VariableDeclarationNode>()) {
										const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
										const auto& decl = var_decl.declaration();
										inst_name = decl.identifier_token().value();
									} else if (instantiated_var->is<DeclarationNode>()) {
										const auto& decl = instantiated_var->as<DeclarationNode>();
										inst_name = decl.identifier_token().value();
									} else {
										inst_name = idenfifier_token.value();
									}
									Token inst_token(Token::Type::Identifier, inst_name, 
									                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
									result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
									return ParseResult::success(*result);
								} else {
									// Variable template found but couldn't instantiate (likely dependent args)
									// Create a placeholder identifier node - will be resolved during actual template instantiation
									FLASH_LOG_FORMAT(Parser, Debug, "Variable template '{}' found but not instantiated (dependent args)", idenfifier_token.value());
									result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
									return ParseResult::success(*result);
								}
							}
							
							// Check if this is a concept application (e.g., default_constructible<HasDefault>)
							// Concepts evaluate to boolean values at compile time
							auto concept_opt = gConceptRegistry.lookupConcept(idenfifier_token.value());
							if (concept_opt.has_value() && explicit_template_args.has_value()) {
								// Check if any template arguments are dependent (referencing template parameters)
								// If so, we can't evaluate the concept yet - defer to instantiation time
								bool has_dependent_args = false;
								for (const auto& arg : *explicit_template_args) {
									if (arg.is_dependent) {
										has_dependent_args = true;
										break;
									}
								}
								
								if (has_dependent_args) {
									// Defer evaluation - create a FunctionCallNode to preserve the concept application
									FLASH_LOG_FORMAT(Parser, Debug, "Found concept '{}' with DEPENDENT template arguments - deferring evaluation", idenfifier_token.value());
									
									// Create a FunctionCallNode that will be evaluated during instantiation
									// The concept name is stored in the token, template args are already parsed
									Token concept_token = idenfifier_token;
									
									// Create a dummy declaration for the concept call
									Token void_token(Token::Type::Keyword, "void"sv, concept_token.line(), concept_token.column(), concept_token.file_index());
									auto void_type = emplace_node<TypeSpecifierNode>(
										Type::Void, 0, 0, void_token, CVQualifier::None);
									auto concept_decl = emplace_node<DeclarationNode>(void_type, concept_token);
									
									auto [func_call_node, func_call_ref] = emplace_node_ref<FunctionCallNode>(
										concept_decl.as<DeclarationNode>(), 
										ChunkedVector<ASTNode>(),
										concept_token);
									
									// Store the template arguments for later evaluation
									std::vector<ASTNode> template_arg_nodes;
									for (const auto& arg : *explicit_template_args) {
										// Convert TemplateTypeArg to an appropriate expression node
										if (arg.is_dependent && arg.dependent_name.isValid()) {
											Token dep_token(Token::Type::Identifier, arg.dependent_name.view(),
											               concept_token.line(), concept_token.column(), concept_token.file_index());
											auto dep_node = emplace_node<ExpressionNode>(IdentifierNode(dep_token));
											template_arg_nodes.push_back(dep_node);
										} else if (arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
											std::string_view type_name = StringTable::getStringView(gTypeInfo[arg.type_index].name_);
											Token type_token(Token::Type::Identifier, type_name,
											                concept_token.line(), concept_token.column(), concept_token.file_index());
											auto type_node = emplace_node<ExpressionNode>(IdentifierNode(type_token));
											template_arg_nodes.push_back(type_node);
										}
									}
									func_call_ref.set_template_arguments(std::move(template_arg_nodes));
									
									result = emplace_node<ExpressionNode>(func_call_node.as<FunctionCallNode>());
									return ParseResult::success(*result);
								}
								
								FLASH_LOG_FORMAT(Parser, Debug, "Found concept '{}' with concrete template arguments", idenfifier_token.value());
								
								// Evaluate the concept constraint with the provided template arguments
								auto constraint_result = evaluateConstraint(
									concept_opt->as<ConceptDeclarationNode>().constraint_expr(),
									*explicit_template_args,
									{}  // No template param names needed for concrete types
								);
								
								// Create a BoolLiteralNode with the result
								bool concept_satisfied = constraint_result.satisfied;
								Token bool_token(Token::Type::Keyword, concept_satisfied ? "true"sv : "false"sv,
								                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
								result = emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, concept_satisfied));
								return ParseResult::success(*result);
							}
							
							// Check for member template function (including current struct and inherited from base classes)
							// Example: __helper<_Tp>({}) where __helper is in the same struct or base class
							// Template args already parsed at this point
							if (!struct_parsing_context_stack_.empty() && peek() == "("_tok) {
								const auto& sp_ctx2 = struct_parsing_context_stack_.back();
								const StructDeclarationNode* struct_node = sp_ctx2.struct_node;
								if (struct_node) {
									StringHandle id_handle = idenfifier_token.handle();
									
									// First, check the current struct's member functions (including those parsed so far)
									for (const auto& member_func_decl : struct_node->member_functions()) {
										const ASTNode& func_node = member_func_decl.function_declaration;
										// Check if this is a template function
										if (func_node.is<TemplateFunctionDeclarationNode>()) {
											const TemplateFunctionDeclarationNode& template_func = func_node.as<TemplateFunctionDeclarationNode>();
											const FunctionDeclarationNode& func_decl = template_func.function_declaration().as<FunctionDeclarationNode>();
											StringHandle func_name = func_decl.decl_node().identifier_token().handle();
											if (func_name == id_handle) {
												FLASH_LOG(Parser, Debug, "Found member template function '", idenfifier_token.value(), "' in current struct");
												gSymbolTable.insert(idenfifier_token.value(), func_node);
												identifierType = func_node;
												goto inherited_template_found;
											}
										}
									}
									
									// If not found in current struct, check base classes
									for (const auto& base : struct_node->base_classes()) {
										auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base.name));
										if (base_type_it != gTypesByName.end()) {
											const TypeInfo* base_type_info = base_type_it->second;
											const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();
											if (base_struct_info) {
												for (const auto& member_func : base_struct_info->member_functions) {
													if (member_func.getName() == id_handle) {
														if (member_func.function_decl.is<TemplateFunctionDeclarationNode>()) {
															FLASH_LOG(Parser, Debug, "Found inherited member template function '", idenfifier_token.value(), "' in base class '", base.name, "'");
															gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
															identifierType = member_func.function_decl;
															// Don't return - let normal function call parsing proceed
															// The template args are already parsed, and we need to parse the function call
															goto inherited_template_found;
														}
													}
												}
											}
										}
									}
								}
							}
						}
					inherited_template_found:;
					}
				}
			
				// Check if we're parsing a template and this identifier is a template parameter
				if (!identifierType && (parsing_template_class_ || !current_template_param_names_.empty())) {
					// Check if this identifier matches any template parameter name
					for (const auto& param_name : current_template_param_names_) {
						if (param_name == idenfifier_token.value()) {
							// This is a template parameter reference
							// Check if we have a substitution value (for deferred template body parsing)
							bool substituted = false;
							for (const auto& subst : template_param_substitutions_) {
								if (subst.param_name == param_name && subst.is_value_param) {
									// Substitute with actual value - return immediately
									// Use StringBuilder.append(int64_t) to persist the string value (avoids temporary strings)
									StringBuilder value_str;
									value_str.append(subst.value);  // Directly append int64_t without std::to_string()
									std::string_view value_view = value_str.commit();
									Token num_token(Token::Type::Literal, value_view, 
									                idenfifier_token.line(), idenfifier_token.column(), 
									                idenfifier_token.file_index());
									result = emplace_node<ExpressionNode>(
										NumericLiteralNode(num_token, 
										                   static_cast<unsigned long long>(subst.value), 
										                   subst.value_type, 
										                   TypeQualifier::None, 
										                   get_type_size_bits(subst.value_type))
									);
									FLASH_LOG(Templates, Debug, "Substituted template parameter '", param_name, 
									          "' with value ", subst.value);
									// Return the substituted value immediately
									return ParseResult::success(*result);
								}
							}
							
							if (!substituted) {
								// No substitution - create TemplateParameterReferenceNode as before
								// Don't return yet - we need to check if this is a constructor call T(...)
								result = emplace_node<ExpressionNode>(TemplateParameterReferenceNode(param_name, idenfifier_token));
								// Set identifierType so the constructor call logic below can detect it
								identifierType = result;
							}
							break;
						}
					}
				}
				
				// Check if this identifier is a concept name
				// Concepts are used in requires clauses: requires Concept<T>
				if (!identifierType && gConceptRegistry.hasConcept(idenfifier_token.value())) {
					// Try to parse template arguments: Concept<T>
					if (peek() == "<"_tok) {
						auto template_args = parse_explicit_template_arguments();
						if (template_args.has_value()) {
							// Create a concept check expression
							// We'll represent this as an identifier with the concept name and args attached
							// The constraint evaluator will handle the actual check
							// For now, just wrap it in an identifier node
							result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
							return ParseResult::success(*result);
						}
					}
					// Concept without template args - just an identifier reference
					result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
					return ParseResult::success(*result);
				}

				// Not a function call, template member access, or template parameter reference
				// But allow pack expansion (identifier...)
				if (!identifierType && is_pack_expansion) {
					// Create a simple identifier node - the pack expansion will be handled by the caller
					result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
					return ParseResult::success(*result);
				}
				
				// Before reporting error, check if this could be a template alias or class template usage
				// Example: remove_const_t<T> where remove_const_t is defined as "using remove_const_t = typename remove_const<T>::type;"
				// Or: type_identity<T>{} for template class brace initialization
				if (!identifierType && peek() == "<"_tok) {
					// Check if this is an alias template
					auto alias_opt = gTemplateRegistry.lookup_alias_template(idenfifier_token.value());
					
					// If not found directly, try looking up as a member alias template of the enclosing class
					// This handles patterns like: template<typename T, typename U> using cond_t = decltype(...);
					// used within the same struct as: decltype(cond_t<T, U>())
					if (!alias_opt.has_value() && !struct_parsing_context_stack_.empty()) {
						const auto& sp_ctx3 = struct_parsing_context_stack_.back();
						// Build qualified name: EnclosingClass::MemberAliasTemplate
						StringBuilder qualified_alias_name;
						qualified_alias_name.append(sp_ctx3.struct_name).append("::"sv).append(idenfifier_token.value());
						std::string_view qualified_alias_name_sv = qualified_alias_name.commit();
						alias_opt = gTemplateRegistry.lookup_alias_template(qualified_alias_name_sv);
						if (alias_opt.has_value()) {
							FLASH_LOG(Parser, Debug, "Found member alias template '", idenfifier_token.value(), "' as '", qualified_alias_name_sv, "'");
						}
					}
					
					if (alias_opt.has_value()) {
						// This is an alias template like "remove_const_t<T>"
						// We need to instantiate it, which will happen in the normal template arg parsing flow below
						// Set a marker that we found an alias template so we can handle it later
						// For now, create a placeholder node and let the template instantiation logic handle it
						FLASH_LOG(Parser, Debug, "Found alias template '", idenfifier_token.value(), "' in expression context");
						// Don't return yet - let it fall through to template argument parsing below
					} else {
						// Check if this is a class template (for expressions like type_identity<T>{})
						auto class_template_opt = gTemplateRegistry.lookupTemplate(idenfifier_token.value());
						FLASH_LOG(Parser, Debug, "Looking up class template '", idenfifier_token.value(), "', found=", class_template_opt.has_value());
						
						// If not found directly, try looking up as a member struct template of the enclosing class
						// This handles patterns like: template<typename T> struct Select<Wrapper<T>> { };
						// where Wrapper is a member struct template of the same class
						if (!class_template_opt.has_value() && !struct_parsing_context_stack_.empty()) {
							const auto& sp_ctx4 = struct_parsing_context_stack_.back();
							// Build qualified name: EnclosingClass::MemberTemplate
							StringBuilder qualified_name;
							qualified_name.append(sp_ctx4.struct_name).append("::"sv).append(idenfifier_token.value());
							std::string_view qualified_name_sv = qualified_name.commit();
							class_template_opt = gTemplateRegistry.lookupTemplate(qualified_name_sv);
							if (class_template_opt.has_value()) {
								FLASH_LOG(Parser, Debug, "Found member struct template '", idenfifier_token.value(), "' as '", qualified_name_sv, "'");
							}
						}
						
						if (class_template_opt.has_value()) {
							FLASH_LOG(Parser, Debug, "Found class template '", idenfifier_token.value(), "' in expression context");
							// Mark as found to prevent "Missing identifier" error
							found_as_type_alias = true;  // Reuse this flag - class template acts like a type name
							// Don't return - let it fall through to template argument parsing below
						} else {
							// Check if this is a variable template (e.g., is_reference_v<T>)
							auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(idenfifier_token.value());
							
							// If not found directly, try namespace-qualified lookup
							if (!var_template_opt.has_value()) {
								NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
								if (!current_namespace.isGlobal()) {
									StringHandle name_handle = idenfifier_token.handle();
									StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, name_handle);
									std::string_view qualified_name = StringTable::getStringView(qualified_handle);
									var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
									if (var_template_opt.has_value()) {
										FLASH_LOG(Parser, Debug, "Found variable template '", idenfifier_token.value(), "' as '", qualified_name, "'");
									}
								}
							}
							
							if (var_template_opt.has_value()) {
								FLASH_LOG(Parser, Debug, "Found variable template '", idenfifier_token.value(), "' in expression context");
								// Don't return - let it fall through to template argument parsing below
							} else if (!found_as_type_alias) {
								// Check if this is an inherited member template function (e.g., __test<_Tp>(0) from <type_traits>)
								// This pattern is used for SFINAE detection where a derived class calls a base class template function
								bool found_inherited_template = false;
								
								// First try member_function_context_stack_ (for code inside member function bodies)
								if (!member_function_context_stack_.empty()) {
									const auto& mf_ctx2 = member_function_context_stack_.back();
									TypeIndex struct_type_index = mf_ctx2.struct_type_index;
									if (struct_type_index < gTypeInfo.size()) {
										const TypeInfo& type_info = gTypeInfo[struct_type_index];
										const StructTypeInfo* struct_info = type_info.getStructInfo();
										if (struct_info) {
											// Search through base classes for member template functions
											std::vector<TypeIndex> base_classes_to_search;
											for (const auto& base : struct_info->base_classes) {
												base_classes_to_search.push_back(base.type_index);
											}
											
											StringHandle id_handle = idenfifier_token.handle();
											for (size_t i = 0; i < base_classes_to_search.size() && !found_inherited_template; ++i) {
												TypeIndex base_idx = base_classes_to_search[i];
												if (base_idx >= gTypeInfo.size()) continue;
												
												const TypeInfo& base_type_info = gTypeInfo[base_idx];
												const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
												if (!base_struct_info) continue;
												
												// Check member functions in this base class for template functions
												for (const auto& member_func : base_struct_info->member_functions) {
													if (member_func.getName() == id_handle) {
														// Found a match - check if it's a template function
														if (member_func.function_decl.is<TemplateFunctionDeclarationNode>()) {
															FLASH_LOG(Parser, Debug, "Found inherited member template function '", idenfifier_token.value(), "' in base class (member function context)");
															// Add to symbol table and set identifierType
															gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
															identifierType = member_func.function_decl;
															found_inherited_template = true;
															break;
														}
													}
												}
												
												// Add this base's base classes to search list (for multi-level inheritance)
												for (const auto& nested_base : base_struct_info->base_classes) {
													bool already_in_list = false;
													for (TypeIndex existing : base_classes_to_search) {
														if (existing == nested_base.type_index) {
															already_in_list = true;
															break;
														}
													}
													if (!already_in_list) {
														base_classes_to_search.push_back(nested_base.type_index);
													}
												}
											}
										}
									}
								}
								
								// If not found in member function context, try struct_parsing_context_stack_
								// This handles expressions in type aliases like: using type = decltype(__test<_Tp>(0));
								if (!found_inherited_template && !struct_parsing_context_stack_.empty()) {
									const auto& sp_ctx5 = struct_parsing_context_stack_.back();
									const StructDeclarationNode* struct_node = sp_ctx5.struct_node;
									if (struct_node) {
										// Get base classes from the struct AST node
										StringHandle id_handle = idenfifier_token.handle();
										for (const auto& base : struct_node->base_classes()) {
											// Look up the base class type
											auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base.name));
											if (base_type_it != gTypesByName.end()) {
												const TypeInfo* base_type_info = base_type_it->second;
												const StructTypeInfo* base_struct_info = base_type_info->getStructInfo();
												if (base_struct_info) {
													// Check member functions for template functions
													for (const auto& member_func : base_struct_info->member_functions) {
														if (member_func.getName() == id_handle) {
															// Found a match - check if it's a template function
															if (member_func.function_decl.is<TemplateFunctionDeclarationNode>()) {
																FLASH_LOG(Parser, Debug, "Found inherited member template function '", idenfifier_token.value(), "' in base class (struct parsing context)");
																// Add to symbol table and set identifierType
																gSymbolTable.insert(idenfifier_token.value(), member_func.function_decl);
																identifierType = member_func.function_decl;
																found_inherited_template = true;
																break;
															}
														}
													}
													if (found_inherited_template) break;
												}
											}
										}
									}
								}
								
								if (!found_inherited_template) {
									// Not an alias template, class template, variable template, inherited member template, or found anywhere
									FLASH_LOG(Parser, Error, "Missing identifier: ", idenfifier_token.value());
									return ParseResult::error("Missing identifier", idenfifier_token);
								}
							}
						}
					}
				} else if (!identifierType && !found_as_type_alias) {
					// Not a function call, template member access, template parameter reference, pack expansion, or alias template
					// In template context, treat unknown identifiers as potentially member references that will resolve at instantiation time
					if (parsing_template_body_ || !current_template_param_names_.empty() || !struct_parsing_context_stack_.empty()) {
						FLASH_LOG(Parser, Debug, "Treating unknown identifier '", idenfifier_token.value(), "' as dependent in template context");
						result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
						// Don't return error - let it continue as a dependent expression
					} else {
						FLASH_LOG(Parser, Error, "Missing identifier: ", idenfifier_token.value());
						return ParseResult::error("Missing identifier", idenfifier_token);
					}
				}
			}
		}
		if (identifierType && (!identifierType->is<DeclarationNode>() && 
					!identifierType->is<FunctionDeclarationNode>() && 
					!identifierType->is<VariableDeclarationNode>() &&
					!identifierType->is<TemplateFunctionDeclarationNode>() &&
					!identifierType->is<TemplateVariableDeclarationNode>() &&
					!identifierType->is<TemplateParameterReferenceNode>())) {
			FLASH_LOG(Parser, Error, "Identifier type check failed, type_name=", identifierType->type_name());
			return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, current_token_);
		}
		else {
			// Identifier already consumed at line 1621

			// Check for explicit template arguments: identifier<type1, type2>(args)
			// BUT: Don't attempt template argument parsing for regular variables (could be < comparison)
			std::optional<std::vector<TemplateTypeArg>> explicit_template_args;
			std::vector<ASTNode> explicit_template_arg_nodes;  // Store AST nodes for template arguments
			bool should_try_template_args = true;  // Default: try template parsing
			
			// Only skip template argument parsing if we KNOW it's a regular variable
			if (identifierType) {
				// Check if it's a regular variable
				bool is_regular_var = identifierType->is<VariableDeclarationNode>() || 
				                     identifierType->is<DeclarationNode>();
				
				if (is_regular_var) {
					// It's definitely a variable, don't try template args
					should_try_template_args = false;
				}
				// For all other cases (templates, functions, unknown), try template args
			}
			// If identifierType is null (not found), default to true (might be a template)
			
			if (should_try_template_args && peek() == "<"_tok) {
				explicit_template_args = parse_explicit_template_arguments(&explicit_template_arg_nodes);
				// If parsing failed, it might be a less-than operator, so continue normally
				
				// After template arguments, check for :: to handle Template<T>::member syntax
				if (explicit_template_args.has_value() && peek() == "::"_tok) {
					// Instantiate the template to ensure defaults are filled in
					// This returns the instantiated struct node
					auto instantiation_result = try_instantiate_class_template(idenfifier_token.value(), *explicit_template_args);
					
					// Get the instantiated class name with defaults filled in
					std::string_view instantiated_class_name;
					if (instantiation_result.has_value() && instantiation_result->is<StructDeclarationNode>()) {
						// Get the name from the instantiated struct
						const StructDeclarationNode& inst_struct = instantiation_result->as<StructDeclarationNode>();
						instantiated_class_name = StringTable::getStringView(inst_struct.name());
					} else {
						// Fallback: build name from explicit args (may be missing defaults)
						instantiated_class_name = get_instantiated_class_name(idenfifier_token.value(), *explicit_template_args);
					}
					
					// Create a token with the instantiated name to pass to parse_qualified_identifier_after_template
					Token instantiated_token(Token::Type::Identifier, instantiated_class_name,
					                        idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					
					// Parse qualified identifier after template
					auto qualified_result = parse_qualified_identifier_after_template(instantiated_token);
					if (!qualified_result.is_error() && qualified_result.node().has_value()) {
						auto qualified_node = qualified_result.node()->as<QualifiedIdentifierNode>();
						
						// Try to parse member template function call: Template<T>::member<U>()
						auto func_call_result = try_parse_member_template_function_call(
							instantiated_class_name, qualified_node.name(), qualified_node.identifier_token());
						if (func_call_result.has_value()) {
							if (func_call_result->is_error()) {
								return *func_call_result;
							}
							result = *func_call_result->node();
							return ParseResult::success(*result);
						}
						
						// Not a function call - return as qualified identifier
						result = emplace_node<ExpressionNode>(qualified_node);
						return ParseResult::success(*result);
					}
				}
				
				// Check if this is a variable template usage (identifier<args> without following '(')
				if (explicit_template_args.has_value() && 
				    (peek() != "("_tok)) {
					// Try to instantiate as variable template
					// First try unqualified name
					auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(idenfifier_token.value());
					std::string_view template_name_to_use = idenfifier_token.value();
					
					// If not found, try namespace-qualified lookup
					if (!var_template_opt.has_value()) {
						NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
						if (!current_namespace.isGlobal()) {
							StringHandle name_handle = idenfifier_token.handle();
							StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, name_handle);
							std::string_view qualified_name = StringTable::getStringView(qualified_handle);
							var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_name);
							if (var_template_opt.has_value()) {
								template_name_to_use = qualified_name;
								FLASH_LOG_FORMAT(Templates, Debug, "Found variable template with namespace-qualified name: {}", qualified_name);
							}
						}
					}
					
					if (var_template_opt.has_value()) {
						auto instantiated_var = try_instantiate_variable_template(template_name_to_use, *explicit_template_args);
						if (instantiated_var.has_value()) {
							// Could be VariableDeclarationNode (first instantiation) or DeclarationNode (already instantiated)
							std::string_view inst_name;
							if (instantiated_var->is<VariableDeclarationNode>()) {
								const auto& var_decl = instantiated_var->as<VariableDeclarationNode>();
								const auto& decl = var_decl.declaration();
								inst_name = decl.identifier_token().value();
							} else if (instantiated_var->is<DeclarationNode>()) {
								const auto& decl = instantiated_var->as<DeclarationNode>();
								inst_name = decl.identifier_token().value();
							} else {
								inst_name = idenfifier_token.value();  // Fallback
							}
							
							// Return identifier reference to the instantiated variable
							Token inst_token(Token::Type::Identifier, inst_name, 
							                idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
							result = emplace_node<ExpressionNode>(IdentifierNode(inst_token));
							return ParseResult::success(*result);
						}
					}
				}
			}
			
			// Handle functional-style cast for class templates: ClassName<Args>()
			// This creates a temporary object of the instantiated class type
			// Pattern: hash<_Tp>() creates a temporary hash<_Tp> object
			if (explicit_template_args.has_value() && peek() == "("_tok) {
				// Check if this is a class template
				auto class_template_opt = gTemplateRegistry.lookupTemplate(idenfifier_token.value());
				if (class_template_opt.has_value() && class_template_opt->is<TemplateClassDeclarationNode>()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Functional-style cast for class template '{}' with template args", idenfifier_token.value());
					
					// Build the instantiated type name using hash-based naming
					std::string_view instantiated_type_name = get_instantiated_class_name(idenfifier_token.value(), *explicit_template_args);
					
					// Try to instantiate the class template
					try_instantiate_class_template(idenfifier_token.value(), *explicit_template_args);
					
					// Consume '(' and parse constructor arguments
					advance(); // consume '('
					
					// Parse constructor arguments
					ChunkedVector<ASTNode> args;
					if (current_token_.value() != ")") {
						while (true) {
							auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (arg_result.is_error()) {
								return arg_result;
							}
							if (auto arg = arg_result.node()) {
								args.push_back(*arg);
							}
							
							if (current_token_.kind().is_eof() || current_token_.value() != ",") {
								break;
							}
							advance(); // consume ','
						}
					}
					
					if (!consume(")"_tok)) {
						return ParseResult::error("Expected ')' after constructor arguments", current_token_);
					}
					
					// Create TypeSpecifierNode for the instantiated template type
					Token inst_type_token(Token::Type::Identifier, instantiated_type_name,
					                      idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, inst_type_token);
					
					// Create ConstructorCallNode for functional-style cast
					result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), inst_type_token));
					return ParseResult::success(*result);
				}
			}

			// Handle brace initialization for type names: TypeName{} or TypeName{args}
			// This handles expressions like "throw bad_any_cast{}" where bad_any_cast is a class
			if (found_as_type_alias && !identifierType && peek() == "{"_tok) {
				// Look up the actual type info to determine if this is an aggregate
				StringHandle identifier_handle = idenfifier_token.handle();
				auto type_it = gTypesByName.find(identifier_handle);
				if (type_it == gTypesByName.end()) {
					// Try namespace-qualified lookup
					NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
					if (!current_namespace.isGlobal()) {
						StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, identifier_handle);
						type_it = gTypesByName.find(qualified_handle);
					}
				}

				if (type_it != gTypesByName.end()) {
					const TypeInfo* type_info_ptr = type_it->second;
					const StructTypeInfo* struct_info = type_info_ptr->getStructInfo();
					TypeIndex type_index = type_info_ptr->type_index_;
					
					// Check if this is an aggregate type (no user-declared constructors, all public, no vtable)
					bool is_aggregate = false;
					if (struct_info) {
						bool has_user_ctors = false;
						for (const auto& func : struct_info->member_functions) {
							if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
								if (!func.function_decl.as<ConstructorDeclarationNode>().is_implicit()) {
									has_user_ctors = true;
									break;
								}
							}
						}
						bool all_public = true;
						for (const auto& member : struct_info->members) {
							if (member.access == AccessSpecifier::Private || member.access == AccessSpecifier::Protected) {
								all_public = false;
								break;
							}
						}
						is_aggregate = !has_user_ctors && !struct_info->has_vtable && all_public && !struct_info->members.empty();
					}
					
					if (is_aggregate) {
						// For aggregates, use parse_brace_initializer which creates proper InitializerListNode
						unsigned char type_size = struct_info ? static_cast<unsigned char>(struct_info->total_size * 8) : 0;
						auto type_spec = TypeSpecifierNode(Type::Struct, type_index, type_size, idenfifier_token);
						ParseResult init_result = parse_brace_initializer(type_spec);
						if (init_result.is_error()) {
							return init_result;
						}
						// Wrap the result in a ConstructorCallNode so codegen knows the target type
						if (init_result.node().has_value() && init_result.node()->is<InitializerListNode>()) {
							auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, idenfifier_token);
							// Convert InitializerListNode initializers to ConstructorCallNode args
							const InitializerListNode& init_list = init_result.node()->as<InitializerListNode>();
							ChunkedVector<ASTNode> args;
							for (const auto& init : init_list.initializers()) {
								args.push_back(init);
							}
							result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
							return ParseResult::success(*result);
						}
						return init_result;
					} else {
						// Non-aggregate: use constructor call with proper type info
						advance(); // consume '{'
						
						ChunkedVector<ASTNode> args;
						while (current_token_.value() != "}") {
							auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (arg_result.is_error()) {
								return arg_result;
							}
							if (auto arg = arg_result.node()) {
								args.push_back(*arg);
							}
							
							if (current_token_.value() == ",") {
								advance(); // consume ','
							} else if (current_token_.kind().is_eof() || current_token_.value() != "}") {
								return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
							}
						}
						
						if (!consume("}"_tok)) {
							return ParseResult::error("Expected '}' after brace initializer", current_token_);
						}
						
						unsigned char type_size = struct_info ? static_cast<unsigned char>(struct_info->total_size * 8) : 0;
						auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, idenfifier_token);
						result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
						return ParseResult::success(*result);
					}
				} else {
					// Type not found - fall back to generic constructor call
					advance(); // consume '{'
					
					ChunkedVector<ASTNode> args;
					while (current_token_.value() != "}") {
						auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (arg_result.is_error()) {
							return arg_result;
						}
						if (auto arg = arg_result.node()) {
							args.push_back(*arg);
						}
						
						if (current_token_.value() == ",") {
							advance(); // consume ','
						} else if (current_token_.kind().is_eof() || current_token_.value() != "}") {
							return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
						}
					}
					
					if (!consume("}"_tok)) {
						return ParseResult::error("Expected '}' after brace initializer", current_token_);
					}
					
					auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, idenfifier_token);
					result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
					return ParseResult::success(*result);
				}
			}

			// Initially set result to a simple identifier - will be upgraded to FunctionCallNode if it's a function call
			if (!result.has_value()) {
				result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
			}

			// Check if this looks like a function call
			// Only consume '(' if the identifier is actually a function OR a function pointer OR has operator()
			FLASH_LOG_FORMAT(Parser, Debug, "FUNCTION_CALL_CHECK for '{}', identifierType.has_value()={}", 
				idenfifier_token.value(), identifierType.has_value());
			bool is_function_decl = identifierType && (identifierType->is<FunctionDeclarationNode>() || identifierType->is<TemplateFunctionDeclarationNode>());
			bool is_function_pointer = false;
			bool has_operator_call = false;
			if (identifierType) {
				FLASH_LOG_FORMAT(Parser, Debug, "identifierType exists for '{}'", idenfifier_token.value());
				const DeclarationNode* decl = get_decl_from_symbol(*identifierType);
				if (decl) {
					FLASH_LOG_FORMAT(Parser, Debug, "decl exists for '{}'", idenfifier_token.value());
					const auto& type_node = decl->type_node().as<TypeSpecifierNode>();
					FLASH_LOG_FORMAT(Parser, Debug, "type_node.type()={} for '{}'", static_cast<int>(type_node.type()), idenfifier_token.value());
					// Check for function pointers or function references (both have function_signature)
					is_function_pointer = type_node.is_function_pointer() || type_node.has_function_signature();
					FLASH_LOG_FORMAT(Parser, Debug, "is_function_pointer={} (is_fp={}, has_sig={}) for '{}'", 
						is_function_pointer, type_node.is_function_pointer(), type_node.has_function_signature(), idenfifier_token.value());

					// Check if this is a struct with operator()
					// Note: Lambda variables have Type::Auto (from auto lambda = [...]), not Type::Struct
					if (type_node.type() == Type::Struct || type_node.type() == Type::UserDefined || type_node.type() == Type::Auto) {
						TypeIndex type_index = type_node.type_index();
						FLASH_LOG_FORMAT(Parser, Debug, "Checking identifier '{}' for operator(): type_index={}", idenfifier_token.value(), type_index);
						if (type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index];
							if (type_info.struct_info_) {
								FLASH_LOG_FORMAT(Parser, Debug, "Struct '{}' has {} member functions", 
									StringTable::getStringView(type_info.struct_info_->name), type_info.struct_info_->member_functions.size());
								// Check if struct has operator()
								for (const auto& member_func : type_info.struct_info_->member_functions) {
									FLASH_LOG_FORMAT(Parser, Debug, "Member function: is_operator={}, symbol='{}'", 
										member_func.is_operator_overload, member_func.operator_symbol);
									if (member_func.is_operator_overload && member_func.operator_symbol == "()") {
										has_operator_call = true;
										break;
									}
								}
								FLASH_LOG_FORMAT(Parser, Debug, "has_operator_call for '{}': {}", idenfifier_token.value(), has_operator_call);
							}
						}
					}
					// Treat Type::Auto as a callable type (function pointer-like)
					// This handles generic lambda parameters: [](auto&& func) { func(); }
					else if (type_node.type() == Type::Auto) {
						is_function_pointer = true;
					}
				}
			}
			// Check if this is a template parameter (for constructor calls like T(...))
			bool is_template_parameter = identifierType && identifierType->is<TemplateParameterReferenceNode>();
			
			bool is_function_call = peek() == "("_tok &&
			                        (is_function_decl || is_function_pointer || has_operator_call || explicit_template_args.has_value() || is_template_parameter);

			if (is_function_call && consume("("_tok)) {
				if (peek().is_eof())
					return ParseResult::error(ParserError::NotImplemented, idenfifier_token);

				// Use parse_function_arguments to handle all argument parsing including brace-init-list
				auto args_result = parse_function_arguments(FlashCpp::FunctionArgumentContext{
					.handle_pack_expansion = true,
					.collect_types = false,
					.expand_simple_packs = true,
					.callee_name = idenfifier_token.value()
				});
				if (!args_result.success) {
					return ParseResult::error(args_result.error_message, args_result.error_token.value_or(current_token_));
				}
				ChunkedVector<ASTNode> args = std::move(args_result.args);

				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function call arguments", current_token_);
				}
				
				FLASH_LOG_FORMAT(Parser, Debug, "After parsing args: size={}, has_operator_call={}, is_template_parameter={}, is_function_pointer={}", 
					args.size(), has_operator_call, is_template_parameter, is_function_pointer);

				// For operator() calls, create a member function call
				if (has_operator_call) {
					// Create a member function call: object.operator()(args)
					auto object_expr = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));

					// Find the operator() function declaration in the struct
					const DeclarationNode* decl = get_decl_from_symbol(*identifierType);
					if (!decl) {
						return ParseResult::error("Invalid declaration for operator() call", idenfifier_token);
					}
					const auto& type_node = decl->type_node().as<TypeSpecifierNode>();
					TypeIndex type_index = type_node.type_index();
					const TypeInfo& type_info = gTypeInfo[type_index];

					// Find operator() in member functions
					FunctionDeclarationNode* operator_call_func = nullptr;
					for (const auto& member_func : type_info.struct_info_->member_functions) {
						if (member_func.is_operator_overload && member_func.operator_symbol == "()") {
							operator_call_func = &const_cast<FunctionDeclarationNode&>(member_func.function_decl.as<FunctionDeclarationNode>());
							break;
						}
					}

					if (!operator_call_func) {
						return ParseResult::error("operator() not found in struct", idenfifier_token);
					}

					Token operator_token(Token::Type::Identifier, "operator()"sv, idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					result = emplace_node<ExpressionNode>(MemberFunctionCallNode(object_expr, *operator_call_func, std::move(args), operator_token));
				}
				// For template parameter constructor calls, create ConstructorCallNode
				else if (is_template_parameter) {
					// This is a constructor call: T(args)
					const auto& template_param = identifierType->as<TemplateParameterReferenceNode>();
					// Create a TypeSpecifierNode for the template parameter
					Token param_token(Token::Type::Identifier, template_param.param_name().view(), idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
					auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, param_token);
					result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
				}
				// For function pointers, skip overload resolution and create FunctionCallNode directly
				else if (is_function_pointer) {
					const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
					if (!decl_ptr) {
						return ParseResult::error("Invalid function pointer declaration", idenfifier_token);
					}
					result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
					
					// Mark this as an indirect call (function pointer/reference)
					std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_indirect_call(true);
					
					// Copy mangled name if available
					if (identifierType->is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
						if (func_decl.has_mangled_name()) {
							std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
						}
					}
				}
				else {
					// Check if this is a constructor call on a template parameter
					if (result.has_value() && result->is<ExpressionNode>()) {
						const ExpressionNode& expr = result->as<ExpressionNode>();
						FLASH_LOG_FORMAT(Parser, Debug, "Checking if result is TemplateParameterReferenceNode, expr_index={}", expr.index());
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							// This is a constructor call: T(args)
							FLASH_LOG(Parser, Debug, "result IS TemplateParameterReferenceNode, moving args");
							const auto& template_param = std::get<TemplateParameterReferenceNode>(expr);
							// Create a TypeSpecifierNode for the template parameter
							Token param_token(Token::Type::Identifier, template_param.param_name().view(), idenfifier_token.line(), idenfifier_token.column(), idenfifier_token.file_index());
							auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::UserDefined, TypeQualifier::None, 0, param_token);
							result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), idenfifier_token));
						} else {
							FLASH_LOG_FORMAT(Parser, Debug, "result is NOT TemplateParameterReferenceNode, proceeding to overload resolution, args.size()={}", args.size());
							// Perform overload resolution for regular functions
							// First, get all overloads of this function
							auto all_overloads = gSymbolTable.lookup_all(idenfifier_token.value());

							// Extract argument types
							std::vector<TypeSpecifierNode> arg_types;
							for (size_t i = 0; i < args.size(); ++i) {
								auto arg_type = get_expression_type(args[i]);
								if (!arg_type.has_value()) {
									// If we can't determine the type, fall back to old behavior
									const DeclarationNode* decl_ptr = getDeclarationNode(*identifierType);
									if (!decl_ptr) {
										return ParseResult::error("Invalid function declaration", idenfifier_token);
									}
									result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
									
									// Copy mangled name if available
									if (identifierType->is<FunctionDeclarationNode>()) {
										const FunctionDeclarationNode& func_decl = identifierType->as<FunctionDeclarationNode>();
										if (func_decl.has_mangled_name()) {
											std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
										}
									}
									// Return early - we've created the FunctionCallNode with the args
									if (result.has_value())
										return ParseResult::success(*result);
									break;
								}
							
								TypeSpecifierNode arg_type_node = *arg_type;
							
								FLASH_LOG(Parser, Debug, "  get_expression_type returned: type=", (int)arg_type_node.type(), ", is_ref=", arg_type_node.is_reference(), ", is_rvalue_ref=", arg_type_node.is_rvalue_reference());
							
								// For perfect forwarding: check if argument is an lvalue
								// Lvalues: named variables, array subscripts, member access, dereferences, string literals
								// Rvalues: numeric/bool literals, temporaries, function calls returning non-reference
								if (args[i].is<ExpressionNode>()) {
									const ExpressionNode& arg_expr = args[i].as<ExpressionNode>();
									bool is_lvalue = std::visit([](const auto& inner) -> bool {
										using T = std::decay_t<decltype(inner)>;
										if constexpr (std::is_same_v<T, IdentifierNode>) {
											// Named variables are lvalues
											return true;
										} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
											// Array subscript expressions are lvalues
											return true;
										} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
											// Member access expressions are lvalues
											return true;
										} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
											// Dereference (*ptr) is an lvalue
											// Other unary operators like ++, --, etc. may also be lvalues
											return inner.op() == "*" || inner.op() == "++" || inner.op() == "--";
										} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
											// String literals are lvalues in C++
											return true;
										} else {
											// All other expressions (literals, temporaries, etc.) are rvalues
											return false;
										}
									}, arg_expr);
									
									if (is_lvalue) {
										// For forwarding reference deduction: Args&& deduces to T& for lvalues
										arg_type_node.set_lvalue_reference(true);
									}
								}
							
								arg_types.push_back(arg_type_node);
							}
							
							// If we successfully extracted all argument types, perform overload resolution
							if (arg_types.size() == args.size()) {
								// Check for explicit template arguments: either from local variable or pending member variable
								std::optional<std::vector<TemplateTypeArg>> effective_template_args;
								if (explicit_template_args.has_value()) {
									effective_template_args = explicit_template_args;
								} else if (pending_explicit_template_args_.has_value()) {
									effective_template_args = pending_explicit_template_args_;
									// Clear the pending args after using them
									pending_explicit_template_args_.reset();
								}
								
								// If explicit template arguments were provided, use them directly
								if (effective_template_args.has_value()) {
									// Check if any template arguments are dependent (contain template parameters)
									// In that case, we cannot instantiate the template now - it will be done at instantiation time
									bool has_dependent_template_args = false;
									for (const auto& targ : *effective_template_args) {
										if (targ.is_dependent) {
											has_dependent_template_args = true;
											break;
										}
									}
									
									// Skip template instantiation in extern "C" contexts - C has no templates
									std::optional<ASTNode> instantiated_func;
									if (current_linkage_ != Linkage::C && !has_dependent_template_args) {
										instantiated_func = try_instantiate_template_explicit(idenfifier_token.value(), *effective_template_args);
									}
									if (instantiated_func.has_value()) {
										// Check if the function is deleted
										const FunctionDeclarationNode* func_check = get_function_decl_node(*instantiated_func);
										if (func_check && func_check->is_deleted()) {
											return ParseResult::error("Call to deleted function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
										}
										// Successfully instantiated template
										const DeclarationNode* decl_ptr = getDeclarationNode(*instantiated_func);
										if (!decl_ptr) {
											return ParseResult::error("Invalid template instantiation", idenfifier_token);
										}
										result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
										
										// Copy mangled name if available
										if (instantiated_func->is<FunctionDeclarationNode>()) {
											const FunctionDeclarationNode& func_decl = instantiated_func->as<FunctionDeclarationNode>();
											if (func_decl.has_mangled_name()) {
												std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
											}
										}
									} else if (has_dependent_template_args) {
										// Template arguments are dependent - this is a template-dependent expression
										// Create a FunctionCallNode with a placeholder declaration that will be resolved during template instantiation
										// IMPORTANT: We must create a FunctionCallNode (not just IdentifierNode) to preserve the information
										// that this is a function call with template arguments. This is needed for non-type template arguments
										// like: bool_constant<test_func<T>()> where the function call result is used as a constant expression.
										FLASH_LOG(Templates, Debug, "Creating dependent FunctionCallNode for call to '", idenfifier_token.value(), "'");
										
										// Create a placeholder declaration for the dependent function call
										auto type_node = emplace_node<TypeSpecifierNode>(Type::Bool, TypeQualifier::None, 1, idenfifier_token);
										auto placeholder_decl = emplace_node<DeclarationNode>(type_node, idenfifier_token);
										const DeclarationNode& decl_ref = placeholder_decl.as<DeclarationNode>();
										
										// Create FunctionCallNode with the placeholder
										result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(decl_ref), std::move(args), idenfifier_token));
										
										// Store the template arguments in the FunctionCallNode for later resolution
										FunctionCallNode& func_call = std::get<FunctionCallNode>(result->as<ExpressionNode>());
										if (!explicit_template_arg_nodes.empty()) {
											func_call.set_template_arguments(std::move(explicit_template_arg_nodes));
										}
									} else {
										return ParseResult::error("No matching template for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
									}
								} else {
									// No explicit template arguments - try overload resolution first
									FLASH_LOG(Parser, Debug, "Function call to '", idenfifier_token.value(), "': found ", all_overloads.size(), " overload(s), ", arg_types.size(), " argument(s)");
									for (size_t i = 0; i < arg_types.size(); ++i) {
										const auto& arg = arg_types[i];
										FLASH_LOG(Parser, Debug, "  Arg[", i, "]: type=", (int)arg.type(), ", is_ref=", arg.is_reference(), ", is_rvalue_ref=", arg.is_rvalue_reference(), ", is_lvalue_ref=", arg.is_lvalue_reference(), ", is_ptr=", arg.is_pointer(), ", ptr_depth=", arg.pointer_depth());
									}
									if (all_overloads.empty()) {
										// No overloads found - try template instantiation (skip in extern "C" - C has no templates)
										std::optional<ASTNode> instantiated_func;
										if (current_linkage_ != Linkage::C) {
											instantiated_func = try_instantiate_template(idenfifier_token.value(), arg_types);
										}
										if (instantiated_func.has_value()) {
											// Check if the function is deleted
											const FunctionDeclarationNode* func_check = get_function_decl_node(*instantiated_func);
											if (func_check && func_check->is_deleted()) {
												return ParseResult::error("Call to deleted function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
											}
											// Successfully instantiated template
											const DeclarationNode* decl_ptr = getDeclarationNode(*instantiated_func);
											if (!decl_ptr) {
												return ParseResult::error("Invalid template instantiation", idenfifier_token);
											}
											result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
											
											// Copy mangled name if available
											if (instantiated_func->is<FunctionDeclarationNode>()) {
												const FunctionDeclarationNode& func_decl = instantiated_func->as<FunctionDeclarationNode>();
												if (func_decl.has_mangled_name()) {
													std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
												}
											}
										} else {
											// In SFINAE context (e.g., requires expression), function lookup failure
											// means the constraint is not satisfied - not an error
											if (in_sfinae_context_) {
												// Create a placeholder node to indicate failed lookup
												// The requires expression will treat this as "constraint not satisfied"
												result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
											} else {
												return ParseResult::error("No matching function for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
											}
										}
									} else {
										// Have overloads - do overload resolution
										auto resolution_result = resolve_overload(all_overloads, arg_types);

										FLASH_LOG(Parser, Debug, "Overload resolution result: has_match=", resolution_result.has_match, ", is_ambiguous=", resolution_result.is_ambiguous);
										
										if (resolution_result.is_ambiguous) {
											return ParseResult::error("Ambiguous call to overloaded function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
										} else if (!resolution_result.has_match) {
											// No matching regular function found - try template instantiation with deduction (skip in extern "C" - C has no templates)
											std::optional<ASTNode> instantiated_func;
											if (current_linkage_ != Linkage::C) {
												instantiated_func = try_instantiate_template(idenfifier_token.value(), arg_types);
											}
											if (instantiated_func.has_value()) {
												// Check if the function is deleted
												const FunctionDeclarationNode* func_check = get_function_decl_node(*instantiated_func);
												if (func_check && func_check->is_deleted()) {
													return ParseResult::error("Call to deleted function '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
												}
												// Successfully instantiated template
												const DeclarationNode* decl_ptr = getDeclarationNode(*instantiated_func);
												if (!decl_ptr) {
													return ParseResult::error("Invalid template instantiation", idenfifier_token);
												}
												result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
												
												// Copy mangled name if available
												if (instantiated_func->is<FunctionDeclarationNode>()) {
													const FunctionDeclarationNode& func_decl = instantiated_func->as<FunctionDeclarationNode>();
													if (func_decl.has_mangled_name()) {
														std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
													}
												}
											} else {
												// In SFINAE context (e.g., requires expression), function lookup failure
												// means the constraint is not satisfied - not an error
												if (in_sfinae_context_) {
													// Create a placeholder node to indicate failed lookup
													result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
												} else {
													return ParseResult::error("No matching function for call to '" + std::string(idenfifier_token.value()) + "'", idenfifier_token);
												}
											}
										} else {
											// Get the selected overload
											const DeclarationNode* decl_ptr = getDeclarationNode(*resolution_result.selected_overload);
											if (!decl_ptr) {
												return ParseResult::error("Invalid function declaration", idenfifier_token);
											}

											result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), idenfifier_token));
											
											// If the function has a pre-computed mangled name, set it on the FunctionCallNode
											// This is important for functions in namespaces accessed via using directives
											if (resolution_result.selected_overload->is<FunctionDeclarationNode>()) {
												const FunctionDeclarationNode& func_decl = resolution_result.selected_overload->as<FunctionDeclarationNode>();
												if (func_decl.has_mangled_name()) {
													std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
												}
											}
										}
									}
								}
							}
						}
					}
				}

			}
			else {
				// Regular identifier
				// Additional type checking and verification logic can be performed here using identifierType

				result = emplace_node<ExpressionNode>(IdentifierNode(idenfifier_token));
			}
		}
	}
	else if (current_token_.type() == Token::Type::Literal) {
		auto literal_type = get_numeric_literal_type(current_token_.value());
		if (!literal_type) {
			return ParseResult::error("Expected numeric literal", current_token_);
		}
		result = emplace_node<ExpressionNode>(NumericLiteralNode(current_token_, literal_type->value, literal_type->type, literal_type->typeQualifier, literal_type->sizeInBits));
		advance();
	}
	else if (current_token_.type() == Token::Type::StringLiteral) {
		// Handle adjacent string literal concatenation
		// C++ allows "Hello " "World" to be concatenated into "Hello World"
		Token first_string = current_token_;
		std::string concatenated_value(first_string.value());
		advance();

		// Check for adjacent string literals
		while (peek().is_string_literal()) {
			Token next_string = peek_info();
			// Remove quotes from both strings and concatenate
			// First string: remove trailing quote
			// Next string: remove leading quote
			std::string_view first_content = concatenated_value;
			if (first_content.size() >= 2 && first_content.back() == '"') {
				first_content.remove_suffix(1);
			}
			std::string_view next_content = next_string.value();
			if (next_content.size() >= 2 && next_content.front() == '"') {
				next_content.remove_prefix(1);
			}

			// Concatenate: first_content (without trailing ") + next_content (without leading ")
			concatenated_value = std::string(first_content) + std::string(next_content);
			advance();
		}

		// Store the concatenated string in CompileContext so it persists
		std::string_view persistent_string = context_.storeFunctionNameLiteral(concatenated_value);
		Token concatenated_token(Token::Type::StringLiteral,
		                         persistent_string,
		                         first_string.line(),
		                         first_string.column(),
		                         first_string.file_index());

		result = emplace_node<ExpressionNode>(StringLiteralNode(concatenated_token));
		
		// Check for user-defined literal suffix: "hello"_suffix or "hello"sv
		if (peek_info().type() == Token::Type::Identifier) {
			std::string_view suffix = peek_info().value();
			// UDL suffixes start with _ (user-defined) or are standard (sv, s, etc.)
			if (suffix.size() > 0 && (suffix[0] == '_' || suffix == "sv" || suffix == "s")) {
				// Save position before consuming suffix in case the operator is not found
				SaveHandle pre_suffix_pos = save_token_position();
				Token suffix_token = peek_info();
				advance(); // consume suffix
				
				// Build the operator name: operator""_suffix
				std::string_view operator_name = StringBuilder().append("operator\"\""sv).append(suffix).commit();
				
				// Look up the UDL operator in the symbol table
				auto udl_lookup = gSymbolTable.lookup(operator_name);
				if (udl_lookup.has_value() && udl_lookup->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = udl_lookup->as<FunctionDeclarationNode>();
					DeclarationNode& decl = const_cast<DeclarationNode&>(func_decl.decl_node());
					
					// Build arguments: the string literal and its length
					ChunkedVector<ASTNode> args;
					args.push_back(*result);  // string literal
					
					// Calculate string length (excluding quotes)
					std::string_view str_val = persistent_string;
					size_t str_len = 0;
					if (str_val.size() >= 2) {
						str_len = str_val.size() - 2;  // Remove opening and closing quotes
					}
					
					// Create a NumericLiteralNode for the length
					std::string_view len_placeholder_sv = "0";
					Token len_token(Token::Type::Literal, len_placeholder_sv, suffix_token.line(), suffix_token.column(), suffix_token.file_index());
					auto len_node = emplace_node<ExpressionNode>(
						NumericLiteralNode(len_token, static_cast<unsigned long long>(str_len), Type::UnsignedLong, TypeQualifier::None, 64));
					args.push_back(len_node);
					
					result = emplace_node<ExpressionNode>(
						FunctionCallNode(decl, std::move(args), suffix_token));
					
					// Set mangled name if available
					if (func_decl.has_mangled_name()) {
						std::get<FunctionCallNode>(result->as<ExpressionNode>()).set_mangled_name(func_decl.mangled_name());
					}
				} else {
					// Operator not found - restore position so suffix token is not lost
					restore_token_position(pre_suffix_pos);
				}
			}
		}
	}
	else if (current_token_.type() == Token::Type::CharacterLiteral) {
		// Parse character literal and convert to numeric value
		std::string_view value = current_token_.value();

		// Character literal format:
		// - Regular: 'x' or '\x' (char_offset = 1)
		// - Wide: L'x' or L'\x' (char_offset = 2)
		// - char8_t: u8'x' (char_offset = 3)
		// - char16_t: u'x' (char_offset = 2)
		// - char32_t: U'x' (char_offset = 2)
		size_t char_offset = 1;  // Default: regular char literal 'x'
		Type char_type = Type::Char;
		int char_size_bits = 8;

		// Check for prefix (wide character literals)
		if (value.size() > 0 && value[0] == 'L') {
			char_offset = 2;  // L'x'
			char_type = Type::WChar;
			char_size_bits = get_wchar_size_bits();
		} else if (value.size() > 1 && value[0] == 'u' && value[1] == '8') {
			char_offset = 3;  // u8'x'
			char_type = Type::Char8;
			char_size_bits = 8;
		} else if (value.size() > 0 && value[0] == 'u') {
			char_offset = 2;  // u'x'
			char_type = Type::Char16;
			char_size_bits = 16;
		} else if (value.size() > 0 && value[0] == 'U') {
			char_offset = 2;  // U'x'
			char_type = Type::Char32;
			char_size_bits = 32;
		}

		// Minimum size check: prefix + quote + char + quote
		if (value.size() < char_offset + 2) {
			return ParseResult::error("Invalid character literal", current_token_);
		}

		uint32_t char_value = 0;  // Use uint32_t for wide chars
		if (value[char_offset] == '\\') {
			// Escape sequence
			if (value.size() < char_offset + 3) {
				return ParseResult::error("Invalid escape sequence in character literal", current_token_);
			}
			char escape_char = value[char_offset + 1];
			switch (escape_char) {
				case 'n': char_value = '\n'; break;
				case 't': char_value = '\t'; break;
				case 'r': char_value = '\r'; break;
				case '0': char_value = '\0'; break;
				case '\\': char_value = '\\'; break;
				case '\'': char_value = '\''; break;
				case '"': char_value = '"'; break;
				default:
					return ParseResult::error("Unknown escape sequence in character literal", current_token_);
			}
		}
		else {
			// Single character
			char_value = static_cast<unsigned char>(value[char_offset]);
		}

		// Create a numeric literal node with the character's value
		result = emplace_node<ExpressionNode>(NumericLiteralNode(current_token_,
			static_cast<unsigned long long>(char_value),
			char_type, TypeQualifier::None, char_size_bits));
		advance();
	}
	else if (current_token_.type() == Token::Type::Keyword &&
			 (current_token_.value() == "true"sv || current_token_.value() == "false"sv)) {
		// Handle bool literals
		bool value = (current_token_.value() == "true");
		result = emplace_node<ExpressionNode>(BoolLiteralNode(current_token_, value));
		advance();
	}
	else if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "nullptr"sv) {
		// Handle nullptr literal - represented as null pointer constant (0)
		// The actual type will be determined by context (can convert to any pointer type)
		result = emplace_node<ExpressionNode>(NumericLiteralNode(current_token_,
			0ULL, Type::Int, TypeQualifier::None, 64));
		advance();
	}
	else if (current_token_.type() == Token::Type::Keyword && current_token_.value() == "this"sv) {
		// Handle 'this' keyword - represents a pointer to the current object
		// Only valid inside member functions
		if (member_function_context_stack_.empty()) {
			return ParseResult::error("'this' can only be used inside a member function", current_token_);
		}

		Token this_token = current_token_;
		advance();

		// Create an identifier node for 'this'
		result = emplace_node<ExpressionNode>(IdentifierNode(this_token));
	}
	else if (current_token_.type() == Token::Type::Punctuator && current_token_.value() == "{") {
		// Handle braced initializer in expression context
		// Examples:
		//   return { .a = 5 };  // Aggregate initialization with return type
		//   func({})            // Braced initializer as function argument (type inferred from parameter)
		
		// Check if we're parsing a function argument by looking at the expression context
		// In a function call argument, braced initializers are valid and their type is inferred
		// from the function parameter type. Since we're doing a single-pass parser and we might
		// not have resolved the function yet, we just accept it as a placeholder.
		
		// For now, if we don't have a current_function_ context (which means we're not in a
		// return statement), just parse it as an empty braced initializer placeholder.
		// This handles cases like: decltype(func({})) in template default parameters
		if (!current_function_) {
			Token brace_token = current_token_;
			advance(); // consume '{'
			
			// Skip the contents of the braced initializer
			// We need to match braces to find the closing '}'
			int brace_depth = 1;
			while (brace_depth > 0 && !current_token_.kind().is_eof()) {
				if (current_token_.value() == "{") {
					brace_depth++;
				} else if (current_token_.value() == "}") {
					brace_depth--;
				}
				if (brace_depth > 0) {
					advance();
				}
			}
			
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' to close braced initializer", current_token_);
			}
			
			// Create a placeholder literal node - the type will be inferred from context
			// (e.g., function parameter type, variable declaration type, etc.)
			// The actual value doesn't matter, only that it represents a braced initializer
			NumericLiteralValue val = static_cast<unsigned long long>(0);
			result = emplace_node<ExpressionNode>(NumericLiteralNode(brace_token, val, Type::Int, TypeQualifier::None, 32));
			return ParseResult::success(*result);
		}
		
		// We're in a function body (current_function_ is set)
		// Get the return type from the current function
		const DeclarationNode& func_decl = current_function_->decl_node();
		const ASTNode& return_type_node = func_decl.type_node();
		
		if (!return_type_node.is<TypeSpecifierNode>()) {
			return ParseResult::error("Cannot determine return type for braced initializer", current_token_);
		}
		
		const TypeSpecifierNode& return_type = return_type_node.as<TypeSpecifierNode>();
		
		// Parse the braced initializer with the return type
		ParseResult init_result = parse_brace_initializer(return_type);
		if (init_result.is_error()) {
			return init_result;
		}
		
		if (!init_result.node().has_value()) {
			return ParseResult::error("Expected initializer expression", current_token_);
		}
		
		// For scalar types, parse_brace_initializer already returns an expression
		// Just return it directly
		return init_result;
	}
	else if (consume("("_tok)) {
		// Could be either:
		// 1. C-style cast: (Type)expression
		// 2. Parenthesized expression: (expression)
		// 3. C++17 Fold expression: (...op pack), (pack op...), (init op...op pack), (pack op...op init)
		
		// Check for fold expression patterns
		SaveHandle fold_check_pos = save_token_position();
		bool is_fold = false;
		
		// Pattern 1: Unary left fold: (... op pack)
		if (peek() == "..."_tok) {
			advance(); // consume ...
			
			// Next should be an operator
			if (peek().is_operator()) {
				std::string_view fold_op = peek_info().value();
				Token op_token = peek_info();
				advance(); // consume operator
				
				// Next should be the pack identifier
				if (peek().is_identifier()) {
					std::string_view pack_name = peek_info().value();
					advance(); // consume pack name
					
					if (consume(")"_tok)) {
						// Valid unary left fold: (... op pack)
						discard_saved_token(fold_check_pos);
						result = emplace_node<ExpressionNode>(
							FoldExpressionNode(pack_name, fold_op, 
								FoldExpressionNode::Direction::Left, op_token));
						is_fold = true;
					}
				}
			}
		}
		
		if (!is_fold) {
			restore_token_position(fold_check_pos);
			
			// Pattern 2 & 4: Check if starts with identifier (could be pack or init)
			if (peek().is_identifier()) {
				std::string_view first_id = peek_info().value();
				advance(); // consume identifier
				
				// Check what follows
				if (peek().is_operator()) {
					std::string_view fold_op = peek_info().value();
					Token op_token = peek_info();
					advance(); // consume operator
					
					// Check for ... (fold expression)
					if (peek() == "..."_tok) {
						advance(); // consume ...
						
						// Check if binary fold or unary right fold
						if (peek().is_operator() &&
							peek_info().value() == fold_op) {
							// Binary right fold: (pack op ... op init)
							advance(); // consume second operator
							
							ParseResult init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (!init_result.is_error() && init_result.node().has_value() &&
								consume(")"_tok)) {
								discard_saved_token(fold_check_pos);
								result = emplace_node<ExpressionNode>(
									FoldExpressionNode(first_id, fold_op,
										FoldExpressionNode::Direction::Right, *init_result.node(), op_token));
								is_fold = true;
							}
						} else if (consume(")"_tok)) {
							// Unary right fold: (pack op ...)
							discard_saved_token(fold_check_pos);
							result = emplace_node<ExpressionNode>(
								FoldExpressionNode(first_id, fold_op,
									FoldExpressionNode::Direction::Right, op_token));
							is_fold = true;
						}
					}
				}
			}
		}
		
		// Pattern 3: Binary left fold: (init op ... op pack)
		// This is tricky because init can be a complex expression
		// For now, we'll handle simple cases where init is a literal or identifier
		if (!is_fold) {
			restore_token_position(fold_check_pos);
			
			// Try to parse as a simple expression
			SaveHandle init_pos = save_token_position();
			ParseResult init_result = parse_primary_expression(ExpressionContext::Normal);
			
			if (!init_result.is_error() && init_result.node().has_value()) {
				if (peek().is_operator()) {
					std::string_view fold_op = peek_info().value();
					Token op_token = peek_info();
					advance(); // consume operator
					
					if (peek() == "..."_tok) {
						advance(); // consume ...
						
						if (peek().is_operator() &&
							peek_info().value() == fold_op) {
							advance(); // consume second operator
							
							if (peek().is_identifier()) {
								std::string_view pack_name = peek_info().value();
								advance(); // consume pack name
								
								if (consume(")"_tok)) {
									// Valid binary left fold: (init op ... op pack)
									discard_saved_token(fold_check_pos);
									discard_saved_token(init_pos);
									result = emplace_node<ExpressionNode>(
										FoldExpressionNode(pack_name, fold_op,
											FoldExpressionNode::Direction::Left, *init_result.node(), op_token));
									is_fold = true;
								}
							}
						} else if (consume(")"_tok)) {
							// Unary right fold with complex expression: (expr op ...)
							// The expression is a pack expansion that will be folded
							discard_saved_token(fold_check_pos);
							discard_saved_token(init_pos);
							// For unary right fold, we need to extract the pack name from the expression
							// If the expression contains a pack expansion, use that
							// For now, we'll create a fold expression with the expression as the pack
							result = emplace_node<ExpressionNode>(
								FoldExpressionNode(*init_result.node(), fold_op,
									FoldExpressionNode::Direction::Right, op_token));
							is_fold = true;
						}
					}
				}
			}
			
			if (!is_fold) {
				restore_token_position(init_pos);
			}
		}
		
		// If not a fold expression, parse as parenthesized expression
		if (!is_fold) {
			restore_token_position(fold_check_pos);
		
			// Parse as parenthesized expression
			// Note: C-style casts are now handled in parse_unary_expression()
			// Allow comma operator in parenthesized expressions
			// Pass the context so the expression parser knows how to handle special tokens
			ParseResult paren_result = parse_expression(MIN_PRECEDENCE, context);
			if (paren_result.is_error()) {
				return paren_result;
			}
			
			// In TemplateArgument or Decltype context, allow pack expansion (...) before closing paren
			// Pattern: (expr...) where ... is pack expansion operator
			// This is needed for patterns like decltype((expr...)) in template contexts
			if ((context == ExpressionContext::TemplateArgument || context == ExpressionContext::Decltype) &&
			    peek() == "..."_tok) {
				// Consume the ... and create a PackExpansionExprNode
				Token ellipsis_token = peek_info();
				advance(); // consume '...'
				
				// Wrap the expression in a PackExpansionExprNode
				if (paren_result.node().has_value()) {
					result = emplace_node<ExpressionNode>(
						PackExpansionExprNode(*paren_result.node(), ellipsis_token));
				} else {
					return ParseResult::error("Expected expression before '...'", current_token_);
				}
				
				FLASH_LOG(Parser, Debug, "Created PackExpansionExprNode for parenthesized pack expansion");
			} else {
				result = paren_result.node();
			}
			
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after parenthesized expression",
					current_token_);
			}
		}  // End of fold expression check
	}
	else {
		return ParseResult::error("Expected primary expression", current_token_);
	}

found_member_variable:  // Label for member variable detection - jump here to skip error checking
	// Phase 3: Postfix operators are now handled in parse_postfix_expression()
	// Return the primary expression result
	if (result.has_value())
		return ParseResult::success(*result);

	// No result was produced - this should not happen in a well-formed expression
	return ParseResult();  // Return monostate instead of empty success
}

ParseResult Parser::parse_for_loop() {
    if (!consume("for"_tok)) {
        return ParseResult::error("Expected 'for' keyword", current_token_);
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'for'", current_token_);
    }

    // Enter a new scope for the for loop (C++ standard: for-init-statement creates a scope)
    FlashCpp::SymbolTableScope for_scope(ScopeType::Block);

    // Parse initialization (optional: can be empty, declaration, or expression)
    std::optional<ASTNode> init_statement;

    // Check if init is empty (starts with semicolon)
    if (!consume(";"_tok)) {
        // Not empty, parse init statement
        bool try_as_declaration = false;
        
        if (!peek().is_eof()) {
            if (peek().is_keyword()) {
                // Check if it's a type keyword or CV-qualifier (variable declaration)
                if (type_keywords.find(peek_info().value()) != type_keywords.end()) {
                    try_as_declaration = true;
                }
            } else if (peek().is_identifier()) {
                // Check if it's a known type name (e.g., size_t, string, etc.) or a qualified type (std::size_t)
                StringHandle type_handle = peek_info().handle();
                if (lookupTypeInCurrentContext(type_handle)) {
                    try_as_declaration = true;
                } else if (peek(1) == "::"_tok) {
                    // Treat Identifier followed by :: as a potential qualified type name
                    try_as_declaration = true;
                }
            }
        }
        
        if (try_as_declaration) {
            // Handle variable declaration
            SaveHandle decl_saved = save_token_position();
            ParseResult init = parse_variable_declaration();
            if (init.is_error()) {
                // Not a declaration, backtrack and try as expression instead
                restore_token_position(decl_saved);
                ParseResult expr_init = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
                if (expr_init.is_error()) {
                    return expr_init;
                }
                init_statement = expr_init.node();
            } else {
                init_statement = init.node();
            }
        } else {
            // Try parsing as expression
            ParseResult init = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
            if (init.is_error()) {
                return init;
            }
            init_statement = init.node();
        }

        // Check for ranged-for syntax: for (declaration : range_expression)
        if (consume(":"_tok)) {
            // This is a ranged for loop (without init-statement)
            if (!init_statement.has_value()) {
                return ParseResult::error("Ranged for loop requires a loop variable declaration", current_token_);
            }

            // Parse the range expression
            ParseResult range_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
            if (range_result.is_error()) {
                return range_result;
            }

            auto range_expr = range_result.node();
            if (!range_expr.has_value()) {
                return ParseResult::error("Expected range expression in ranged for loop", current_token_);
            }

            if (!consume(")"_tok)) {
                return ParseResult::error("Expected ')' after ranged for loop range expression", current_token_);
            }

            // Parse body (can be a block or a single statement)
            ParseResult body_result;
            if (peek() == "{"_tok) {
                body_result = parse_block();
            } else {
                body_result = parse_statement_or_declaration();
            }

            if (body_result.is_error()) {
                return body_result;
            }

            auto body_node = body_result.node();
            if (!body_node.has_value()) {
                return ParseResult::error("Invalid ranged for loop body", current_token_);
            }

            return ParseResult::success(emplace_node<RangedForStatementNode>(
                *init_statement, *range_expr, *body_node
            ));
        }

        if (!consume(";"_tok)) {
            return ParseResult::error("Expected ';' after for loop initialization", current_token_);
        }
    }

    // At this point, we've parsed the init statement (or it was empty) and consumed the first semicolon
    // Now check for C++20 range-based for with init-statement: for (init; decl : range)
    // This requires checking if the next part looks like a range declaration
    
    // Save position to potentially backtrack
    SaveHandle range_check_pos = save_token_position();
    
    // Check if this could be a C++20 range-based for with init-statement
    bool is_range_for_with_init = false;
    std::optional<ASTNode> range_decl;
    
    if (peek().is_keyword() &&
        type_keywords.find(peek_info().value()) != type_keywords.end()) {
        // Try to parse as a range declaration
        ParseResult decl_result = parse_variable_declaration();
        if (!decl_result.is_error() && decl_result.node().has_value()) {
            // Check if followed by ':'
            if (peek() == ":"_tok) {
                is_range_for_with_init = true;
                range_decl = decl_result.node();
            }
        }
    }
    
    if (is_range_for_with_init) {
        // This is a C++20 range-based for with init-statement
        consume(":"_tok);  // consume the ':'
        
        // Parse the range expression
        ParseResult range_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
        if (range_result.is_error()) {
            return range_result;
        }

        auto range_expr = range_result.node();
        if (!range_expr.has_value()) {
            return ParseResult::error("Expected range expression in ranged for loop", current_token_);
        }

        if (!consume(")"_tok)) {
            return ParseResult::error("Expected ')' after ranged for loop range expression", current_token_);
        }

        // Parse body (can be a block or a single statement)
        ParseResult body_result;
        if (peek() == "{"_tok) {
            body_result = parse_block();
        } else {
            body_result = parse_statement_or_declaration();
        }

        if (body_result.is_error()) {
            return body_result;
        }

        auto body_node = body_result.node();
        if (!body_node.has_value()) {
            return ParseResult::error("Invalid ranged for loop body", current_token_);
        }

        // Create ranged for statement with init-statement
        return ParseResult::success(emplace_node<RangedForStatementNode>(
            *range_decl, *range_expr, *body_node, init_statement
        ));
    }
    
    // Not a range-based for with init - restore position and continue with regular for loop
    restore_token_position(range_check_pos);

    // Parse condition (optional: can be empty, defaults to true)
    std::optional<ASTNode> condition;

    // Check if condition is empty (next token is semicolon)
    if (!consume(";"_tok)) {
        // Not empty, parse condition expression
        ParseResult cond_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
        if (cond_result.is_error()) {
            return cond_result;
        }
        condition = cond_result.node();

        if (!consume(";"_tok)) {
            return ParseResult::error("Expected ';' after for loop condition", current_token_);
        }
    }

    // Parse increment/update expression (optional: can be empty)
    std::optional<ASTNode> update_expression;

    // Check if increment is empty (next token is closing paren)
    if (!consume(")"_tok)) {
        // Not empty, parse increment expression (allow comma operator)
        ParseResult inc_result = parse_expression(MIN_PRECEDENCE, ExpressionContext::Normal);
        if (inc_result.is_error()) {
            return inc_result;
        }
        update_expression = inc_result.node();

        if (!consume(")"_tok)) {
            return ParseResult::error("Expected ')' after for loop increment", current_token_);
        }
    }

    // Parse body (can be a block or a single statement)
    ParseResult body_result;
    if (peek() == "{"_tok) {
        body_result = parse_block();
    } else {
        body_result = parse_statement_or_declaration();
    }

    if (body_result.is_error()) {
        return body_result;
    }

    // Create for statement node with optional components
    auto body_node = body_result.node();
    if (!body_node.has_value()) {
        return ParseResult::error("Invalid for loop body", current_token_);
    }

    return ParseResult::success(emplace_node<ForStatementNode>(
        init_statement, condition, update_expression, *body_node
    ));
}

ParseResult Parser::parse_while_loop() {
    if (!consume("while"_tok)) {
        return ParseResult::error("Expected 'while' keyword", current_token_);
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'while'", current_token_);
    }

    // Parse condition
    ParseResult condition_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    if (condition_result.is_error()) {
        return condition_result;
    }

    if (!consume(")"_tok)) {
        return ParseResult::error("Expected ')' after while condition", current_token_);
    }

    // Parse body (can be a block or a single statement)
    // Always use parse_statement_or_declaration to ensure proper scope management
    ParseResult body_result = parse_statement_or_declaration();
    if (body_result.is_error()) {
        return body_result;
    }

    // Create while statement node
    auto condition_node = condition_result.node();
    auto body_node = body_result.node();
    if (!condition_node.has_value() || !body_node.has_value()) {
        return ParseResult::error("Invalid while loop construction", current_token_);
    }

    return ParseResult::success(emplace_node<WhileStatementNode>(
        *condition_node, *body_node
    ));
}

ParseResult Parser::parse_do_while_loop() {
    if (!consume("do"_tok)) {
        return ParseResult::error("Expected 'do' keyword", current_token_);
    }

    // Parse body (can be a block or a single statement)
    // Always use parse_statement_or_declaration to ensure proper scope management
    ParseResult body_result = parse_statement_or_declaration();
    if (body_result.is_error()) {
        return body_result;
    }

    // For non-block body statements, consume the trailing semicolon
    // (parse_block handles this internally, but single statements don't)
    if (body_result.node().has_value() && !body_result.node()->is<BlockNode>()) {
        consume(";"_tok);
    }

    if (!consume("while"_tok)) {
        return ParseResult::error("Expected 'while' after do-while body", current_token_);
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'while'", current_token_);
    }

    // Parse condition
    ParseResult condition_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    if (condition_result.is_error()) {
        return condition_result;
    }

    if (!consume(")"_tok)) {
        return ParseResult::error("Expected ')' after do-while condition", current_token_);
    }

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after do-while statement", current_token_);
    }

    // Create do-while statement node
    auto body_node = body_result.node();
    auto condition_node = condition_result.node();
    if (!body_node.has_value() || !condition_node.has_value()) {
        return ParseResult::error("Invalid do-while loop construction", current_token_);
    }

    return ParseResult::success(emplace_node<DoWhileStatementNode>(
        *body_node, *condition_node
    ));
}

ParseResult Parser::parse_break_statement() {
    auto break_token_opt = peek_info();
    if (break_token_opt.value() != "break"sv) {
        return ParseResult::error("Expected 'break' keyword", current_token_);
    }

    Token break_token = break_token_opt;
    advance(); // Consume the 'break' keyword

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after break statement", current_token_);
    }

    return ParseResult::success(emplace_node<BreakStatementNode>(break_token));
}

ParseResult Parser::parse_continue_statement() {
    auto continue_token_opt = peek_info();
    if (continue_token_opt.value() != "continue"sv) {
        return ParseResult::error("Expected 'continue' keyword", current_token_);
    }

    Token continue_token = continue_token_opt;
    advance(); // Consume the 'continue' keyword

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after continue statement", current_token_);
    }

    return ParseResult::success(emplace_node<ContinueStatementNode>(continue_token));
}

ParseResult Parser::parse_goto_statement() {
    auto goto_token_opt = peek_info();
    if (goto_token_opt.value() != "goto"sv) {
        return ParseResult::error("Expected 'goto' keyword", current_token_);
    }

    Token goto_token = goto_token_opt;
    advance(); // Consume the 'goto' keyword

    // Parse the label identifier
    auto label_token_opt = peek_info();
    if (label_token_opt.type() != Token::Type::Identifier) {
        return ParseResult::error("Expected label identifier after 'goto'", current_token_);
    }

    Token label_token = label_token_opt;
    advance(); // Consume the label identifier

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after goto statement", current_token_);
    }

    return ParseResult::success(emplace_node<GotoStatementNode>(label_token, goto_token));
}

ParseResult Parser::parse_label_statement() {
    // This is called when we've detected identifier followed by ':'
    // The identifier token should be the current token
    auto label_token_opt = peek_info();
    if (label_token_opt.type() != Token::Type::Identifier) {
        return ParseResult::error("Expected label identifier", current_token_);
    }

    Token label_token = label_token_opt;
    advance(); // Consume the label identifier

    if (!consume(":"_tok)) {
        return ParseResult::error("Expected ':' after label", current_token_);
    }

    return ParseResult::success(emplace_node<LabelStatementNode>(label_token));
}

ParseResult Parser::parse_try_statement() {
    // Parse: try { block } catch (type identifier) { block } [catch (...) { block }]
    auto try_token_opt = peek_info();
    if (try_token_opt.value() != "try"sv) {
        return ParseResult::error("Expected 'try' keyword", current_token_);
    }

    Token try_token = try_token_opt;
    advance(); // Consume the 'try' keyword

    // Parse the try block
    auto try_block_result = parse_block();
    if (try_block_result.is_error()) {
        return try_block_result;
    }

    ASTNode try_block = *try_block_result.node();

    // Parse catch clauses (at least one required)
    std::vector<ASTNode> catch_clauses;

    while (peek() == "catch"_tok) {
        Token catch_token = peek_info();
        advance(); // Consume the 'catch' keyword

        if (!consume("("_tok)) {
            return ParseResult::error("Expected '(' after 'catch'", current_token_);
        }

        std::optional<ASTNode> exception_declaration;
        bool is_catch_all = false;

        // Check for catch(...)
        if (peek() == "..."_tok) {
            advance(); // Consume '...'
            is_catch_all = true;
        } else {
            // Parse exception type and optional identifier
            auto type_result = parse_type_and_name();
            if (type_result.is_error()) {
                return type_result;
            }
            exception_declaration = type_result.node();
        }

        if (!consume(")"_tok)) {
            return ParseResult::error("Expected ')' after catch declaration", current_token_);
        }

        // Enter a new scope for the catch block and add the exception parameter to the symbol table
        gSymbolTable.enter_scope(ScopeType::Block);
        
        // Add exception parameter to symbol table (if it's not catch(...))
        if (!is_catch_all && exception_declaration.has_value()) {
            const auto& decl = exception_declaration->as<DeclarationNode>();
            if (!decl.identifier_token().value().empty()) {
                gSymbolTable.insert(decl.identifier_token().value(), *exception_declaration);
            }
        }

        // Parse the catch block
        auto catch_block_result = parse_block();
        
        // Exit the catch block scope
        gSymbolTable.exit_scope();
        
        if (catch_block_result.is_error()) {
            return catch_block_result;
        }

        ASTNode catch_block = *catch_block_result.node();

        // Create the catch clause node
        if (is_catch_all) {
            catch_clauses.push_back(emplace_node<CatchClauseNode>(catch_block, catch_token, true));
        } else {
            catch_clauses.push_back(emplace_node<CatchClauseNode>(exception_declaration, catch_block, catch_token));
        }
    }

    if (catch_clauses.empty()) {
        return ParseResult::error("Expected at least one 'catch' clause after 'try' block", current_token_);
    }

    return ParseResult::success(emplace_node<TryStatementNode>(try_block, std::move(catch_clauses), try_token));
}

ParseResult Parser::parse_throw_statement() {
    // Parse: throw; or throw expression;
    auto throw_token_opt = peek_info();
    if (throw_token_opt.value() != "throw"sv) {
        return ParseResult::error("Expected 'throw' keyword", current_token_);
    }

    Token throw_token = throw_token_opt;
    advance(); // Consume the 'throw' keyword

    // Check for rethrow (throw;)
    if (peek() == ";"_tok) {
        advance(); // Consume ';'
        return ParseResult::success(emplace_node<ThrowStatementNode>(throw_token));
    }

    // Parse the expression to throw
    auto expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    if (expr_result.is_error()) {
        return expr_result;
    }

    if (!consume(";"_tok)) {
        return ParseResult::error("Expected ';' after throw expression", current_token_);
    }

    return ParseResult::success(emplace_node<ThrowStatementNode>(*expr_result.node(), throw_token));
}

ParseResult Parser::parse_lambda_expression() {
    // Expect '['
    if (!consume("["_tok)) {
        return ParseResult::error("Expected '[' to start lambda expression", current_token_);
    }

    Token lambda_token = current_token_;

    // Parse captures
    std::vector<LambdaCaptureNode> captures;

    // Check for empty capture list
    if (peek() != "]"_tok) {
        // Parse capture list
        while (true) {
            auto token = peek_info();
            if (peek().is_eof()) {
                return ParseResult::error("Unexpected end of file in lambda capture list", current_token_);
            }

            // Check for capture-all
            if (token.value() == "=") {
                advance();
                captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::AllByValue));
            } else if (token.value() == "&") {
                advance();
                // Check if this is capture-all by reference or a specific reference capture
                auto next_token = peek_info();
                if (next_token.type() == Token::Type::Identifier) {
                    // Could be [&x] or [&x = expr]
                    Token id_token = next_token;
                    advance();
                    
                    // Check for init-capture: [&x = expr]
                    if (peek() == "="_tok) {
                        advance(); // consume '='
                        auto init_expr = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
                        if (init_expr.is_error()) {
                            return init_expr;
                        }
                        captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByReference, id_token, *init_expr.node()));
                    } else {
                        // Simple reference capture: [&x]
                        captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByReference, id_token));
                    }
                } else {
                    // Capture-all by reference: [&]
                    captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::AllByReference));
                }
            } else if (token.type() == Token::Type::Operator && token.value() == "*") {
                // Check for [*this] capture (C++17)
                advance(); // consume '*'
                auto next_token = peek_info();
                if (next_token.value() == "this") {
                    Token this_token = next_token;
                    advance(); // consume 'this'
                    captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::CopyThis, this_token));
                } else {
                    return ParseResult::error("Expected 'this' after '*' in lambda capture", current_token_);
                }
            } else if (token.type() == Token::Type::Identifier || token.type() == Token::Type::Keyword) {
                // Check for 'this' keyword first
                if (token.value() == "this") {
                    Token this_token = token;
                    advance();
                    captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::This, this_token));
                } else if (token.type() == Token::Type::Identifier) {
                    // Could be [x] or [x = expr]
                    Token id_token = token;
                    advance();
                    
                    
                    // Check for init-capture: [x = expr]
                    if (peek() == "="_tok) {
                        advance(); // consume '='
                        auto init_expr = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
                        if (init_expr.is_error()) {
                            return init_expr;
                        }
                        captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByValue, id_token, *init_expr.node()));
                    } else {
                        // Simple value capture: [x]
                        captures.push_back(LambdaCaptureNode(LambdaCaptureNode::CaptureKind::ByValue, id_token));
                    }
                } else {
                    return ParseResult::error("Expected capture specifier in lambda", token);
                }
            } else {
                return ParseResult::error("Expected capture specifier in lambda", token);
            }

            // Check for comma (more captures) or closing bracket
            if (peek() == ","_tok) {
                advance(); // consume comma
            } else {
                break;
            }
        }
    }

    // Expect ']'
    if (!consume("]"_tok)) {
        return ParseResult::error("Expected ']' after lambda captures", current_token_);
    }

    // Parse optional template parameter list (C++20): []<typename T>(...) 
    std::vector<std::string_view> template_param_names;
    if (peek() == "<"_tok) {
        advance(); // consume '<'
        
        // Parse template parameters
        while (true) {
            // Expect 'typename' or 'class' keyword
            if (peek().is_eof()) {
                return ParseResult::error("Expected template parameter", current_token_);
            }
            
            auto keyword_token = peek_info();
            if (keyword_token.value() != "typename" && keyword_token.value() != "class") {
                return ParseResult::error("Expected 'typename' or 'class' in template parameter", keyword_token);
            }
            advance(); // consume 'typename' or 'class'
            
            // Expect identifier (template parameter name)
            if (!peek().is_identifier()) {
                return ParseResult::error("Expected template parameter name", current_token_);
            }
            
            Token param_name_token = peek_info();
            template_param_names.push_back(param_name_token.value());
            advance(); // consume parameter name
            
            // Check for comma (more parameters) or closing '>'
            if (peek() == ","_tok) {
                advance(); // consume comma
            } else if (peek() == ">"_tok) {
                advance(); // consume '>'
                break;
            } else {
                return ParseResult::error("Expected ',' or '>' in template parameter list", current_token_);
            }
        }
    }

    // Parse parameter list (optional) using unified parse_parameter_list (Phase 1)
    std::vector<ASTNode> parameters;
    if (peek() == "("_tok) {
        FlashCpp::ParsedParameterList params;
        auto param_result = parse_parameter_list(params);
        if (param_result.is_error()) {
            return param_result;
        }
        parameters = std::move(params.parameters);
        // Note: params.is_variadic could be used for variadic lambdas (C++14+)
    }

    // Parse optional lambda specifiers (C++20 lambda-specifier-seq)
    // Accepts mutable, constexpr, consteval in any order
    bool is_mutable = false;
    bool lambda_is_constexpr = false;
    bool lambda_is_consteval = false;
    bool parsing_specifiers = true;
    while (parsing_specifiers) {
        if (!is_mutable && peek() == "mutable"_tok) {
            advance();
            is_mutable = true;
        } else if (!lambda_is_constexpr && !lambda_is_consteval && peek() == "constexpr"_tok) {
            advance();
            lambda_is_constexpr = true;
        } else if (!lambda_is_consteval && !lambda_is_constexpr && peek() == "consteval"_tok) {
            advance();
            lambda_is_consteval = true;
        } else {
            parsing_specifiers = false;
        }
    }

    // Parse optional noexcept specifier (C++20)
    bool lambda_is_noexcept = false;
    if (peek() == "noexcept"_tok) {
        advance(); // consume 'noexcept'
        lambda_is_noexcept = true;
        // Handle noexcept(expr) form - evaluate the expression
        if (peek() == "("_tok) {
            advance(); // consume '('
            auto noexcept_expr = parse_expression(MIN_PRECEDENCE, ExpressionContext::Normal);
            if (noexcept_expr.node().has_value()) {
                ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
                eval_ctx.parser = this;
                auto eval_result = ConstExpr::Evaluator::evaluate(*noexcept_expr.node(), eval_ctx);
                if (eval_result.success()) {
                    lambda_is_noexcept = eval_result.as_int() != 0;
                }
            }
            consume(")"_tok);
        }
    }

    // Skip optional requires clause (C++20)
    if (peek() == "requires"_tok) {
        advance(); // consume 'requires'
        // Skip the requires expression/clause
        if (peek() == "("_tok) {
            // requires(expr) form
            advance(); // consume '('
            int paren_depth = 1;
            while (!peek().is_eof() && paren_depth > 0) {
                if (peek() == "("_tok) paren_depth++;
                else if (peek() == ")"_tok) paren_depth--;
                if (paren_depth > 0) advance();
            }
            consume(")"_tok);
        } else {
            // Simple requires constraint expression (e.g., requires SomeConcept<T>)
            // Skip tokens until we reach '->' or '{'
            while (!peek().is_eof() && peek() != "->"_tok && peek() != "{"_tok) {
                advance();
            }
        }
    }

    // Skip C++20 attributes on lambda (e.g., [[nodiscard]])
    skip_cpp_attributes();

    // Parse optional return type (-> type)
    std::optional<ASTNode> return_type;
    if (peek() == "->"_tok) {
        advance(); // consume '->'
        ParseResult type_result = parse_type_specifier();
        if (type_result.is_error()) {
            return type_result;
        }
        return_type = type_result.node();
    }

    // Parse body (must be a compound statement)
    if (peek() != "{"_tok) {
        return ParseResult::error("Expected '{' for lambda body", current_token_);
    }

    // Add parameters and captures to symbol table before parsing body
    gSymbolTable.enter_scope(ScopeType::Block);
    
    // Add captures to symbol table
    for (const auto& capture : captures) {
        if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
            capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
            // Skip 'this' and '*this' captures - they're handled differently
            continue;
        }
        if (capture.kind() == LambdaCaptureNode::CaptureKind::AllByValue ||
            capture.kind() == LambdaCaptureNode::CaptureKind::AllByReference) {
            // Capture-all will be expanded later, skip for now
            continue;
        }
        
        // For regular captures (by value or by reference), add them to the symbol table
        // so they can be referenced in the lambda body
        Token id_token = capture.identifier_token();
        
        // Determine the type for the capture variable
        // For init-captures, we need to get the type from the initializer
        // For regular captures, we look up the original variable
        TypeSpecifierNode capture_type_node(Type::Auto, TypeQualifier::None, 0, id_token);
        
        if (capture.has_initializer()) {
            // Init-capture: [x = expr]
            // Try to deduce the type from the initializer expression
            auto deduced_type_opt = get_expression_type(*capture.initializer());
            if (deduced_type_opt.has_value()) {
                capture_type_node = *deduced_type_opt;
            }
        } else {
            // Regular capture: [x] or [&x]
            // Look up the original variable to get its type
            auto var_symbol = lookup_symbol(id_token.handle());
            if (var_symbol.has_value()) {
                const DeclarationNode* decl = get_decl_from_symbol(*var_symbol);
                if (decl) {
                    capture_type_node = decl->type_node().as<TypeSpecifierNode>();
                }
            }
        }
        
        // Create a DeclarationNode for the capture variable
        auto capture_decl = emplace_node<DeclarationNode>(
            emplace_node<TypeSpecifierNode>(capture_type_node),
            id_token
        );
        
        // Add to symbol table
        gSymbolTable.insert(id_token.value(), capture_decl);
    }
    
    // Add parameters to symbol table
    for (const auto& param : parameters) {
        if (param.is<DeclarationNode>()) {
            const auto& decl = param.as<DeclarationNode>();
            gSymbolTable.insert(decl.identifier_token().value(), param);
        }
    }

    ParseResult body_result = parse_block();

    // Remove parameters from symbol table after parsing body
    gSymbolTable.exit_scope();

    if (body_result.is_error()) {
        return body_result;
    }

    // Deduce lambda return type if not explicitly specified or if it's auto
    // Now with proper guard against circular dependencies in get_expression_type
    // AND validation that all return paths return the same type
    if (!return_type.has_value() || 
        (return_type->is<TypeSpecifierNode>() && return_type->as<TypeSpecifierNode>().type() == Type::Auto)) {
        // Search lambda body for return statements to deduce return type
        [[maybe_unused]] const BlockNode& body = body_result.node()->as<BlockNode>();
        std::optional<TypeSpecifierNode> deduced_type;
        std::vector<std::pair<TypeSpecifierNode, Token>> all_return_types;  // Track all return types for validation
        
        // Recursive lambda to search for return statements in lambda body
        std::function<void(const ASTNode&)> find_return_in_lambda = [&](const ASTNode& node) {
            if (node.is<ReturnStatementNode>()) {
                const ReturnStatementNode& ret = node.as<ReturnStatementNode>();
                if (ret.expression().has_value()) {
                    // Try to get the type using get_expression_type
                    // The guard in get_expression_type will prevent infinite recursion
                    auto expr_type_opt = get_expression_type(*ret.expression());
                    if (expr_type_opt.has_value()) {
                        // Store this return type for validation
                        all_return_types.emplace_back(*expr_type_opt, lambda_token);
                        
                        FLASH_LOG(Parser, Debug, "Lambda found return statement #", all_return_types.size(), 
                                 " with type=", (int)expr_type_opt->type(), " size=", (int)expr_type_opt->size_in_bits());
                        
                        // Set the deduced type from the first return statement
                        if (!deduced_type.has_value()) {
                            deduced_type = *expr_type_opt;
                            FLASH_LOG(Parser, Debug, "Lambda return type deduced from expression: type=", 
                                     (int)deduced_type->type(), " size=", (int)deduced_type->size_in_bits());
                        }
                    } else {
                        // If we couldn't deduce (possibly due to circular dependency guard),
                        // default to int as a safe fallback
                        if (!deduced_type.has_value()) {
                            deduced_type = TypeSpecifierNode(Type::Int, TypeQualifier::None, 32);
                            all_return_types.emplace_back(*deduced_type, lambda_token);
                            FLASH_LOG(Parser, Debug, "Lambda return type defaulted to int (type resolution failed)");
                        }
                    }
                }
            } else if (node.is<BlockNode>()) {
                // Recursively search nested blocks
                const BlockNode& block = node.as<BlockNode>();
                const auto& stmts = block.get_statements();
                for (size_t i = 0; i < stmts.size(); ++i) {
                    find_return_in_lambda(stmts[i]);
                }
            } else if (node.is<IfStatementNode>()) {
                const IfStatementNode& if_stmt = node.as<IfStatementNode>();
                find_return_in_lambda(if_stmt.get_then_statement());
                if (if_stmt.has_else()) {
                    find_return_in_lambda(*if_stmt.get_else_statement());
                }
            } else if (node.is<WhileStatementNode>()) {
                const WhileStatementNode& while_stmt = node.as<WhileStatementNode>();
                find_return_in_lambda(while_stmt.get_body_statement());
            } else if (node.is<ForStatementNode>()) {
                const ForStatementNode& for_stmt = node.as<ForStatementNode>();
                find_return_in_lambda(for_stmt.get_body_statement());
            } else if (node.is<DoWhileStatementNode>()) {
                const DoWhileStatementNode& do_while = node.as<DoWhileStatementNode>();
                if (do_while.get_body_statement().has_value()) {
                    find_return_in_lambda(do_while.get_body_statement());
                }
            } else if (node.is<SwitchStatementNode>()) {
                const SwitchStatementNode& switch_stmt = node.as<SwitchStatementNode>();
                if (switch_stmt.get_body().has_value()) {
                    find_return_in_lambda(switch_stmt.get_body());
                }
            }
        };
        
        // Search the lambda body
        find_return_in_lambda(*body_result.node());
        
        // Validate that all return statements have compatible types
        if (all_return_types.size() > 1) {
            const TypeSpecifierNode& first_type = all_return_types[0].first;
            for (size_t i = 1; i < all_return_types.size(); ++i) {
                const TypeSpecifierNode& current_type = all_return_types[i].first;
                if (!are_types_compatible(first_type, current_type)) {
                    // Build error message showing the conflicting types
                    std::string error_msg = "Lambda has inconsistent return types: ";
                    error_msg += "first return has type '";
                    error_msg += type_to_string(first_type);
                    error_msg += "', but another return has type '";
                    error_msg += type_to_string(current_type);
                    error_msg += "'";
                    
                    FLASH_LOG(Parser, Error, error_msg);
                    return ParseResult::error(error_msg, all_return_types[i].second);
                }
            }
        }
        
        // If we found a deduced type, use it; otherwise default to void
        if (deduced_type.has_value()) {
            return_type = emplace_node<TypeSpecifierNode>(*deduced_type);
            FLASH_LOG(Parser, Debug, "Lambda auto return type deduced: type=", (int)deduced_type->type());
        } else {
            // No return statement found or return with no value - lambda returns void
            return_type = emplace_node<TypeSpecifierNode>(Type::Void, TypeQualifier::None, 0);
            FLASH_LOG(Parser, Debug, "Lambda has no return or returns void");
        }
    }

    // Expand capture-all before creating the lambda node
    std::vector<LambdaCaptureNode> expanded_captures;
    std::vector<ASTNode> captured_var_decls_for_all;  // Store declarations for capture-all
    bool has_capture_all = false;
    LambdaCaptureNode::CaptureKind capture_all_kind = LambdaCaptureNode::CaptureKind::ByValue;

    for (const auto& capture : captures) {
        if (capture.is_capture_all()) {
            has_capture_all = true;
            capture_all_kind = capture.kind();
        } else {
            expanded_captures.push_back(capture);
        }
    }

    if (has_capture_all) {
        // Find all identifiers referenced in the lambda body
        std::unordered_set<StringHandle> referenced_vars;
        findReferencedIdentifiers(*body_result.node(), referenced_vars);

        // Build a set of parameter names to exclude from captures
        std::unordered_set<StringHandle> param_names;
        for (const auto& param : parameters) {
            if (param.is<DeclarationNode>()) {
                param_names.insert(param.as<DeclarationNode>().identifier_token().handle());
            }
        }

        // Build a set of local variable names declared inside the lambda body
        std::unordered_set<StringHandle> local_vars;
        findLocalVariableDeclarations(*body_result.node(), local_vars);

        // Convert capture-all kind to specific capture kind
        LambdaCaptureNode::CaptureKind specific_kind =
            (capture_all_kind == LambdaCaptureNode::CaptureKind::AllByValue)
            ? LambdaCaptureNode::CaptureKind::ByValue
            : LambdaCaptureNode::CaptureKind::ByReference;

        // For each referenced variable, check if it's a non-local variable
        for (const auto& var_name : referenced_vars) {
            // Skip empty names or placeholders
			if (!var_name.isValid() || var_name.view() == "_"sv) {
                continue;
            }

            // Skip if it's a parameter
            if (param_names.find(var_name) != param_names.end()) {
                continue;
            }

            // Skip if it's a local variable declared inside the lambda
            if (local_vars.find(var_name) != local_vars.end()) {
                continue;
            }

            // Look up the variable in the symbol table
            // At this point, we're after the lambda body scope was exited,
            // so any variable found in the symbol table is from an outer scope
            std::optional<ASTNode> var_symbol = lookup_symbol(var_name);
            if (var_symbol.has_value()) {
                // Check if this is a variable (not a function or type)
                // Variables are stored as DeclarationNode or VariableDeclarationNode in the symbol table
                if (const DeclarationNode* decl = get_decl_from_symbol(*var_symbol)) {
                    // Check if this variable is already explicitly captured
                    bool already_captured = false;
                    for (const auto& existing_capture : expanded_captures) {
                        if (existing_capture.identifier_name() == var_name) {
                            already_captured = true;
                            break;
                        }
                    }

                    if (!already_captured) {
                        // Create a capture node for this variable with SPECIFIC kind (not AllByValue/AllByReference)
                        // Use the identifier token from the declaration to ensure stable string_view
                        Token var_token = decl->identifier_token();
                        expanded_captures.emplace_back(specific_kind, var_token);  // Use ByValue or ByReference, not AllByValue/AllByReference
                        // Store the declaration for later use
                        captured_var_decls_for_all.push_back(*var_symbol);
                    }
                }
            }
        }
    }

    auto lambda_node = emplace_node<LambdaExpressionNode>(
        std::move(expanded_captures),
        std::move(parameters),
        *body_result.node(),
        return_type,
        lambda_token,
        is_mutable,
        std::move(template_param_names),
        lambda_is_noexcept,
        lambda_is_constexpr,
        lambda_is_consteval
    );

    // Register the lambda closure type in the type system immediately
    // This allows auto type deduction to work
    const auto& lambda = lambda_node.as<LambdaExpressionNode>();
    auto closure_name = lambda.generate_lambda_name();

    // Get captures from the lambda node (since we moved them above)
    const auto& lambda_captures = lambda.captures();

    TypeInfo& closure_type = add_struct_type(closure_name);
    auto closure_struct_info = std::make_unique<StructTypeInfo>(closure_name, AccessSpecifier::Public);

    // For non-capturing lambdas, create a 1-byte struct (like Clang does)
    if (lambda_captures.empty()) {
        closure_struct_info->total_size = 1;
        closure_struct_info->alignment = 1;
    } else {
        // Add captured variables as members to the closure struct
        for (const auto& capture : lambda_captures) {
            if (capture.is_capture_all()) {
                // Capture-all should have been expanded before this point
                continue;
            }
            
            // Handle [this] capture
            if (capture.kind() == LambdaCaptureNode::CaptureKind::This) {
                // [this] capture: store a pointer to the enclosing object (8 bytes on x64)
                // We'll store it with a special member name so it can be accessed later
                TypeSpecifierNode ptr_type(Type::Void, TypeQualifier::None, 64);
                ptr_type.add_pointer_level();  // Make it a void*
                
                // Phase 7B: Intern special member name and use StringHandle overload
                StringHandle this_member_handle = StringTable::getOrInternStringHandle("__this");
                closure_struct_info->addMember(
                    this_member_handle,  // Special member name for captured this
                    Type::Void,         // Base type (will be treated as pointer)
                    0,                  // No type index
                    8,                  // Pointer size on x64
                    8,                  // Alignment
                    AccessSpecifier::Public,
                    std::nullopt,       // No initializer
                    false,              // Not a reference
                    false,              // Not rvalue reference
                    64                  // Size in bits
                );
                continue;  // Skip the rest of processing for this capture
            }
            
            // Handle [*this] capture (C++17)
            if (capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
                // [*this] capture: store a copy of the entire enclosing object
                // We need to determine the size of the enclosing struct
                if (!member_function_context_stack_.empty()) {
                    const auto& context = member_function_context_stack_.back();
                    StringHandle struct_name = context.struct_name;
                    auto type_it = gTypesByName.find(struct_name);
                    if (type_it != gTypesByName.end()) {
                        const TypeInfo* enclosing_type = type_it->second;
                        const StructTypeInfo* enclosing_struct = enclosing_type->getStructInfo();
                        if (enclosing_struct) {
                            StringHandle copy_this_member_handle = StringTable::getOrInternStringHandle("__copy_this");
                            closure_struct_info->addMember(
                                copy_this_member_handle,            // Special member name for copied this
                                Type::Struct,                       // Struct type
                                enclosing_type->type_index_,        // Type index of enclosing struct
                                enclosing_struct->total_size,       // Size of the entire struct
                                enclosing_struct->alignment,        // Alignment from enclosing struct
                                AccessSpecifier::Public,
                                std::nullopt,                       // No initializer
                                false,                              // Not a reference
                                false,                              // Not rvalue reference
                                enclosing_struct->total_size * 8    // Size in bits
                            );
                        }
                    }
                }
                continue;  // Skip the rest of processing for this capture
            }

            auto var_name = StringTable::getOrInternStringHandle(capture.identifier_name());
            TypeSpecifierNode var_type(Type::Int, TypeQualifier::None, 32);  // Default type
            
            if (capture.has_initializer()) {
                // Init-capture: type is inferred from the initializer
                // For now, use simple type inference based on the initializer
                const auto& init_expr = *capture.initializer();
                
                // Try to infer type from the initializer expression
                if (init_expr.is<NumericLiteralNode>()) {
                    var_type = TypeSpecifierNode(Type::Int, TypeQualifier::None, 32);
                } else if (init_expr.is<IdentifierNode>()) {
                    // Look up the identifier's type
                    auto init_id = init_expr.as<IdentifierNode>().nameHandle();
                    auto init_symbol = lookup_symbol(init_id);
                    if (init_symbol.has_value()) {
                        const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
                        if (init_decl) {
                            var_type = init_decl->type_node().as<TypeSpecifierNode>();
                        }
                    }
                } else if (init_expr.is<ExpressionNode>()) {
                    // For expressions, try to get the type from a binary operation or other expr
                    const auto& expr_node = init_expr.as<ExpressionNode>();
                    if (std::holds_alternative<BinaryOperatorNode>(expr_node)) {
                        // For binary operations, assume int type for arithmetic
                        var_type = TypeSpecifierNode(Type::Int, TypeQualifier::None, 32);
                    } else if (std::holds_alternative<IdentifierNode>(expr_node)) {
                        auto init_id = std::get<IdentifierNode>(expr_node).nameHandle();
                        auto init_symbol = lookup_symbol(init_id);
                        if (init_symbol.has_value()) {
                            const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
                            if (init_decl) {
                                var_type = init_decl->type_node().as<TypeSpecifierNode>();
                            }
                        }
                    }
                }
                // For other expression types, we'll use the default int type
            } else {
                // Regular capture: look up the variable in the current scope
                std::optional<ASTNode> var_symbol = lookup_symbol(var_name);
                
                if (!var_symbol.has_value()) {
                    continue;
                }
                
                const DeclarationNode* var_decl = get_decl_from_symbol(*var_symbol);
                if (!var_decl) {
                    continue;
                }
                
                var_type = var_decl->type_node().as<TypeSpecifierNode>();
            }

            // Determine size and alignment based on capture kind
            size_t member_size;
            size_t member_alignment;
            Type member_type;
            TypeIndex type_index = 0;

			if (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference) {
				// By-reference capture: store a pointer (8 bytes on x64)
				// We store the base type (e.g., Int) but the member will be accessed as a pointer
				member_size = 8;
				member_alignment = 8;
				member_type = var_type.type();
				if (var_type.type() == Type::Struct) {
					type_index = var_type.type_index();
				}
			} else {
                // By-value capture: store the actual value
                member_size = var_type.size_in_bits() / 8;
                member_alignment = member_size;  // Simple alignment = size
                member_type = var_type.type();
                if (var_type.type() == Type::Struct) {
                    type_index = var_type.type_index();
                }
            }

			size_t referenced_size_bits = member_size * 8;
			bool is_ref_capture = (capture.kind() == LambdaCaptureNode::CaptureKind::ByReference);
			if (is_ref_capture) {
				referenced_size_bits = var_type.size_in_bits();
				if (referenced_size_bits == 0 && var_type.type() == Type::Struct) {
					const TypeInfo* member_type_info = nullptr;
					for (const auto& ti : gTypeInfo) {
						if (ti.type_index_ == var_type.type_index()) {
							member_type_info = &ti;
							break;
						}
					}
					if (member_type_info && member_type_info->getStructInfo()) {
						referenced_size_bits = static_cast<size_t>(member_type_info->getStructInfo()->total_size * 8);
					}
				}
			}

			closure_struct_info->addMember(
				var_name,
				member_type,
				type_index,
				member_size,
				member_alignment,
				AccessSpecifier::Public,
				std::nullopt,
				is_ref_capture,
				false,
				referenced_size_bits
			);
        }

        // addMember() already updates total_size and alignment, but ensure minimum size of 1
        if (closure_struct_info->total_size == 0) {
            closure_struct_info->total_size = 1;
        }
    }

    // Generate operator() member function for the lambda
    // This allows lambda() calls to work
    // Determine return type
    TypeSpecifierNode return_type_spec(Type::Int, TypeQualifier::None, 32);
    if (return_type.has_value()) {
        return_type_spec = return_type->as<TypeSpecifierNode>();
    }

    // Create operator() declaration
    DeclarationNode& operator_call_decl = emplace_node<DeclarationNode>(
        emplace_node<TypeSpecifierNode>(return_type_spec),
        Token(Token::Type::Identifier, "operator()"sv, lambda_token.line(), lambda_token.column(), lambda_token.file_index())
    ).as<DeclarationNode>();

    // Create FunctionDeclarationNode for operator()
    ASTNode operator_call_func_node = emplace_node<FunctionDeclarationNode>(
        operator_call_decl,
        closure_name
    );
    FunctionDeclarationNode& operator_call_func = operator_call_func_node.as<FunctionDeclarationNode>();

    // Add parameters from lambda to operator()
    for (const auto& param : lambda.parameters()) {
        operator_call_func.add_parameter_node(param);
    }

    // Add operator() as a member function
    StructMemberFunction operator_call_member(
        StringTable::getOrInternStringHandle("operator()"),
        operator_call_func_node,  // Use the original ASTNode, not a copy
        AccessSpecifier::Public,
        false,  // not constructor
        false,  // not destructor
        true,   // is operator overload
        "()"    // operator symbol
    );

    closure_struct_info->member_functions.push_back(operator_call_member);

    closure_type.struct_info_ = std::move(closure_struct_info);

    // Wrap the lambda in an ExpressionNode before returning
    ExpressionNode expr_node = lambda_node.as<LambdaExpressionNode>();
    return ParseResult::success(emplace_node<ExpressionNode>(std::move(expr_node)));
}

ParseResult Parser::parse_if_statement() {
    if (!consume("if"_tok)) {
        return ParseResult::error("Expected 'if' keyword", current_token_);
    }

    // Check for C++17 'if constexpr'
    bool is_constexpr = false;
    if (peek() == "constexpr"_tok) {
        consume("constexpr"_tok);
        is_constexpr = true;
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'if'", current_token_);
    }

    // Unified declaration handling for if-statements:
    // 1. C++17 if-with-initializer: if (Type var = expr; condition)
    // 2. C++ declaration-as-condition: if (Type var = expr)
    // Both start with a type followed by a variable declaration.
    // We try parse_variable_declaration() once and check the delimiter:
    //   ';' → init-statement, then parse the condition expression separately
    //   ')' → declaration-as-condition
    //   otherwise → not a declaration, fall back to expression parsing
    std::optional<ASTNode> init_statement;
    std::optional<FlashCpp::SymbolTableScope> if_scope;
    ParseResult condition;
    bool condition_parsed = false;

    // Determine if the next tokens could be a declaration (keyword type or identifier type)
    bool try_declaration = false;
    if (peek().is_keyword() && type_keywords.find(peek_info().value()) != type_keywords.end()) {
        try_declaration = true;
    } else if (peek().is_identifier()) {
        // Lookahead: check for "Type name =" pattern where Type can be qualified (ns::Type)
        // This avoids misinterpreting simple "if (x)" as a declaration
        auto lookahead = save_token_position();
        advance(); // skip potential type name
        // Skip qualified name components: ns::inner::Type
        while (peek() == "::"_tok) {
            advance(); // skip '::'
            if (peek().is_identifier()) {
                advance(); // skip next component
            }
        }
        if (peek() == "<"_tok) {
            skip_template_arguments();
        }
        while (peek() == "*"_tok || peek() == "&"_tok || peek() == "&&"_tok) {
            advance();
        }
        if (peek().is_identifier()) {
            advance(); // skip potential variable name
            if (peek() == "="_tok || peek() == "{"_tok) {
                try_declaration = true;
            }
        }
        restore_token_position(lookahead);
    }

    if (try_declaration) {
        auto checkpoint = save_token_position();
        if_scope.emplace(ScopeType::Block);

        ParseResult potential_decl = parse_variable_declaration();

        if (!potential_decl.is_error() && peek() == ";"_tok) {
            // Init-statement: if (Type var = expr; condition)
            discard_saved_token(checkpoint);
            init_statement = potential_decl.node();
            if (!consume(";"_tok)) {
                return ParseResult::error("Expected ';' after if initializer", current_token_);
            }
        } else if (!potential_decl.is_error() && peek() == ")"_tok) {
            // Declaration-as-condition: if (Type var = expr)
            discard_saved_token(checkpoint);
            condition = potential_decl;
            condition_parsed = true;
        } else {
            // Not a declaration - undo scope (reset calls exit_scope) and restore tokens
            if_scope.reset();
            restore_token_position(checkpoint);
        }
    }

    // Parse condition as expression if not already set by declaration-as-condition
    if (!condition_parsed) {
        condition = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    }
    if (condition.is_error()) {
        return condition;
    }

    if (!consume(")"_tok)) {
        return ParseResult::error("Expected ')' after if condition", current_token_);
    }

    // Skip C++20 [[likely]]/[[unlikely]] attributes on if branches
    skip_cpp_attributes();

    // For if constexpr during template body re-parsing with parameter packs,
    // evaluate the condition at compile time and skip the dead branch
    // (which may contain ill-formed code like unexpanded parameter packs)
    if (is_constexpr && has_parameter_packs_ && condition.node().has_value()) {
        ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
        eval_ctx.parser = this;
        auto eval_result = ConstExpr::Evaluator::evaluate(*condition.node(), eval_ctx);
        if (eval_result.success()) {
            bool condition_value = eval_result.as_int() != 0;
            FLASH_LOG(Templates, Debug, "if constexpr condition evaluated to ", condition_value ? "true" : "false", " during template body re-parse");
            
            if (condition_value) {
                // Parse the then-branch normally
                ParseResult then_stmt_result;
                if (peek() == "{"_tok) {
                    then_stmt_result = parse_block();
                } else {
                    then_stmt_result = parse_statement_or_declaration();
                    consume(";"_tok);
                }
                // Skip the else-branch if present
                if (peek() == "else"_tok) {
                    advance(); // consume 'else'
                    skip_cpp_attributes(); // Skip [[likely]]/[[unlikely]] after else
                    // Recursively skip the else branch, which may be:
                    // 1. A block: else { ... }
                    // 2. An else-if chain: else if (...) { ... } else ...
                    // 3. A single statement: else return x;
                    while (true) {
                        if (peek() == "{"_tok) {
                            skip_balanced_braces();
                            break;
                        } else if (peek() == "if"_tok) {
                            advance(); // consume 'if'
                            if (peek() == "constexpr"_tok) advance();
                            skip_balanced_parens(); // skip condition
                            skip_cpp_attributes(); // Skip [[likely]]/[[unlikely]] after if condition
                            // Skip then-branch (block or statement)
                            if (peek() == "{"_tok) {
                                skip_balanced_braces();
                            } else {
                                while (!peek().is_eof() && peek() != ";"_tok) advance();
                                consume(";"_tok);
                            }
                            // Continue loop to handle else/else-if after this branch
                            if (peek() == "else"_tok) {
                                advance(); // consume 'else'
                                skip_cpp_attributes(); // Skip [[likely]]/[[unlikely]] after inner else
                                continue; // loop handles next branch
                            }
                            break;
                        } else {
                            // Single statement else - skip to semicolon
                            while (!peek().is_eof() && peek() != ";"_tok) advance();
                            consume(";"_tok);
                            break;
                        }
                    }
                }
                // Return just the then-branch content
                return then_stmt_result;
            } else {
                // Skip the then-branch
                if (peek() == "{"_tok) {
                    skip_balanced_braces();
                } else {
                    while (!peek().is_eof() && peek() != ";"_tok) advance();
                    consume(";"_tok);
                }
                // Parse the else-branch if present
                if (peek() == "else"_tok) {
                    consume("else"_tok);
                    skip_cpp_attributes(); // Skip [[likely]]/[[unlikely]] after else
                    ParseResult else_result;
                    if (peek() == "{"_tok) {
                        else_result = parse_block();
                    } else if (peek() == "if"_tok) {
                        else_result = parse_if_statement();
                    } else {
                        else_result = parse_statement_or_declaration();
                        consume(";"_tok);
                    }
                    if (!else_result.is_error() && else_result.node().has_value()) {
                        return else_result;
                    }
                    return else_result;  // Propagate the error
                }
                // No else branch and condition is false - return empty block
                return ParseResult::success(emplace_node<BlockNode>());
            }
        }
    }

    // Parse then-statement (can be a block or a single statement)
    ParseResult then_stmt;
    if (peek() == "{"_tok) {
        then_stmt = parse_block();
    } else {
        then_stmt = parse_statement_or_declaration();
        // Consume trailing semicolon if present (expression statements don't consume their ';')
        consume(";"_tok);
    }

    if (then_stmt.is_error()) {
        return then_stmt;
    }

    // Check for else clause
    std::optional<ASTNode> else_stmt;
    if (peek() == "else"_tok) {
        consume("else"_tok);

        // Skip C++20 [[likely]]/[[unlikely]] attributes on else branches
        skip_cpp_attributes();

        // Parse else-statement (can be a block, another if, or a single statement)
        ParseResult else_result;
        if (peek() == "{"_tok) {
            else_result = parse_block();
        } else if (peek() == "if"_tok) {
            // Handle else-if chain
            else_result = parse_if_statement();
        } else {
            else_result = parse_statement_or_declaration();
            // Consume trailing semicolon if present
            consume(";"_tok);
        }

        if (else_result.is_error()) {
            return else_result;
        }
        else_stmt = else_result.node();
    }

    // Create if statement node
    if (auto cond_node = condition.node()) {
        if (auto then_node = then_stmt.node()) {
            return ParseResult::success(emplace_node<IfStatementNode>(
                *cond_node, *then_node, else_stmt, init_statement, is_constexpr
            ));
        }
    }

    return ParseResult::error("Invalid if statement construction", current_token_);
}

ParseResult Parser::parse_switch_statement() {
    if (!consume("switch"_tok)) {
        return ParseResult::error("Expected 'switch' keyword", current_token_);
    }

    if (!consume("("_tok)) {
        return ParseResult::error("Expected '(' after 'switch'", current_token_);
    }

    // Parse the switch condition expression
    auto condition = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
    if (condition.is_error()) {
        return condition;
    }

    if (!consume(")"_tok)) {
        return ParseResult::error("Expected ')' after switch condition", current_token_);
    }

    // Parse the switch body (must be a compound statement with braces)
    if (!consume("{"_tok)) {
        return ParseResult::error("Expected '{' for switch body", current_token_);
    }

    // Create a block to hold case/default labels and their statements
    auto [block_node, block_ref] = create_node_ref(BlockNode());

    // Parse case and default labels
    while (!peek().is_eof() && peek() != "}"_tok) {
        auto current = peek_info();

        if (current.type() == Token::Type::Keyword && current.value() == "case") {
            // Parse case label
            advance(); // consume 'case'

            // Parse case value (must be a constant expression)
            auto case_value = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
            if (case_value.is_error()) {
                return case_value;
            }

            if (!consume(":"_tok)) {
                return ParseResult::error("Expected ':' after case value", current_token_);
            }

            // Skip C++20 [[likely]]/[[unlikely]] attributes after case label
            skip_cpp_attributes();

            // Parse statements until next case/default/closing brace
            // We collect all statements for this case into a sub-block
            auto [case_block_node, case_block_ref] = create_node_ref(BlockNode());

            while (!peek().is_eof() &&
                   peek() != "}"_tok &&
                   !(peek().is_keyword() &&
                     (peek() == "case"_tok || peek() == "default"_tok))) {
                // Skip stray semicolons (empty statements)
                if (peek().is_punctuator() && peek() == ";"_tok) {
                    advance();
                    continue;
                }

                auto stmt = parse_statement_or_declaration();
                if (stmt.is_error()) {
                    return stmt;
                }
                if (auto stmt_node = stmt.node()) {
                    case_block_ref.add_statement_node(*stmt_node);
                }
            }

            // Create case label node with the block of statements
            auto case_label = emplace_node<CaseLabelNode>(*case_value.node(), case_block_node);
            block_ref.add_statement_node(case_label);

        } else if (current.type() == Token::Type::Keyword && current.value() == "default") {
            // Parse default label
            advance(); // consume 'default'

            if (!consume(":"_tok)) {
                return ParseResult::error("Expected ':' after 'default'", current_token_);
            }

            // Skip C++20 [[likely]]/[[unlikely]] attributes after default label
            skip_cpp_attributes();

            // Parse statements until next case/default/closing brace
            auto [default_block_node, default_block_ref] = create_node_ref(BlockNode());

            while (!peek().is_eof() &&
                   peek() != "}"_tok &&
                   !(peek().is_keyword() &&
                     (peek() == "case"_tok || peek() == "default"_tok))) {
                // Skip stray semicolons (empty statements)
                if (peek().is_punctuator() && peek() == ";"_tok) {
                    advance();
                    continue;
                }

                auto stmt = parse_statement_or_declaration();
                if (stmt.is_error()) {
                    return stmt;
                }
                if (auto stmt_node = stmt.node()) {
                    default_block_ref.add_statement_node(*stmt_node);
                }
            }

            // Create default label node with the block of statements
            auto default_label = emplace_node<DefaultLabelNode>(default_block_node);
            block_ref.add_statement_node(default_label);

        } else {
            // If we're here, we have an unexpected token at the switch body level
            std::string error_msg = "Expected 'case' or 'default' in switch body, but found: ";
            if (current.type() == Token::Type::Keyword) {
                error_msg += "keyword '" + std::string(current.value()) + "'";
            } else if (current.type() == Token::Type::Identifier) {
                error_msg += "identifier '" + std::string(current.value()) + "'";
            } else {
                error_msg += "'" + std::string(current.value()) + "'";
            }
            return ParseResult::error(error_msg, current_token_);
        }
    }

    if (!consume("}"_tok)) {
        return ParseResult::error("Expected '}' to close switch body", current_token_);
    }

    // Create switch statement node
    if (auto cond_node = condition.node()) {
        return ParseResult::success(emplace_node<SwitchStatementNode>(*cond_node, block_node));
    }

    return ParseResult::error("Invalid switch statement construction", current_token_);
}

ParseResult Parser::parse_qualified_identifier() {
	// This method parses qualified identifiers like std::print or ns1::ns2::func
	// It should be called when we've already seen an identifier followed by ::

	std::vector<StringType<>> namespaces;
	Token final_identifier;

	// We should already be at an identifier
	auto first_token = peek_info();
	if (first_token.type() != Token::Type::Identifier) {
		return ParseResult::error("Expected identifier in qualified name", first_token);
	}

	// Collect namespace parts
	while (true) {
		auto identifier_token = advance();
		if (identifier_token.type() != Token::Type::Identifier) {
			return ParseResult::error("Expected identifier", identifier_token);
		}

		// Check if followed by ::
		if (peek() == "::"_tok) {
			// This is a namespace part
			namespaces.emplace_back(StringType<>(identifier_token.value()));
			advance(); // consume ::
		} else {
			// This is the final identifier
			final_identifier = identifier_token;
			break;
		}
	}

	// Create a QualifiedIdentifierNode
	NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
	auto qualified_node = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
	return ParseResult::success(qualified_node);
}

// Helper: Parse template brace initialization: Template<Args>{}
// Parses the brace initializer, looks up the instantiated type, and creates a ConstructorCallNode
ParseResult Parser::parse_template_brace_initialization(
        const std::vector<TemplateTypeArg>& template_args,
        std::string_view template_name,
        const Token& identifier_token) {
	
	// Build the instantiated type name
	std::string_view instantiated_name = get_instantiated_class_name(template_name, template_args);
	
	// Look up the instantiated type
	auto type_handle = StringTable::getOrInternStringHandle(instantiated_name);
	auto type_it = gTypesByName.find(type_handle);
	if (type_it == gTypesByName.end()) {
		// Type not found with provided args - try filling in default template arguments
		auto template_lookup = gTemplateRegistry.lookupTemplate(template_name);
		if (template_lookup.has_value() && template_lookup->is<TemplateClassDeclarationNode>()) {
			const auto& template_class = template_lookup->as<TemplateClassDeclarationNode>();
			const auto& template_params = template_class.template_parameters();
			if (template_args.size() < template_params.size()) {
				std::vector<TemplateTypeArg> filled_args = template_args;
				for (size_t i = filled_args.size(); i < template_params.size(); ++i) {
					const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
					if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
						const ASTNode& default_node = param.default_value();
						if (default_node.is<TypeSpecifierNode>()) {
							filled_args.push_back(TemplateTypeArg(default_node.as<TypeSpecifierNode>()));
						}
					}
				}
				if (filled_args.size() > template_args.size()) {
					instantiated_name = get_instantiated_class_name(template_name, filled_args);
					type_handle = StringTable::getOrInternStringHandle(instantiated_name);
					type_it = gTypesByName.find(type_handle);
				}
			}
		}
		if (type_it == gTypesByName.end()) {
			// Type not found - instantiation may have failed
			return ParseResult::error("Template instantiation failed or type not found", identifier_token);
		}
	}
	
	// Determine which token checking method to use based on what token is '{'
	// If current_token_ is '{', we use current_token_ style checking
	// Otherwise, we use peek_token() style checking
	bool use_current_token = current_token_.value() == "{";
	
	// Consume the opening '{'
	if (use_current_token) {
		advance(); // consume '{'
	} else if (peek() == "{"_tok) {
		advance(); // consume '{'
	} else {
		return ParseResult::error("Expected '{' for brace initialization", identifier_token);
	}
	
	// Parse arguments inside braces
	ChunkedVector<ASTNode> args;
	while (true) {
		// Check for closing brace
		bool at_close = use_current_token 
			? (current_token_.value() == "}")
			: (peek() == "}"_tok);
		
		if (at_close) {
			break;
		}
		
		// Parse argument expression
		auto argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (argResult.is_error()) {
			return argResult;
		}
		if (auto node = argResult.node()) {
			args.push_back(*node);
		}
		
		// Check for comma or closing brace
		bool has_comma = use_current_token
			? (current_token_.value() == ",")
			: (peek() == ","_tok);
		
		bool has_close = use_current_token
			? (current_token_.value() == "}")
			: (peek() == "}"_tok);
		
		if (has_comma) {
			advance(); // consume ','
		} else if (!has_close) {
			return ParseResult::error("Expected ',' or '}' in brace initializer", current_token_);
		}
	}
	
	// Consume the closing '}'
	if (use_current_token) {
		if (current_token_.kind().is_eof() || current_token_.value() != "}") {
			return ParseResult::error("Expected '}' after brace initializer", current_token_);
		}
		advance();
	} else {
		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' after brace initializer", current_token_);
		}
	}
	
	// Create TypeSpecifierNode for the instantiated class
	const TypeInfo& type_info = *type_it->second;
	TypeIndex type_index = type_info.type_index_;
	int type_size = 0;
	if (type_info.struct_info_) {
		type_size = static_cast<int>(type_info.struct_info_->total_size * 8);
	}
	Token type_token(Token::Type::Identifier, instantiated_name, 
	                identifier_token.line(), identifier_token.column(), identifier_token.file_index());
	auto type_spec_node = emplace_node<TypeSpecifierNode>(Type::Struct, type_index, type_size, type_token);
	
	// Create ConstructorCallNode
	std::optional<ASTNode> result = emplace_node<ExpressionNode>(ConstructorCallNode(type_spec_node, std::move(args), type_token));
	return ParseResult::success(*result);
}

// Helper: Parse qualified identifier path after template arguments (Template<T>::member)
// Assumes we're positioned right after template arguments and next token is ::
// Returns a QualifiedIdentifierNode wrapped in ExpressionNode if successful
ParseResult Parser::parse_qualified_identifier_after_template(const Token& template_base_token, bool* had_template_keyword) {
	std::vector<StringType<32>> namespaces;
	Token final_identifier = template_base_token;  // Start with the template name
	bool encountered_template_keyword = false;
	
	// Collect the qualified path after ::
	while (peek() == "::"_tok) {
		// Current identifier becomes a namespace part
		namespaces.emplace_back(StringType<32>(final_identifier.value()));
		advance(); // consume ::
		
		// Handle optional 'template' keyword in dependent contexts
		// e.g., typename Base<T>::template member<U>
		if (peek() == "template"_tok) {
			advance(); // consume 'template'
			encountered_template_keyword = true;  // Track that we saw 'template' keyword
		}
		
		// Get next identifier
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected identifier after '::'", peek_info());
		}
		final_identifier = peek_info();
		advance(); // consume the identifier
	}
	
	// Report whether we encountered a 'template' keyword
	if (had_template_keyword) {
		*had_template_keyword = encountered_template_keyword;
	}
	
	// Create a QualifiedIdentifierNode
	NamespaceHandle ns_handle = gSymbolTable.resolve_namespace_handle(namespaces);
	auto qualified_node = emplace_node<QualifiedIdentifierNode>(ns_handle, final_identifier);
	return ParseResult::success(qualified_node);
}

// Helper to parse member template function calls: Template<T>::member<U>()
// This consolidates the logic for parsing member template arguments and function calls
// that appears in multiple places when handling qualified identifiers after template instantiation.
std::optional<ParseResult> Parser::try_parse_member_template_function_call(
	std::string_view instantiated_class_name,
	std::string_view member_name,
	const Token& member_token) {
	
	FLASH_LOG(Templates, Debug, "try_parse_member_template_function_call called for: ", instantiated_class_name, "::", member_name);
	
	// Check for member template arguments: Template<T>::member<U>
	std::optional<std::vector<TemplateTypeArg>> member_template_args;
	if (peek() == "<"_tok) {
		// Before parsing < as template arguments, check if the member is actually a template
		// This prevents misinterpreting patterns like R1<T>::num < R2<T>::num> where < is comparison
		
		// Check if the member is a known template (class or variable template)
		auto member_template_opt = gTemplateRegistry.lookupTemplate(member_name);
		auto member_var_template_opt = gTemplateRegistry.lookupVariableTemplate(member_name);
		
		// Also check with the qualified name (instantiated_class_name::member_name)
		StringBuilder qualified_member_builder;
		qualified_member_builder.append(instantiated_class_name).append("::").append(member_name);
		std::string_view qualified_member_name = qualified_member_builder.preview();
		
		auto qual_template_opt = gTemplateRegistry.lookupTemplate(qualified_member_name);
		auto qual_var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_member_name);
		
		bool is_known_template = member_template_opt.has_value() || member_var_template_opt.has_value() ||
		                         qual_template_opt.has_value() || qual_var_template_opt.has_value();
		
		qualified_member_builder.reset();
		
		if (is_known_template) {
			member_template_args = parse_explicit_template_arguments();
			// If parsing failed, it might be a less-than operator, but that's rare for member access
		} else {
			// Member is NOT a known template - don't parse < as template arguments
			// This handles patterns like integral_constant<bool, R1::num < R2::num>
			FLASH_LOG_FORMAT(Parser, Debug, 
			    "Member '{}' is not a known template - not parsing '<' as template arguments",
			    member_name);
		}
	}
	
	// Check for function call: Template<T>::member() or Template<T>::member<U>()
	if (peek() != "("_tok) {
		return std::nullopt;  // Not a function call
	}
	
	advance(); // consume '('
	
	// Parse function arguments
	ChunkedVector<ASTNode> args;
	while (!peek().is_eof() && peek() != ")"_tok) {
		ParseResult argResult = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (argResult.is_error()) {
			return argResult;
		}
		
		if (argResult.node().has_value()) {
			args.push_back(*argResult.node());
		}
		
		// Check for comma between arguments
		if (peek() == ","_tok) {
			advance(); // consume ','
		} else if (!peek().is_eof() && peek() != ")"_tok) {
			return ParseResult::error("Expected ',' or ')' in function arguments", peek_info());
		}
	}
	
	// Expect closing parenthesis
	if (!consume(")"_tok)) {
		return ParseResult::error("Expected ')' after function arguments", current_token_);
	}
	
	// Try to instantiate the member template function if we have explicit template args
	std::optional<ASTNode> instantiated_func;
	if (member_template_args.has_value() && !member_template_args->empty()) {
		instantiated_func = try_instantiate_member_function_template_explicit(
			instantiated_class_name,
			member_name,
			*member_template_args
		);
	}
	
	// Trigger lazy member function instantiation if needed
	if (!instantiated_func.has_value()) {
		StringHandle class_name_handle = StringTable::getOrInternStringHandle(instantiated_class_name);
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
		FLASH_LOG(Templates, Debug, "Checking lazy instantiation for: ", instantiated_class_name, "::", member_name);
		if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(class_name_handle, member_name_handle)) {
			FLASH_LOG(Templates, Debug, "Lazy instantiation triggered for qualified call: ", instantiated_class_name, "::", member_name);
			auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(class_name_handle, member_name_handle);
			if (lazy_info_opt.has_value()) {
				instantiated_func = instantiateLazyMemberFunction(*lazy_info_opt);
				LazyMemberInstantiationRegistry::getInstance().markInstantiated(class_name_handle, member_name_handle);
			}
		}
		// If the hash-based name didn't match (dependent vs concrete hash mismatch),
		// try to find the correct instantiation by looking up gTypesByName for a matching
		// template instantiation with the same base template name.
		if (!instantiated_func.has_value()) {
			size_t dollar_pos = instantiated_class_name.find('$');
			if (dollar_pos != std::string_view::npos) {
				std::string_view base_tmpl = instantiated_class_name.substr(0, dollar_pos);
				// Search all types to find a matching template instantiation
				for (const auto& [name_handle, type_info_ptr] : gTypesByName) {
					if (type_info_ptr->isTemplateInstantiation() &&
					    StringTable::getStringView(type_info_ptr->baseTemplateName()) == base_tmpl &&
					    StringTable::getStringView(name_handle) != instantiated_class_name) {
						StringHandle alt_class_handle = name_handle;
						if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(alt_class_handle, member_name_handle)) {
							FLASH_LOG(Templates, Debug, "Lazy instantiation triggered via base template match: ", 
							          StringTable::getStringView(alt_class_handle), "::", member_name);
							auto lazy_info_opt2 = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(alt_class_handle, member_name_handle);
							if (lazy_info_opt2.has_value()) {
								instantiated_func = instantiateLazyMemberFunction(*lazy_info_opt2);
								LazyMemberInstantiationRegistry::getInstance().markInstantiated(alt_class_handle, member_name_handle);
								// Update instantiated_class_name to the correct one for mangling
								instantiated_class_name = StringTable::getStringView(alt_class_handle);
								break;
							}
						}
					}
				}
			}
		}
	}
	
	// Build qualified function name including template args
	StringBuilder func_name_builder;
	func_name_builder.append(instantiated_class_name);
	func_name_builder.append("::");
	func_name_builder.append(member_name);
	
	// If member has template args, append them using hash-based naming
	if (member_template_args.has_value() && !member_template_args->empty()) {
		// Generate hash suffix for template args
		auto key = FlashCpp::makeInstantiationKeyV2(StringTable::getOrInternStringHandle(member_name), *member_template_args);
		func_name_builder.append("$");
		auto hash_val = FlashCpp::TemplateInstantiationKeyV2Hash{}(key);
		char hex[17];
		std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash_val));
		func_name_builder.append(std::string_view(hex, 16));
	}
	std::string_view func_name = func_name_builder.commit();
	
	// Create function call token
	Token func_token(Token::Type::Identifier, func_name, 
	                member_token.line(), 
	                member_token.column(),
	                member_token.file_index());
	
	// If we successfully instantiated the function, use its declaration
	const DeclarationNode* decl_ptr = nullptr;
	const FunctionDeclarationNode* func_decl_ptr = nullptr;
	if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
		func_decl_ptr = &instantiated_func->as<FunctionDeclarationNode>();
		decl_ptr = &func_decl_ptr->decl_node();
	} else {
		// For non-template member functions (e.g. Template<T>::allocate()),
		// resolve directly from the instantiated class before creating a fallback decl.
		StringHandle class_name_handle = StringTable::getOrInternStringHandle(instantiated_class_name);
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
		auto type_it = gTypesByName.find(class_name_handle);
		if (type_it != gTypesByName.end() && type_it->second) {
			const StructTypeInfo* struct_info = type_it->second->getStructInfo();
			if (struct_info) {
				const FunctionDeclarationNode* first_name_match = nullptr;
				size_t call_arg_count = args.size();
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

		// Fall back to forward declaration only if we still couldn't resolve.
		if (!decl_ptr) {
			auto type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, func_token);
			auto forward_decl = emplace_node<DeclarationNode>(type_node, func_token);
			decl_ptr = &forward_decl.as<DeclarationNode>();
		}
	}
	
	auto result = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(*decl_ptr), std::move(args), func_token));
	
	// Set the mangled name on the function call if we have the function declaration
	if (func_decl_ptr && func_decl_ptr->has_mangled_name()) {
		std::get<FunctionCallNode>(result.as<ExpressionNode>()).set_mangled_name(func_decl_ptr->mangled_name());
	}
	
	return ParseResult::success(result);
}

std::string Parser::buildPrettyFunctionSignature(const FunctionDeclarationNode& func_node) const {
	StringBuilder result;

	// Get return type from the function's declaration node
	const DeclarationNode& decl = func_node.decl_node();
	const TypeSpecifierNode& ret_type = decl.type_node().as<TypeSpecifierNode>();
	result.append(ret_type.getReadableString()).append(" ");

	// Add namespace prefix if we're in a namespace
	NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
	std::string_view qualified_namespace = gNamespaceRegistry.getQualifiedName(current_handle);
	if (!qualified_namespace.empty()) {
		result.append(qualified_namespace).append("::");
	}

	// Add class/struct prefix if this is a member function
	if (func_node.is_member_function()) {
		result.append(func_node.parent_struct_name()).append("::");
	}

	// Add function name
	result.append(decl.identifier_token().value());

	// Add parameters
	result.append("(");
	const auto& params = func_node.parameter_nodes();
	for (size_t i = 0; i < params.size(); ++i) {
		if (i > 0) result.append(", ");
		const auto& param_decl = params[i].as<DeclarationNode>();
		const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
		result.append(param_type.getReadableString());
	}

	// Add variadic ellipsis if this is a variadic function
	if (func_node.is_variadic()) {
		if (!params.empty()) result.append(", ");
		result.append("...");
	}

	result.append(")");

	return std::string(result.commit());
}

// Check if an identifier name is a template parameter in current scope
bool Parser::is_template_parameter(std::string_view name) const {
    bool result = std::find(template_param_names_.begin(), template_param_names_.end(), name) != template_param_names_.end();
    return result;
}

// Helper: Check if a base class name is a template parameter
// Returns true if the name matches any template parameter in the current template scope
bool Parser::is_base_class_template_parameter(std::string_view base_class_name) const {
	for (const auto& param_name : current_template_param_names_) {
		if (StringTable::getStringView(param_name) == base_class_name) {
			FLASH_LOG_FORMAT(Templates, Debug, 
				"Base class '{}' is a template parameter - deferring resolution", 
				base_class_name);
			return true;
		}
	}
	return false;
}

// Helper: Look up a type alias including inherited ones from base classes
// Searches struct_name::member_name first, then recursively searches base classes
// Uses depth limit to prevent infinite recursion in case of malformed input
const TypeInfo* Parser::lookup_inherited_type_alias(StringHandle struct_name, StringHandle member_name, int depth) {
	// Prevent infinite recursion with a reasonable depth limit
	constexpr int kMaxInheritanceDepth = 100;
	if (depth > kMaxInheritanceDepth) {
		FLASH_LOG_FORMAT(Templates, Warning, "lookup_inherited_type_alias: max depth exceeded for '{}::{}'", 
		                 StringTable::getStringView(struct_name), StringTable::getStringView(member_name));
		return nullptr;
	}
	
	FLASH_LOG_FORMAT(Templates, Debug, "lookup_inherited_type_alias: looking for '{}::{}' ", 
	                 StringTable::getStringView(struct_name), StringTable::getStringView(member_name));
	
	// First try direct lookup with qualified name
	StringBuilder qualified_name_builder;
	qualified_name_builder.append(StringTable::getStringView(struct_name))
	                     .append("::")
	                     .append(StringTable::getStringView(member_name));
	std::string_view qualified_name = qualified_name_builder.commit();
	
	auto direct_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_name));
	if (direct_it != gTypesByName.end()) {
		FLASH_LOG_FORMAT(Templates, Debug, "Found direct type alias '{}'", qualified_name);
		return direct_it->second;
	}
	
	// Not found directly, look up the struct and search its base classes
	auto struct_it = gTypesByName.find(struct_name);
	if (struct_it == gTypesByName.end()) {
		FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' not found in gTypesByName", StringTable::getStringView(struct_name));
		return nullptr;
	}
	
	const TypeInfo* struct_type_info = struct_it->second;
	
	// If this is a type alias (no struct_info_), resolve the underlying type
	if (!struct_type_info->struct_info_) {
		// This might be a type alias - try to find the actual struct type
		// Type aliases have a type_index that points to the underlying type
		// Check if type_index_ is valid and points to a different TypeInfo entry
		if (struct_type_info->type_index_ < gTypeInfo.size()) {
			const TypeInfo& underlying_type = gTypeInfo[struct_type_info->type_index_];
			// Check if this is actually an alias (points to a different TypeInfo)
			// by comparing the pointer addresses
			if (&underlying_type != struct_type_info && underlying_type.struct_info_) {
				StringHandle underlying_name = underlying_type.name();
				FLASH_LOG_FORMAT(Templates, Debug, "Type '{}' is an alias for '{}', following alias", 
				                 StringTable::getStringView(struct_name), StringTable::getStringView(underlying_name));
				return lookup_inherited_type_alias(underlying_name, member_name, depth + 1);
			}
		}
		FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' has no struct_info_ and couldn't resolve alias", StringTable::getStringView(struct_name));
		return nullptr;
	}
	
	// Search base classes recursively
	const StructTypeInfo* struct_info = struct_type_info->struct_info_.get();
	FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' has {} base classes", StringTable::getStringView(struct_name), struct_info->base_classes.size());
	for (const auto& base_class : struct_info->base_classes) {
		// Skip deferred base classes (they haven't been resolved yet)
		if (base_class.is_deferred) {
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping deferred base class '{}'", base_class.name);
			continue;
		}
		
		FLASH_LOG_FORMAT(Templates, Debug, "Checking base class '{}'", base_class.name);
		// Recursively look up in base class - convert base_class.name to StringHandle for performance
		StringHandle base_name_handle = StringTable::getOrInternStringHandle(base_class.name);
		const TypeInfo* base_result = lookup_inherited_type_alias(base_name_handle, member_name, depth + 1);
		if (base_result != nullptr) {
			FLASH_LOG_FORMAT(Templates, Debug, "Found inherited type alias '{}::{}' via base class '{}'",
			                 StringTable::getStringView(struct_name), StringTable::getStringView(member_name), base_class.name);
			return base_result;
		}
	}
	
	return nullptr;
}

// Helper: Look up a template function including inherited ones from base classes
const std::vector<ASTNode>* Parser::lookup_inherited_template(StringHandle struct_name, std::string_view template_name, int depth) {
	// Prevent infinite recursion with a reasonable depth limit
	constexpr int kMaxInheritanceDepth = 100;
	if (depth > kMaxInheritanceDepth) {
		FLASH_LOG_FORMAT(Templates, Warning, "lookup_inherited_template: max depth exceeded for '{}::{}'", 
		                 StringTable::getStringView(struct_name), template_name);
		return nullptr;
	}
	
	FLASH_LOG_FORMAT(Templates, Debug, "lookup_inherited_template: looking for '{}::{}' ", 
	                 StringTable::getStringView(struct_name), template_name);
	
	// First try direct lookup with qualified name (ClassName::functionName)
	StringBuilder qualified_name_builder;
	qualified_name_builder.append(StringTable::getStringView(struct_name))
	                     .append("::")
	                     .append(template_name);
	std::string_view qualified_name = qualified_name_builder.commit();
	
	const std::vector<ASTNode>* direct_templates = gTemplateRegistry.lookupAllTemplates(qualified_name);
	if (direct_templates != nullptr && !direct_templates->empty()) {
		FLASH_LOG_FORMAT(Templates, Debug, "Found direct template function '{}'", qualified_name);
		return direct_templates;
	}
	
	// Not found directly, look up the struct and search its base classes
	auto struct_it = gTypesByName.find(struct_name);
	if (struct_it == gTypesByName.end()) {
		FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' not found in gTypesByName", StringTable::getStringView(struct_name));
		return nullptr;
	}
	
	const TypeInfo* struct_type_info = struct_it->second;
	
	// If this is a type alias (no struct_info_), resolve the underlying type
	if (!struct_type_info->struct_info_) {
		// This might be a type alias - try to find the actual struct type
		// Type aliases have a type_index that points to the underlying type
		// Check if type_index_ is valid and points to a different TypeInfo entry
		if (struct_type_info->type_index_ < gTypeInfo.size()) {
			const TypeInfo& underlying_type = gTypeInfo[struct_type_info->type_index_];
			// Check if this is actually an alias (points to a different TypeInfo)
			// by comparing the pointer addresses
			if (&underlying_type != struct_type_info && underlying_type.struct_info_) {
				StringHandle underlying_name = underlying_type.name();
				FLASH_LOG_FORMAT(Templates, Debug, "Type '{}' is an alias for '{}', following alias", 
				                 StringTable::getStringView(struct_name), StringTable::getStringView(underlying_name));
				return lookup_inherited_template(underlying_name, template_name, depth + 1);
			}
		}
		FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' has no struct_info_ and couldn't resolve alias", StringTable::getStringView(struct_name));
		return nullptr;
	}
	
	// Search base classes recursively
	const StructTypeInfo* struct_info = struct_type_info->struct_info_.get();
	FLASH_LOG_FORMAT(Templates, Debug, "Struct '{}' has {} base classes", StringTable::getStringView(struct_name), struct_info->base_classes.size());
	for (const auto& base_class : struct_info->base_classes) {
		// Skip deferred base classes (they haven't been resolved yet)
		if (base_class.is_deferred) {
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping deferred base class '{}'", base_class.name);
			continue;
		}
		
		FLASH_LOG_FORMAT(Templates, Debug, "Checking base class '{}'", base_class.name);
		// Recursively look up in base class - convert base_class.name to StringHandle for performance
		StringHandle base_name_handle = StringTable::getOrInternStringHandle(base_class.name);
		const std::vector<ASTNode>* base_result = lookup_inherited_template(base_name_handle, template_name, depth + 1);
		if (base_result != nullptr && !base_result->empty()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Found inherited template function '{}::{}' via base class '{}'",
			                 StringTable::getStringView(struct_name), template_name, base_class.name);
			return base_result;
		}
	}
	
	return nullptr;
}

// Helper: Validate and add a base class (consolidates lookup, validation, and registration)
ParseResult Parser::validate_and_add_base_class(
	std::string_view base_class_name,
	StructDeclarationNode& struct_ref,
	StructTypeInfo* struct_info,
	AccessSpecifier base_access,
	bool is_virtual_base,
	const Token& error_token)
{
	// Look up base class type
	auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base_class_name));
	
	// If not found directly, try with current namespace prefix
	// This handles cases like: struct Derived : public inner::Base { }
	// where inner::Base is actually ns::inner::Base and we're inside ns
	if (base_type_it == gTypesByName.end()) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		std::string_view qualified_namespace = gNamespaceRegistry.getQualifiedName(current_handle);
		if (!qualified_namespace.empty()) {
			// Try the full namespace qualification first (e.g., ns::outer::inner::Base).
			StringBuilder qualified_name;
			qualified_name.append(qualified_namespace).append("::").append(base_class_name);
			std::string_view qualified_name_view = qualified_name.commit();
			base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_name_view));
			if (base_type_it != gTypesByName.end()) {
				FLASH_LOG(Parser, Debug, "Found base class '", base_class_name, 
				          "' as '", qualified_name_view, "' in current namespace context");
			}
			
			// Try suffixes like inner::Base, deep::Base for sibling namespace access.
			for (size_t pos = qualified_namespace.find("::");
			     pos != std::string_view::npos && base_type_it == gTypesByName.end();
			     pos = qualified_namespace.find("::", pos + 2)) {
				std::string_view suffix = qualified_namespace.substr(pos + 2);
				StringBuilder suffix_builder;
				suffix_builder.append(suffix).append("::").append(base_class_name);
				qualified_name_view = suffix_builder.commit();
				base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_name_view));
				
				if (base_type_it != gTypesByName.end()) {
					FLASH_LOG(Parser, Debug, "Found base class '", base_class_name, 
					          "' as '", qualified_name_view, "' in current namespace context");
				}
			}
		}
	}
	
	if (base_type_it == gTypesByName.end()) {
		return ParseResult::error("Base class '" + std::string(base_class_name) + "' not found", error_token);
	}

	const TypeInfo* base_type_info = base_type_it->second;
	
	FLASH_LOG_FORMAT(Parser, Debug, "process_base_class: initial base_type_info for '{}': type={}, type_index={}", 
	                 base_class_name, static_cast<int>(base_type_info->type_), base_type_info->type_index_);
	
	// Resolve type aliases: if base_type_info points to another type (type alias),
	// follow the chain to find the actual struct type
	size_t max_alias_depth = 10;  // Prevent infinite loops
	while (base_type_info->type_ != Type::Struct && base_type_info->type_index_ < gTypeInfo.size() && max_alias_depth-- > 0) {
		const TypeInfo& underlying = gTypeInfo[base_type_info->type_index_];
		// Stop if we're pointing to ourselves (not a valid alias)
		if (&underlying == base_type_info) break;
		FLASH_LOG_FORMAT(Parser, Debug, "Resolving type alias '{}' -> type_index {}, underlying type={}", 
		                 base_class_name, base_type_info->type_index_, static_cast<int>(underlying.type_));
		base_type_info = &underlying;
	}
	
	FLASH_LOG_FORMAT(Parser, Debug, "process_base_class: final base_type_info: type={}, type_index={}", 
	                 static_cast<int>(base_type_info->type_), base_type_info->type_index_);
	
	// Check if base class is a template parameter
	bool is_template_param = is_base_class_template_parameter(base_class_name);
	
	// Check if base class is a dependent template placeholder (e.g., integral_constant$hash)
	auto [is_dependent_placeholder, template_base] = isDependentTemplatePlaceholder(base_class_name);
	
	// In template bodies, a UserDefined type alias (e.g., _Tp_alloc_type) may resolve to a struct
	// at instantiation time. Treat it as a deferred base class.
	bool is_dependent_type_alias = false;
	if (!is_template_param && !is_dependent_placeholder && base_type_info->type_ == Type::UserDefined &&
		(parsing_template_body_ || !struct_parsing_context_stack_.empty())) {
		is_dependent_type_alias = true;
	}
	
	// Allow Type::Struct for concrete types OR template parameters OR dependent placeholders OR dependent type aliases
	if (!is_template_param && !is_dependent_placeholder && !is_dependent_type_alias && base_type_info->type_ != Type::Struct) {
		return ParseResult::error("Base class '" + std::string(base_class_name) + "' is not a struct/class", error_token);
	}

	// For template parameters, dependent placeholders, or dependent type aliases, skip 'final' check
	if (!is_template_param && !is_dependent_placeholder && !is_dependent_type_alias) {
		// Check if base class is final
		if (base_type_info->struct_info_ && base_type_info->struct_info_->is_final) {
			return ParseResult::error("Cannot inherit from final class '" + std::string(base_class_name) + "'", error_token);
		}
	}

	// Add base class to struct node and type info
	bool is_deferred = is_template_param || is_dependent_type_alias;
	struct_ref.add_base_class(base_class_name, base_type_info->type_index_, base_access, is_virtual_base, is_deferred);
	struct_info->addBaseClass(base_class_name, base_type_info->type_index_, base_access, is_virtual_base, is_deferred);
	
	return ParseResult::success();
}

// Substitute template parameter in a type specification
// Handles complex transformations like const T& -> const int&, T* -> int*, etc.
std::pair<Type, TypeIndex> Parser::substitute_template_parameter(
	const TypeSpecifierNode& original_type,
	const std::vector<ASTNode>& template_params,
	const std::vector<TemplateTypeArg>& template_args
) {
	Type result_type = original_type.type();
	TypeIndex result_type_index = original_type.type_index();

	// Only substitute UserDefined types (which might be template parameters)
	if (result_type == Type::UserDefined) {
		// First try to get the type name from the token (useful for type aliases parsed inside templates
		// where the type_index might be 0/placeholder because the alias wasn't fully registered yet)
		std::string_view type_name;
		if (original_type.token().type() != Token::Type::Uninitialized && !original_type.token().value().empty()) {
			type_name = original_type.token().value();
		}
		
		// If we have a valid type_index, prefer the name from gTypeInfo
		if (result_type_index < gTypeInfo.size() && result_type_index > 0) {
			const TypeInfo& type_info = gTypeInfo[result_type_index];
			type_name = StringTable::getStringView(type_info.name());
			
			FLASH_LOG(Templates, Debug, "substitute_template_parameter: type_index=", result_type_index, 
				", type_name='", type_name, "', underlying_type=", static_cast<int>(type_info.type_), 
				", underlying_type_index=", type_info.type_index_);
		} else if (!type_name.empty()) {
			FLASH_LOG(Templates, Debug, "substitute_template_parameter: using token name '", type_name, "' (type_index=", result_type_index, " is placeholder)");
		}

		// Try to find which template parameter this is
		bool found_match = false;
		if (!type_name.empty()) {
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				if (template_params[i].is<TemplateParameterNode>()) {
					const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
					if (tparam.name() == type_name) {
						// Found a match! Substitute with the concrete type
						const TemplateTypeArg& arg = template_args[i];
						
						// The template argument already contains the full type info including:
						// - base_type, type_index
						// - pointer_depth, is_reference, is_rvalue_reference
						// - cv_qualifier (const/volatile)
						
						// We need to apply the qualifiers from BOTH:
						// 1. The original type (e.g., const T& has const and reference)
						// 2. The template argument (e.g., T=int* has pointer_depth=1)
						
						result_type = arg.base_type;
						result_type_index = arg.type_index;
						
						// Note: The qualifiers (pointer_depth, references, const/volatile) are NOT
						// combined here because they are already fully specified in the TypeSpecifierNode
						// that will be created using this base type. The caller is responsible for
						// constructing a new TypeSpecifierNode with the appropriate qualifiers.
						
						found_match = true;
						break;
					}
				}
			}

			// Try to resolve dependent qualified member types (e.g., Helper_T::type)
			if (!found_match && type_name.find("::") != std::string_view::npos) {
				auto sep_pos = type_name.find("::");
				std::string base_part(type_name.substr(0, sep_pos));
				std::string_view member_part = type_name.substr(sep_pos + 2);
				auto build_resolved_handle = [](std::string_view base, std::string_view member) {
					StringBuilder sb;
					return StringTable::getOrInternStringHandle(sb.append(base).append("::").append(member).commit());
				};
				
				bool replaced = false;
				for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
					if (!template_params[i].is<TemplateParameterNode>()) continue;
					const auto& tparam = template_params[i].as<TemplateParameterNode>();
					std::string_view tname = tparam.name();
					auto pos = base_part.find(tname);
					if (pos != std::string::npos) {
						base_part.replace(pos, tname.size(), template_args[i].toString());
						replaced = true;
					}
				}
				
				if (replaced) {
					StringHandle resolved_handle = build_resolved_handle(base_part, member_part);
					auto type_it = gTypesByName.find(resolved_handle);
					FLASH_LOG(Templates, Debug, "Dependent member type lookup for '",
					          StringTable::getStringView(resolved_handle), "' found=", (type_it != gTypesByName.end()));
					
					// If not found, try instantiating the base template
					// The base_part contains a mangled name like "enable_if_void_int"
					// We need to find the actual template name, which could be "enable_if" not just "enable"
					if (type_it == gTypesByName.end()) {
						std::string_view base_template_name = extract_base_template_name(base_part);
					
						// Only try to instantiate if we found a class template (not a function template)
						if (!base_template_name.empty()) {
							auto template_opt = gTemplateRegistry.lookupTemplate(base_template_name);
							if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
								try_instantiate_class_template(base_template_name, template_args);
								
								std::string_view instantiated_base = get_instantiated_class_name(base_template_name, template_args);
								resolved_handle = build_resolved_handle(instantiated_base, member_part);
								type_it = gTypesByName.find(resolved_handle);
								FLASH_LOG(Templates, Debug, "After instantiating base template '", base_template_name, "', lookup for '",
								          StringTable::getStringView(resolved_handle), "' found=", (type_it != gTypesByName.end()));
							}
						}
					}
					
					if (type_it != gTypesByName.end()) {
						const TypeInfo* resolved_info = type_it->second;
						result_type = resolved_info->type_;
						result_type_index = resolved_info->type_index_;
						found_match = true;
					}
				}
			}

			// Handle hash-based dependent qualified types like "Wrapper$hash::Nested"
			// These come from parsing "typename Wrapper<T>::Nested" during template definition.
			// The hash represents a dependent instantiation (Wrapper<T> with T not yet resolved).
			// We need to extract the template name ("Wrapper"), re-instantiate with concrete args,
			// and look up the nested type in the new instantiation.
			if (!found_match && type_name.find("::") != std::string_view::npos) {
				auto sep_pos = type_name.find("::");
				std::string_view base_part_sv = type_name.substr(0, sep_pos);
				std::string_view member_part = type_name.substr(sep_pos + 2);
				// '$' in the base part indicates a hash-based mangled template name
				// (e.g., "Wrapper$a1b2c3d4" for dependent Wrapper<T>)
				auto dollar_pos = base_part_sv.find('$');
				
				if (dollar_pos != std::string_view::npos) {
					std::string_view base_template_name = base_part_sv.substr(0, dollar_pos);
					
					auto template_opt = gTemplateRegistry.lookupTemplate(base_template_name);
					if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
						// Re-instantiate with concrete args
						try_instantiate_class_template(base_template_name, template_args);
						std::string_view instantiated_base = get_instantiated_class_name(base_template_name, template_args);
						
						StringBuilder sb;
						StringHandle resolved_handle = StringTable::getOrInternStringHandle(
							sb.append(instantiated_base).append("::").append(member_part).commit());
						auto type_it = gTypesByName.find(resolved_handle);
						
						FLASH_LOG(Templates, Debug, "Dependent hash-qualified type: '", type_name,
						          "' -> '", StringTable::getStringView(resolved_handle),
						          "' found=", (type_it != gTypesByName.end()));
						
						if (type_it != gTypesByName.end()) {
							const TypeInfo* resolved_info = type_it->second;
							result_type = resolved_info->type_;
							result_type_index = resolved_info->type_index_;
							found_match = true;
						}
					}
				}
			}

			// Handle dependent placeholder types like "TC_T" - template instantiations that
			// contain template parameters in their mangled name. Extract the template base
			// name and instantiate with the substituted arguments.
			if (!found_match && type_name.find('_') != std::string_view::npos) {
				for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
					if (!template_params[i].is<TemplateParameterNode>()) continue;
					const auto& tparam = template_params[i].as<TemplateParameterNode>();
					std::string_view param_name = tparam.name();

					// Check if the type name ends with "_<param>" pattern (like "TC_T" for param "T")
					size_t pos = type_name.rfind(param_name);
					if (pos != std::string_view::npos && pos > 0 && type_name[pos - 1] == '_' &&
					    pos + param_name.size() == type_name.size()) {
						// Extract the template base name by finding the template in registry
						std::string_view base_sv = type_name.substr(0, pos - 1);
						auto template_opt = gTemplateRegistry.lookupTemplate(base_sv);
						if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
							// Found the template! Instantiate it with the concrete arguments
							FLASH_LOG(Templates, Debug, "substitute_template_parameter: '", type_name,
							          "' is a dependent placeholder for template '", base_sv, "' - instantiating with concrete args");

							try_instantiate_class_template(base_sv, template_args);
							std::string_view instantiated_name = get_instantiated_class_name(base_sv, template_args);

							auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_name));
							if (type_it != gTypesByName.end()) {
								const TypeInfo* resolved_info = type_it->second;
								result_type = resolved_info->type_;
								result_type_index = resolved_info->type_index_;
								found_match = true;
								FLASH_LOG(Templates, Debug, "  Resolved to '", instantiated_name, "' (type_index=", result_type_index, ")");
							}
							break;
						}
					}
				}
			}

			// If not found as a direct template parameter, check if this is a type alias
			// that resolves to a template parameter (e.g., "using value_type = T;")
			// This requires a valid type_index to look up the alias info
			if (!found_match && result_type_index > 0 && result_type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[result_type_index];
				if (type_info.type_ == Type::UserDefined && type_info.type_index_ != result_type_index) {
					// This is a type alias - recursively check what it resolves to
					if (type_info.type_index_ < gTypeInfo.size()) {
						const TypeInfo& alias_target_info = gTypeInfo[type_info.type_index_];
						std::string_view alias_target_name = StringTable::getStringView(alias_target_info.name());
						
						// Check if the alias target is a template parameter
						for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
							if (template_params[i].is<TemplateParameterNode>()) {
								const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
								if (tparam.name() == alias_target_name) {
									// The type alias resolves to a template parameter - substitute!
									const TemplateTypeArg& arg = template_args[i];
									result_type = arg.base_type;
									result_type_index = arg.type_index;
									FLASH_LOG(Templates, Debug, "Substituted type alias '", type_name, 
										"' (which refers to template param '", alias_target_name, "') with type=", static_cast<int>(result_type));
									found_match = true;
									break;
								}
							}
						}
					}
				}
			}
		}
	}

	return {result_type, result_type_index};
}

// Lookup symbol with template parameter checking
std::optional<ASTNode> Parser::lookup_symbol_with_template_check(StringHandle identifier) {
    // First check if it's a template parameter using the new method
    if (parsing_template_body_ && !current_template_param_names_.empty()) {
        return gSymbolTable.lookup(identifier, gSymbolTable.get_current_scope_handle(), &current_template_param_names_);
    }

    // Otherwise, do normal symbol lookup
    return gSymbolTable.lookup(identifier);
}

// Helper to extract type from an expression for overload resolution
std::optional<TypeSpecifierNode> Parser::get_expression_type(const ASTNode& expr_node) const {
	// Guard against infinite recursion by tracking the call stack
	// Use the address of the expr_node as a unique identifier
	const void* expr_ptr = &expr_node;
	
	// Check if we're already resolving this expression's type
	if (expression_type_resolution_stack_.count(expr_ptr) > 0) {
		FLASH_LOG(Parser, Debug, "get_expression_type: Circular dependency detected, returning nullopt");
		return std::nullopt;  // Prevent infinite recursion
	}
	
	// Add to stack and use RAII to ensure removal
	expression_type_resolution_stack_.insert(expr_ptr);
	auto guard = ScopeGuard([this, expr_ptr]() {
		expression_type_resolution_stack_.erase(expr_ptr);
	});
	
	// Handle lambda expressions directly (not wrapped in ExpressionNode)
	if (expr_node.is<LambdaExpressionNode>()) {
		const auto& lambda = expr_node.as<LambdaExpressionNode>();
		auto closure_name = lambda.generate_lambda_name();

		// Look up the closure type in the type system
		auto type_it = gTypesByName.find(closure_name);
		if (type_it != gTypesByName.end()) {
			const TypeInfo* closure_type = type_it->second;
			// Get closure size in bits from struct info
			int closure_size_bits = 64; // Default to pointer size
			if (closure_type->getStructInfo()) {
				closure_size_bits = closure_type->getStructInfo()->total_size * 8;
			}
			return TypeSpecifierNode(Type::Struct, closure_type->type_index_, closure_size_bits, lambda.lambda_token());
		}

		// Fallback: return a placeholder struct type
		return TypeSpecifierNode(Type::Struct, 0, 64, lambda.lambda_token());
	}

	if (!expr_node.is<ExpressionNode>()) {
		return std::nullopt;
	}

	const ExpressionNode& expr = expr_node.as<ExpressionNode>();

	// Handle different expression types
	if (std::holds_alternative<BoolLiteralNode>(expr)) {
		return TypeSpecifierNode(Type::Bool, TypeQualifier::None, 8);
	}
	else if (std::holds_alternative<NumericLiteralNode>(expr)) {
		const auto& literal = std::get<NumericLiteralNode>(expr);
		return TypeSpecifierNode(literal.type(), literal.qualifier(), literal.sizeInBits());
	}
	else if (std::holds_alternative<StringLiteralNode>(expr)) {
		// String literals have type "const char*" (pointer to const char)
		TypeSpecifierNode char_type(Type::Char, TypeQualifier::None, 8);
		char_type.add_pointer_level(CVQualifier::Const);
		return char_type;
	}
	else if (std::holds_alternative<IdentifierNode>(expr)) {
		const auto& ident = std::get<IdentifierNode>(expr);
		auto symbol = this->lookup_symbol(ident.nameHandle());
		if (symbol.has_value()) {
			if (const DeclarationNode* decl = get_decl_from_symbol(*symbol)) {
				TypeSpecifierNode type = decl->type_node().as<TypeSpecifierNode>();

				// Handle array-to-pointer decay
				// When an array is used in an expression (except with sizeof, &, etc.),
				// it decays to a pointer to its first element
				// Use is_array() which handles both sized arrays (int arr[5]) and
				// unsized arrays (int arr[] = {...}) where is_unsized_array_ is true
				if (decl->is_array()) {
					// This is an array declaration - decay to pointer
					// Create a new TypeSpecifierNode with one level of pointer
					TypeSpecifierNode pointer_type = type;
					pointer_type.add_pointer_level();
					return pointer_type;
				}

				return type;
			}
			// Handle function identifiers: __typeof(func) / decltype(func) should
			// return the function's return type. GCC's __typeof on a function name
			// yields the function type, but for practical purposes (libstdc++ usage
			// like 'extern "C" __typeof(uselocale) __uselocale;'), returning the
			// return type allows parsing to continue past these declarations.
			if (symbol->is<FunctionDeclarationNode>()) {
				const auto& func = symbol->as<FunctionDeclarationNode>();
				const TypeSpecifierNode& ret_type = func.decl_node().type_node().as<TypeSpecifierNode>();
				return ret_type;
			}
		}
	}
	else if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		const auto& binary = std::get<BinaryOperatorNode>(expr);
		TokenKind op_kind = binary.get_token().kind();

		// Comparison and logical operators always return bool
		if (op_kind == tok::Equal || op_kind == tok::NotEqual ||
		    op_kind == tok::Less || op_kind == tok::Greater ||
		    op_kind == tok::LessEq || op_kind == tok::GreaterEq ||
		    op_kind == tok::LogicalAnd || op_kind == tok::LogicalOr) {
			return TypeSpecifierNode(Type::Bool, TypeQualifier::None, 8);
		}

		// For bitwise/arithmetic operators, check the LHS type
		// If LHS is an enum, check for free function operator overloads
		auto lhs_type_opt = get_expression_type(binary.get_lhs());
		if (lhs_type_opt.has_value() && lhs_type_opt->type() == Type::Enum) {
			// Look for a free function operator overload (e.g., operator&(EnumA, EnumB) -> EnumA)
			StringBuilder op_name_builder;
			op_name_builder.append("operator"sv);
			op_name_builder.append(binary.op());
			auto op_name = op_name_builder.commit();
			auto overloads = gSymbolTable.lookup_all(op_name);
			for (const auto& overload : overloads) {
				if (overload.is<FunctionDeclarationNode>()) {
					const auto& func = overload.as<FunctionDeclarationNode>();
					const ASTNode& type_node = func.decl_node().type_node();
					if (type_node.is<TypeSpecifierNode>()) {
						return type_node.as<TypeSpecifierNode>();
					}
				}
			}
		}

		// For same-type operands, return the LHS type
		if (lhs_type_opt.has_value()) {
			auto rhs_type_opt = get_expression_type(binary.get_rhs());
			if (rhs_type_opt.has_value() && lhs_type_opt->type() == rhs_type_opt->type()) {
				return *lhs_type_opt;
			}
		}

		// Default: return int for arithmetic/bitwise operations
		return TypeSpecifierNode(Type::Int, TypeQualifier::None, 32);
	}
	else if (std::holds_alternative<UnaryOperatorNode>(expr)) {
		// For unary operators, handle type transformations
		const auto& unary = std::get<UnaryOperatorNode>(expr);
		std::string_view op = unary.op();

		// Get the operand type
		auto operand_type_opt = get_expression_type(unary.get_operand());
		if (!operand_type_opt.has_value()) {
			return std::nullopt;
		}

		TypeSpecifierNode operand_type = *operand_type_opt;

		// Handle dereference operator: *ptr -> removes one level of pointer/reference
		if (op == "*") {
			if (operand_type.is_reference()) {
				// Dereferencing a reference gives the underlying type
				TypeSpecifierNode result = operand_type;
				result.set_reference(false);
				return result;
			} else if (operand_type.pointer_levels().size() > 0) {
				// Dereferencing a pointer removes one level of pointer
				TypeSpecifierNode result = operand_type;
				result.remove_pointer_level();
				return result;
			}
		}
		// Handle address-of operator: &var -> adds one level of pointer
		else if (op == "&") {
			TypeSpecifierNode result = operand_type;
			result.add_pointer_level();
			return result;
		}

		// For other unary operators (+, -, !, ~, ++, --), return the operand type
		return operand_type;
	}
	else if (std::holds_alternative<FunctionCallNode>(expr)) {
		// For function calls, get the return type
		const auto& func_call = std::get<FunctionCallNode>(expr);
		const auto& decl = func_call.function_declaration();
		TypeSpecifierNode return_type = decl.type_node().as<TypeSpecifierNode>();
		
		FLASH_LOG(Parser, Debug, "get_expression_type for function '", decl.identifier_token().value(), "': return_type=", (int)return_type.type(), ", is_ref=", return_type.is_reference(), ", is_rvalue_ref=", return_type.is_rvalue_reference());
		
		// If the return type is still auto, the function should have been deduced already
		// during parsing. The TypeSpecifierNode in the declaration should have been updated.
		// If it's still auto, it means deduction failed or wasn't performed.
		return return_type;
	}
	else if (std::holds_alternative<MemberFunctionCallNode>(expr)) {
		// For member function calls (including lambda operator() calls), get the return type
		const auto& member_call = std::get<MemberFunctionCallNode>(expr);
		const auto& decl = member_call.function_declaration();
		TypeSpecifierNode return_type = decl.decl_node().type_node().as<TypeSpecifierNode>();
		
		// Try to get the actual function declaration from the struct info
		// The placeholder function declaration may have wrong return type
		const ASTNode& object_node = member_call.object();
		if (object_node.is<ExpressionNode>()) {
			auto object_type_opt = get_expression_type(object_node);
			if (object_type_opt.has_value() && object_type_opt->type() == Type::Struct) {
				size_t struct_type_index = object_type_opt->type_index();
				if (struct_type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[struct_type_index];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info) {
						// Look up the member function
						std::string_view func_name = decl.decl_node().identifier_token().value();
						for (const auto& member_func : struct_info->member_functions) {
							if (member_func.getName() == StringTable::getOrInternStringHandle(func_name) && 
								member_func.function_decl.is<FunctionDeclarationNode>()) {
								// Found the real function - use its return type
								const FunctionDeclarationNode& real_func = 
									member_func.function_decl.as<FunctionDeclarationNode>();
								return_type = real_func.decl_node().type_node().as<TypeSpecifierNode>();
								break;
							}
						}
					}
				}
			}
		}
		
		FLASH_LOG(Parser, Debug, "get_expression_type for member function call: ", 
				  decl.decl_node().identifier_token().value(), 
				  " return_type=", (int)return_type.type(), " size=", (int)return_type.size_in_bits());
		
		// If the return type is still auto, it should have been deduced during parsing
		return return_type;
	}
	else if (std::holds_alternative<LambdaExpressionNode>(expr)) {
		// For lambda expressions, return the closure struct type
		const auto& lambda = std::get<LambdaExpressionNode>(expr);
		auto closure_name = lambda.generate_lambda_name();

		// Look up the closure type in the type system
		auto type_it = gTypesByName.find(closure_name);
		if (type_it != gTypesByName.end()) {
			const TypeInfo* closure_type = type_it->second;
			// Get closure size in bits from struct info
			int closure_size_bits = 64; // Default to pointer size
			if (closure_type->getStructInfo()) {
				closure_size_bits = closure_type->getStructInfo()->total_size * 8;
			}
			return TypeSpecifierNode(Type::Struct, closure_type->type_index_, closure_size_bits, lambda.lambda_token());
		}

		// Fallback: return a placeholder struct type
		return TypeSpecifierNode(Type::Struct, 0, 64, lambda.lambda_token());
	}
	else if (std::holds_alternative<ConstructorCallNode>(expr)) {
		// For constructor calls like Widget(42), return the type being constructed
		const auto& ctor_call = std::get<ConstructorCallNode>(expr);
		const ASTNode& type_node = ctor_call.type_node();
		if (type_node.is<TypeSpecifierNode>()) {
			return type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<StaticCastNode>(expr)) {
		// For cast expressions like (Type)expr or static_cast<Type>(expr), return the target type
		const auto& cast = std::get<StaticCastNode>(expr);
		const ASTNode& target_type_node = cast.target_type();
		if (target_type_node.is<TypeSpecifierNode>()) {
			return target_type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<DynamicCastNode>(expr)) {
		// For dynamic_cast<Type>(expr), return the target type
		const auto& cast = std::get<DynamicCastNode>(expr);
		const ASTNode& target_type_node = cast.target_type();
		if (target_type_node.is<TypeSpecifierNode>()) {
			return target_type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<ConstCastNode>(expr)) {
		// For const_cast<Type>(expr), return the target type
		const auto& cast = std::get<ConstCastNode>(expr);
		const ASTNode& target_type_node = cast.target_type();
		if (target_type_node.is<TypeSpecifierNode>()) {
			return target_type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<ReinterpretCastNode>(expr)) {
		// For reinterpret_cast<Type>(expr), return the target type
		const auto& cast = std::get<ReinterpretCastNode>(expr);
		const ASTNode& target_type_node = cast.target_type();
		if (target_type_node.is<TypeSpecifierNode>()) {
			return target_type_node.as<TypeSpecifierNode>();
		}
	}
	else if (std::holds_alternative<MemberAccessNode>(expr)) {
		// For member access expressions like obj.member or (*ptr).member
		const auto& member_access = std::get<MemberAccessNode>(expr);
		const ASTNode& object_node = member_access.object();
		std::string_view member_name = member_access.member_name();
		
		// Get the type of the object
		auto object_type_opt = get_expression_type(object_node);
		if (!object_type_opt.has_value()) {
			return std::nullopt;
		}
		
		const TypeSpecifierNode& object_type = *object_type_opt;
		
		// Handle struct/class member access
		if (object_type.type() == Type::Struct || object_type.type() == Type::UserDefined) {
			size_t struct_type_index = object_type.type_index();
			if (struct_type_index < gTypeInfo.size()) {
				// Look up the member
				auto member_result = FlashCpp::gLazyMemberResolver.resolve(static_cast<TypeIndex>(struct_type_index), StringTable::getOrInternStringHandle(std::string(member_name)));
				if (member_result) {
					// Return the member's type
					// member->size is in bytes, TypeSpecifierNode expects bits
					TypeSpecifierNode member_type(member_result.member->type, TypeQualifier::None, member_result.member->size * 8);
					member_type.set_type_index(member_result.member->type_index);
					return member_type;
				}
			}
		}
	}
	else if (std::holds_alternative<PointerToMemberAccessNode>(expr)) {
		// For pointer-to-member access expressions like obj.*ptr_to_member or obj->*ptr_to_member
		// The type depends on the pointer-to-member type, which is complex to determine
		// For now, return nullopt as this is primarily used in decltype contexts where
		// the actual type isn't needed during parsing
		return std::nullopt;
	}
	else if (std::holds_alternative<PseudoDestructorCallNode>(expr)) {
		// Pseudo-destructor call (obj.~Type()) always returns void
		const auto& dtor_call = std::get<PseudoDestructorCallNode>(expr);
		return TypeSpecifierNode(Type::Void, TypeQualifier::None, 0, dtor_call.type_name_token());
	}
	else if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		// For ternary expressions (cond ? true_expr : false_expr), determine the common type
		// This is important for decltype(true ? expr1 : expr2) patterns used in <type_traits>
		const auto& ternary = std::get<TernaryOperatorNode>(expr);
		
		// Get types of both branches
		auto true_type_opt = get_expression_type(ternary.true_expr());
		auto false_type_opt = get_expression_type(ternary.false_expr());
		
		// If both types are available, determine the common type
		if (true_type_opt.has_value() && false_type_opt.has_value()) {
			const TypeSpecifierNode& true_type = *true_type_opt;
			const TypeSpecifierNode& false_type = *false_type_opt;
			
			// If both types are the same, return that type
			if (true_type.type() == false_type.type() && 
				true_type.type_index() == false_type.type_index() &&
				true_type.pointer_levels().size() == false_type.pointer_levels().size()) {
				// Return the common type (prefer the true branch for reference/const qualifiers)
				return true_type;
			}
			
			// Handle common type conversions for arithmetic types
			if (true_type.type() != Type::Struct && true_type.type() != Type::UserDefined &&
				false_type.type() != Type::Struct && false_type.type() != Type::UserDefined) {
				// For arithmetic types, use usual arithmetic conversions
				// Return the larger type (in terms of bit width)
				if (true_type.size_in_bits() >= false_type.size_in_bits()) {
					return true_type;
				} else {
					return false_type;
				}
			}
			
			// For mixed struct types, we can't easily determine the common type
			// In template context, this might be a dependent type
			// Return the true branch type as fallback
			return true_type;
		}
		
		// If only one type is available, return that
		if (true_type_opt.has_value()) {
			return true_type_opt;
		}
		if (false_type_opt.has_value()) {
			return false_type_opt;
		}
		
		// Both types unavailable - return nullopt
		return std::nullopt;
	}
	else if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
		// For qualified identifiers like MakeUnsigned::List<int, char>::size
		// We need to look up the type of the static member
		const auto& qual_id = std::get<QualifiedIdentifierNode>(expr);
		NamespaceHandle ns_handle = qual_id.namespace_handle();
		std::string_view member_name = qual_id.name();
		
		if (!ns_handle.isGlobal()) {
			// Get the struct name (the namespace handle's name is the last component)
			std::string_view struct_name = gNamespaceRegistry.getName(ns_handle);
			
			// Try to find the struct in gTypesByName
			auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
			
			// If not found directly, try building full qualified name
			if (struct_type_it == gTypesByName.end() && gNamespaceRegistry.getDepth(ns_handle) > 1) {
				std::string_view full_qualified_name = gNamespaceRegistry.getQualifiedName(ns_handle);
				struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(full_qualified_name));
			}
			
			if (struct_type_it != gTypesByName.end() && struct_type_it->second->isStruct()) {
				const StructTypeInfo* struct_info = struct_type_it->second->getStructInfo();
				if (struct_info) {
					// Trigger lazy static member instantiation if needed
					StringHandle member_name_handle = StringTable::getOrInternStringHandle(std::string(member_name));
					const_cast<Parser*>(this)->instantiateLazyStaticMember(struct_info->name, member_name_handle);
					
					// Look for static member
					auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_name_handle);
					if (static_member && owner_struct) {
						// Found the static member - return its type
						TypeSpecifierNode member_type(static_member->type, TypeQualifier::None, static_member->size * 8);
						member_type.set_type_index(static_member->type_index);
						if (static_member->is_const) {
							member_type.set_cv_qualifier(CVQualifier::Const);
						}
						return member_type;
					}
				}
			}
		}
	}
	// Add more cases as needed

	return std::nullopt;
}

// Helper function to deduce the type of an expression for auto type deduction
Type Parser::deduce_type_from_expression(const ASTNode& expr) const {
	// For now, use a simple approach: use the existing get_expression_type function
	// which returns TypeSpecifierNode, and extract the type from it
	auto type_spec_opt = get_expression_type(expr);
	if (type_spec_opt.has_value()) {
		return type_spec_opt->type();
	}

	// Default to int if we can't determine the type
	return Type::Int;
}

// Helper function to deduce and update auto return type from function body
void Parser::deduce_and_update_auto_return_type(FunctionDeclarationNode& func_decl) {
	// Check if the return type is auto
	DeclarationNode& decl_node = func_decl.decl_node();
	const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
	
	FLASH_LOG(Parser, Debug, "deduce_and_update_auto_return_type called for function: ", 
			  decl_node.identifier_token().value(), " return_type=", (int)return_type.type());
	
	if (return_type.type() != Type::Auto) {
		return;  // Not an auto return type, nothing to do
	}
	
	// Prevent infinite recursion: check if we're already deducing this function's type
	if (functions_being_deduced_.count(&func_decl) > 0) {
		FLASH_LOG(Parser, Debug, "  Already deducing this function, skipping to prevent recursion");
		return;
	}
	
	// Add this function to the set of functions being deduced
	functions_being_deduced_.insert(&func_decl);
	
	// RAII guard to remove the function from the set when we exit
	auto guard = ScopeGuard([this, &func_decl]() {
		functions_being_deduced_.erase(&func_decl);
	});
	
	// Get the function body
	const std::optional<ASTNode>& body_opt = func_decl.get_definition();
	if (!body_opt.has_value() || !body_opt->is<BlockNode>()) {
		FLASH_LOG(Parser, Debug, "  No body or invalid body");
		return;  // No body or invalid body
	}
	
	// Walk through the function body to find return statements
	const BlockNode& body = body_opt->as<BlockNode>();
	std::optional<TypeSpecifierNode> deduced_type;
	std::vector<std::pair<TypeSpecifierNode, Token>> all_return_types;  // Track all return types for validation
	
	// Recursive lambda to search for return statements
	std::function<void(const ASTNode&)> find_return_statements = [&](const ASTNode& node) {
		if (node.is<ReturnStatementNode>()) {
			const ReturnStatementNode& ret = node.as<ReturnStatementNode>();
			if (ret.expression().has_value()) {
				auto expr_type_opt = get_expression_type(*ret.expression());
				if (expr_type_opt.has_value()) {
					// Store this return type for validation
					all_return_types.emplace_back(*expr_type_opt, decl_node.identifier_token());
					
					// Set deduced type from first return
					if (!deduced_type.has_value()) {
						deduced_type = *expr_type_opt;
						FLASH_LOG(Parser, Debug, "  Found return statement, deduced type: ", 
								  (int)deduced_type->type(), " size: ", (int)deduced_type->size_in_bits());
					}
				}
			}
		} else if (node.is<BlockNode>()) {
			// Recursively search nested blocks
			const BlockNode& block = node.as<BlockNode>();
			block.get_statements().visit([&](const ASTNode& stmt) {
				find_return_statements(stmt);
			});
		} else if (node.is<IfStatementNode>()) {
			const IfStatementNode& if_stmt = node.as<IfStatementNode>();
			if (if_stmt.get_then_statement().has_value()) {
				find_return_statements(if_stmt.get_then_statement());
			}
			if (if_stmt.get_else_statement().has_value()) {
				find_return_statements(*if_stmt.get_else_statement());
			}
		} else if (node.is<ForStatementNode>()) {
			const ForStatementNode& for_stmt = node.as<ForStatementNode>();
			if (for_stmt.get_body_statement().has_value()) {
				find_return_statements(for_stmt.get_body_statement());
			}
		} else if (node.is<WhileStatementNode>()) {
			const WhileStatementNode& while_stmt = node.as<WhileStatementNode>();
			if (while_stmt.get_body_statement().has_value()) {
				find_return_statements(while_stmt.get_body_statement());
			}
		} else if (node.is<DoWhileStatementNode>()) {
			const DoWhileStatementNode& do_while = node.as<DoWhileStatementNode>();
			if (do_while.get_body_statement().has_value()) {
				find_return_statements(do_while.get_body_statement());
			}
		} else if (node.is<SwitchStatementNode>()) {
			const SwitchStatementNode& switch_stmt = node.as<SwitchStatementNode>();
			if (switch_stmt.get_body().has_value()) {
				find_return_statements(switch_stmt.get_body());
			}
		}
		// Add more statement types as needed
	};
	
	// Search the function body
	body.get_statements().visit([&](const ASTNode& stmt) {
		find_return_statements(stmt);
	});
	
	// Validate that all return statements have compatible types
	if (all_return_types.size() > 1) {
		const TypeSpecifierNode& first_type = all_return_types[0].first;
		for (size_t i = 1; i < all_return_types.size(); ++i) {
			const TypeSpecifierNode& current_type = all_return_types[i].first;
			if (!are_types_compatible(first_type, current_type)) {
				// Log error but don't fail compilation (just log warning)
				// We could make this a hard error, but for now just warn
				FLASH_LOG(Parser, Warning, "Function '", decl_node.identifier_token().value(),
						  "' has inconsistent return types: first return has type '",
						  type_to_string(first_type), "', but another return has type '",
						  type_to_string(current_type), "'");
			}
		}
	}
	
	// If we found a deduced type, update the function declaration's return type
	if (deduced_type.has_value()) {
		// Create a new ASTNode with the deduced type and update the declaration
		// Note: new_type_ref is a reference to the newly created node, not the moved-from deduced_type
		auto [new_type_node, new_type_ref] = create_node_ref<TypeSpecifierNode>(std::move(*deduced_type));
		decl_node.set_type_node(new_type_node);
		
		FLASH_LOG(Parser, Debug, "  Updated return type to: ", (int)new_type_ref.type(), 
				  " size: ", (int)new_type_ref.size_in_bits());
		
		// Log deduction for debugging
		FLASH_LOG(Parser, Debug, "Deduced auto return type for function '", decl_node.identifier_token().value(), 
				  "': type=", (int)new_type_ref.type(), " size=", (int)new_type_ref.size_in_bits());
	}
}

// Helper function to count pack elements in template parameter packs
// Counts by looking up pack_name_0, pack_name_1, etc. in the symbol table
size_t Parser::count_pack_elements(std::string_view pack_name) const {
	size_t num_pack_elements = 0;
	StringBuilder param_name_builder;
	
	while (true) {
		// Build the parameter name: pack_name + "_" + index
		param_name_builder.append(pack_name);
		param_name_builder.append('_');
		param_name_builder.append(num_pack_elements);
		std::string_view param_name = param_name_builder.preview();
		
		// Check if this parameter exists in the symbol table
		auto lookup_result = gSymbolTable.lookup(param_name);
		param_name_builder.reset();  // Reset for next iteration
		
		if (!lookup_result.has_value()) {
			break;  // No more pack elements
		}
		num_pack_elements++;
		
		// Safety limit to prevent infinite loops
		if (num_pack_elements > MAX_PACK_ELEMENTS) {
			FLASH_LOG(Templates, Error, "Pack '", pack_name, "' expansion exceeded MAX_PACK_ELEMENTS (", MAX_PACK_ELEMENTS, ")");
			break;
		}
	}
	
	return num_pack_elements;
}

// Parse extern "C" { ... } block
ParseResult Parser::parse_extern_block(Linkage linkage) {
	// Expect '{'
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' after extern linkage specification", current_token_);
	}

	// Save the current linkage and set the new one
	Linkage saved_linkage = current_linkage_;
	current_linkage_ = linkage;

	// Save the current AST size to know which nodes were added by this block
	size_t ast_size_before = ast_nodes_.size();

	// Parse declarations until '}' by calling parse_top_level_node() repeatedly
	// This ensures extern "C" blocks support exactly the same constructs as file scope
	while (!peek().is_eof() && peek() != "}"_tok) {
		
		ParseResult result = parse_top_level_node();
		
		if (result.is_error()) {
			current_linkage_ = saved_linkage;  // Restore linkage before returning error
			return result;
		}
		
		// parse_top_level_node() already adds nodes to ast_nodes_, so we don't need to do it here
	}

	// Restore the previous linkage
	current_linkage_ = saved_linkage;

	if (!consume("}"_tok)) {
		return ParseResult::error("Expected '}' after extern block", current_token_);
	}

	// Create a block node containing all declarations parsed in this extern block
	auto [block_node, block_ref] = create_node_ref(BlockNode());
	
	// Move all nodes added during this block into the BlockNode
	for (size_t i = ast_size_before; i < ast_nodes_.size(); ++i) {
		block_ref.add_statement_node(ast_nodes_[i]);
	}
	
	// Remove those nodes from ast_nodes_ since they're now in the BlockNode
	ast_nodes_.resize(ast_size_before);

	return ParseResult::success(block_node);
}

// Helper function to get the size of a type in bits
// Helper function to check if two types are compatible (same type, ignoring qualifiers)
bool Parser::are_types_compatible(const TypeSpecifierNode& type1, const TypeSpecifierNode& type2) const {
	// Check basic type
	if (type1.type() != type2.type()) {
		return false;
	}
	
	// For user-defined types (Struct, Enum), check type index
	if (type1.type() == Type::Struct || type1.type() == Type::Enum) {
		if (type1.type_index() != type2.type_index()) {
			return false;
		}
	}
	
	// Check pointer levels
	if (type1.pointer_levels().size() != type2.pointer_levels().size()) {
		return false;
	}
	
	// Check if reference
	if (type1.is_reference() != type2.is_reference()) {
		return false;
	}
	
	// Types are compatible (we ignore const/volatile qualifiers for this check)
	return true;
}

// Helper function to convert a type to a string for error messages
std::string Parser::type_to_string(const TypeSpecifierNode& type) const {
	std::string result;
	
	// Add const/volatile qualifiers
	if (static_cast<uint8_t>(type.cv_qualifier()) & static_cast<uint8_t>(CVQualifier::Const)) {
		result += "const ";
	}
	if (static_cast<uint8_t>(type.cv_qualifier()) & static_cast<uint8_t>(CVQualifier::Volatile)) {
		result += "volatile ";
	}
	
	// Add base type name
	switch (type.type()) {
		case Type::Void: result += "void"; break;
		case Type::Bool: result += "bool"; break;
		case Type::Char: result += "char"; break;
		case Type::UnsignedChar: result += "unsigned char"; break;
		case Type::Short: result += "short"; break;
		case Type::UnsignedShort: result += "unsigned short"; break;
		case Type::Int: result += "int"; break;
		case Type::UnsignedInt: result += "unsigned int"; break;
		case Type::Long: result += "long"; break;
		case Type::UnsignedLong: result += "unsigned long"; break;
		case Type::LongLong: result += "long long"; break;
		case Type::UnsignedLongLong: result += "unsigned long long"; break;
		case Type::Float: result += "float"; break;
		case Type::Double: result += "double"; break;
		case Type::LongDouble: result += "long double"; break;
		case Type::Auto: result += "auto"; break;
		case Type::Struct:
			if (type.type_index() < gTypeInfo.size()) {
				result += std::string(StringTable::getStringView(gTypeInfo[type.type_index()].name()));
			} else {
				result += "struct";
			}
			break;
		case Type::Enum:
			if (type.type_index() < gTypeInfo.size()) {
				result += std::string(StringTable::getStringView(gTypeInfo[type.type_index()].name()));
			} else {
				result += "enum";
			}
			break;
		case Type::Function: result += "function"; break;
		case Type::FunctionPointer: result += "function pointer"; break;
		case Type::MemberFunctionPointer: result += "member function pointer"; break;
		case Type::MemberObjectPointer: result += "member object pointer"; break;
		case Type::Nullptr: result += "nullptr_t"; break;
		default: result += "unknown"; break;
	}
	
	// Add pointer levels
	for (size_t i = 0; i < type.pointer_levels().size(); ++i) {
		result += "*";
		const PointerLevel& ptr_level = type.pointer_levels()[i];
		CVQualifier cv = ptr_level.cv_qualifier;
		if (static_cast<uint8_t>(cv) & static_cast<uint8_t>(CVQualifier::Const)) {
			result += " const";
		}
		if (static_cast<uint8_t>(cv) & static_cast<uint8_t>(CVQualifier::Volatile)) {
			result += " volatile";
		}
	}
	
	// Add reference
	if (type.is_reference()) {
		result += type.is_rvalue_reference() ? "&&" : "&";
	}
	
	return result;
}

// Note: Type size lookup is now unified in ::get_type_size_bits() from AstNodeTypes.h
// This ensures consistent handling of target-dependent types like 'long' (LLP64 vs LP64)
