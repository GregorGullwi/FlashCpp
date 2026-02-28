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
					
					// Reject unresolved qualified names (e.g., Foo::val) that the type parser
					// consumed as a qualified type name placeholder (UserDefined, size 0).
					// When the token is a known struct name but the result is UserDefined (not Struct),
					// parse_type_specifier consumed Foo::member as a single identifier and failed to
					// resolve it as a type.  Fall through to expression parsing so sizeof can
					// look up the struct member via QualifiedIdentifierNode.
					if (type_spec.type() == Type::UserDefined && type_spec.size_in_bits() == 0 &&
					    type_spec.token().type() == Token::Type::Identifier) {
						StringHandle tok_handle = StringTable::getOrInternStringHandle(type_spec.token().value());
						auto struct_it = gTypesByName.find(tok_handle);
						if (struct_it != gTypesByName.end() && struct_it->second->isStruct()) {
							is_complete_type = false;
						}
					}
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
			FunctionCallNode(func_decl, std::move(args), builtin_token));
		
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

