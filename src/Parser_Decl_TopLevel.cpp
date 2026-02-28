ParseResult Parser::parse_top_level_node()
{
	// Save the current token's position to restore later in case of a parsing
	// error
	ScopedTokenPosition saved_position(*this);

#if WITH_DEBUG_INFO
	if (break_at_line_.has_value() && peek_info().line() == break_at_line_)
	{
		DEBUG_BREAK();
	}
#endif

	// Skip empty declarations (lone semicolons) - valid in C++ (empty-declaration)
	if (peek() == ";"_tok) {
		advance();
		return saved_position.success();
	}

	// Check for __pragma() - Microsoft's inline pragma syntax
	// e.g., __pragma(pack(push, 8))
	if (peek_info().type() == Token::Type::Identifier && peek_info().value() == "__pragma") {
		advance(); // consume '__pragma'
		if (!consume("("_tok)) {
			return ParseResult::error("Expected '(' after '__pragma'", current_token_);
		}
		
		// Now parse what's inside - it could be pack(...) or something else
		if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
		    peek_info().value() == "pack") {
			advance(); // consume 'pack'
			if (!consume("("_tok)) {
				return ParseResult::error("Expected '(' after '__pragma(pack'", current_token_);
			}
			
			// Reuse the pack parsing logic by calling parse_pragma_pack_inner
			auto pack_result = parse_pragma_pack_inner();
			if (pack_result.is_error()) {
				return pack_result;
			}
			
			// Consume the outer closing ')'
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after '__pragma(...)'", current_token_);
			}
			return saved_position.success();
		} else {
			// Unknown __pragma content - skip until balanced parens
			int paren_depth = 1;
			while (!peek().is_eof() && paren_depth > 0) {
				if (peek() == "("_tok) {
					paren_depth++;
				} else if (peek() == ")"_tok) {
					paren_depth--;
				}
				advance();
			}
			return saved_position.success();
		}
	}

	// Check for #pragma directives
	if (peek() == "#"_tok) {
		advance(); // consume '#'
		if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
		    peek_info().value() == "pragma") {
			advance(); // consume 'pragma'
			if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
			    peek_info().value() == "pack") {
				advance(); // consume 'pack'

				if (!consume("("_tok)) {
					return ParseResult::error("Expected '(' after '#pragma pack'", current_token_);
				}

				// Use the shared helper function to parse the pack contents
				auto pack_result = parse_pragma_pack_inner();
				if (pack_result.is_error()) {
					return saved_position.propagate(std::move(pack_result));
				}
				return saved_position.success();
			} else {
				// Unknown pragma - skip until end of line or until we hit a token that looks like the start of a new construct
				// Pragmas can span multiple lines with parentheses, so we need to be careful
				FLASH_LOG(Parser, Warning, "Skipping unknown pragma: ", (!peek().is_eof() ? std::string(peek_info().value()) : "EOF"));
				int paren_depth = 0;
				while (!peek().is_eof()) {
					FLASH_LOG(Parser, Debug, "  pragma skip loop: token='", peek_info().value(), "' type=", static_cast<int>(peek_info().type()), " paren_depth=", paren_depth);
					if (peek() == "("_tok) {
						paren_depth++;
						advance();
					} else if (peek() == ")"_tok) {
						paren_depth--;
						advance();
						if (paren_depth == 0) {
							// End of pragma
							break;
						}
					} else if (paren_depth == 0 && peek() == "#"_tok) {
						// Start of a new preprocessor directive
						break;
					} else if (paren_depth == 0 && peek().is_keyword()) {
						// Start of a new declaration (namespace, class, extern, etc.)
						break;
					} else {
						advance();
					}
				}
				return saved_position.success();
			}
		}
	}

	// Helper: parse, push resulting node to AST, return success/propagate.
	auto try_parse_and_push = [&](ParseResult result) -> ParseResult {
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			return saved_position.success();
		}
		return saved_position.propagate(std::move(result));
	};

	// Check if it's a using directive, using declaration, or namespace alias
	if (peek() == "using"_tok)
		return try_parse_and_push(parse_using_directive_or_declaration());

	// Check if it's a static_assert declaration
	if (peek() == "static_assert"_tok) {
		auto result = parse_static_assert();
		if (!result.is_error()) {
			// static_assert doesn't produce an AST node (compile-time only)
			return saved_position.success();
		}
		return saved_position.propagate(std::move(result));
	}

	// Check for inline namespace declaration (inline namespace foo { ... })
	if (peek() == "inline"_tok) {
		auto next = peek_info(1);
		if (next.kind() == "namespace"_tok) {
			pending_inline_namespace_ = true;
			advance(); // consume 'inline'
			return try_parse_and_push(parse_namespace());
		}
	}

	// Check if it's a namespace declaration
	if (peek() == "namespace"_tok)
		return try_parse_and_push(parse_namespace());

	// Check if it's a template declaration (must come before struct/class check)
	if (peek() == "template"_tok)
		return try_parse_and_push(parse_template_declaration());

	// Check if it's a concept declaration (C++20)
	if (peek() == "concept"_tok)
		return try_parse_and_push(parse_concept_declaration());

	// Check if it's a class or struct declaration
	// Note: alignas can appear before struct, but we handle that in parse_struct_declaration
	// If alignas appears before a variable declaration, it will be handled by parse_declaration_or_function_definition
	if ((peek() == "class"_tok || peek() == "struct"_tok || peek() == "union"_tok)) {
		auto result = parse_struct_declaration();
		if (!result.is_error()) {
			if (auto node = result.node()) {
				ast_nodes_.push_back(*node);
			}
			// Add any pending variable declarations from the struct definition
			for (auto& var_node : pending_struct_variables_) {
				ast_nodes_.push_back(var_node);
			}
			pending_struct_variables_.clear();
			return saved_position.success();
		}
		return saved_position.propagate(std::move(result));
	}

	// Check if it's an enum declaration
	if (peek() == "enum"_tok)
		return try_parse_and_push(parse_enum_declaration());

	// Check if it's a typedef declaration
	if (peek() == "typedef"_tok)
		return try_parse_and_push(parse_typedef_declaration());

	// Check for extern "C" linkage specification
	if (peek() == "extern"_tok) {
		// Save position in case this is just a regular extern declaration
		SaveHandle extern_saved_pos = save_token_position();
		advance(); // consume 'extern'

		// Check if this is extern "C" or extern "C++"
		if (peek().is_string_literal()) {
			std::string_view linkage_str = peek_info().value();

			// Remove quotes from string literal
			if (linkage_str.size() >= 2 && linkage_str.front() == '"' && linkage_str.back() == '"') {
				linkage_str = linkage_str.substr(1, linkage_str.size() - 2);
			}

			Linkage linkage = Linkage::None;
			if (linkage_str == "C") {
				linkage = Linkage::C;
			} else if (linkage_str == "C++") {
				linkage = Linkage::CPlusPlus;
			} else {
				return ParseResult::error("Unknown linkage specification: " + std::string(linkage_str), current_token_);
			}

			advance(); // consume linkage string

			// Discard the extern_saved_pos since we're handling extern "C"
			discard_saved_token(extern_saved_pos);

			// Check for block form: extern "C" { ... }
			if (peek() == "{"_tok) {
				auto result = parse_extern_block(linkage);
				if (!result.is_error()) {
					if (auto node = result.node()) {
						// The block contains multiple declarations, add them all
						if (node->is<BlockNode>()) {
							const BlockNode& block = node->as<BlockNode>();
							block.get_statements().visit([&](const ASTNode& stmt) {
								ast_nodes_.push_back(stmt);
							});
						}
					}
					return saved_position.success();
				}
				return saved_position.propagate(std::move(result));
			}

			// Single declaration form: extern "C" int func();
			// Set the current linkage and parse the declaration/function
			Linkage saved_linkage = current_linkage_;
			current_linkage_ = linkage;

			ParseResult decl_result = parse_declaration_or_function_definition();

			// Restore the previous linkage
			current_linkage_ = saved_linkage;

			if (decl_result.is_error()) {
				return decl_result;
			}

			// Add the node to the AST if it exists
			if (auto decl_node = decl_result.node()) {
				ast_nodes_.push_back(*decl_node);
			}

			return saved_position.success();
		} else if (peek() == "template"_tok) {
			// extern template class allocator<char>; — explicit instantiation declaration
			// Don't restore, we've already consumed 'extern', and parse_template_declaration
			// will consume 'template'. Discard the saved position.
			discard_saved_token(extern_saved_pos);
			auto template_result = parse_template_declaration();
			if (!template_result.is_error()) {
				return saved_position.success();
			}
			return saved_position.propagate(std::move(template_result));
		} else {
			// Regular extern without linkage specification, restore and continue
			restore_token_position(extern_saved_pos);
		}
	}

	// Attempt to parse a function definition, variable declaration, or typedef
	FLASH_LOG(Parser, Debug, "parse_top_level_node: About to call parse_declaration_or_function_definition, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	auto result = parse_declaration_or_function_definition();
	if (!result.is_error()) {
		if (auto node = result.node()) {
			ast_nodes_.push_back(*node);
		}
		return saved_position.success();
	}

	// If we failed to parse any top-level construct, restore the token position
	// and report an error
	FLASH_LOG(Parser, Debug, "parse_top_level_node: parse_declaration_or_function_definition failed, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A", ", error: ", result.error_message());
	
	// Preserve the original error token instead of creating a new error with the saved token
	// This ensures error messages point to the actual error location, not the start of the construct
	return saved_position.propagate(std::move(result));
}

ParseResult Parser::parse_static_assert()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'static_assert' keyword
	auto static_assert_keyword = advance();
	if (static_assert_keyword.kind() != "static_assert"_tok) {
		return ParseResult::error("Expected 'static_assert' keyword", static_assert_keyword);
	}

	// Expect opening parenthesis
	if (!consume("("_tok)) {
		return ParseResult::error("Expected '(' after 'static_assert'", current_token_);
	}

	// Parse the condition expression
	ParseResult condition_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	if (condition_result.is_error()) {
		return condition_result;
	}

	// Check for optional comma and message
	std::string message;
	if (consume(","_tok)) {
		// Parse the message string literal(s) - C++ allows adjacent string literals to be concatenated
		while (peek().is_string_literal()) {
			auto message_token = advance();
			if (message_token.value().size() >= 2 && 
			    message_token.value().front() == '"' && 
			    message_token.value().back() == '"') {
				// Extract the message content (remove quotes) and append
				message += std::string(message_token.value().substr(1, message_token.value().size() - 2));
			}
		}
		if (message.empty()) {
			return ParseResult::error("Expected string literal for static_assert message", current_token_);
		}
	}

	// Expect closing parenthesis
	if (!consume(")"_tok)) {
		return ParseResult::error("Expected ')' after static_assert", current_token_);
	}

	// Expect semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after static_assert", current_token_);
	}

	// If we're inside a template body during template DEFINITION (not instantiation),
	// defer static_assert evaluation until instantiation.
	// However, if we can evaluate it now (non-dependent expression), we should do so to catch errors early.
	// The expression may depend on template parameters that are not yet known
	bool is_in_template_definition = parsing_template_body_ && !current_template_param_names_.empty();
	
	// Also consider struct parsing context - if we're inside a template struct body,
	// member function bodies may be parsed later but still contain template-dependent expressions
	bool is_in_template_struct = !struct_parsing_context_stack_.empty() && 
		(parsing_template_body_ || !current_template_param_names_.empty());
	
	// Try to evaluate the constant expression using ConstExprEvaluator
	ConstExpr::EvaluationContext ctx(gSymbolTable);
	ctx.parser = this;  // Enable template function instantiation
	
	// Pass struct context for static member lookup in static_assert within struct body
	if (!struct_parsing_context_stack_.empty()) {
		const auto& struct_ctx = struct_parsing_context_stack_.back();
		ctx.struct_node = struct_ctx.struct_node;
		ctx.struct_info = struct_ctx.local_struct_info;
	}
	
	auto eval_result = ConstExpr::Evaluator::evaluate(*condition_result.node(), ctx);
	
	// If evaluation failed with a template-dependent expression error, defer only in template context.
	// In non-template code, fall through to error handling (e.g. sizeof returning 0 for incomplete types).
	if (!eval_result.success() && eval_result.error_type == ConstExpr::EvalErrorType::TemplateDependentExpression) {
		if (is_in_template_definition || is_in_template_struct) {
			FLASH_LOG(Templates, Debug, "Deferring static_assert with template-dependent expression: ", eval_result.error_message);
			
			// Store the deferred static_assert in the current struct/class for later evaluation
			if (!struct_parsing_context_stack_.empty()) {
				const auto& struct_ctx = struct_parsing_context_stack_.back();
				if (struct_ctx.struct_node) {
					StringHandle message_handle = StringTable::getOrInternStringHandle(message);
					struct_ctx.struct_node->add_deferred_static_assert(*condition_result.node(), message_handle);
					FLASH_LOG(Templates, Debug, "Stored deferred static_assert in struct '", 
					          struct_ctx.struct_node->name(), "' for later evaluation");
				}
			}
			
			return saved_position.success();
		}
		// Not in template context - fall through to error handling below
	}
	
	// If we're in a template definition and evaluation failed for other reasons,
	// that's okay - skip it and it will be checked during instantiation
	if ((is_in_template_definition || is_in_template_struct) && !eval_result.success()) {
		FLASH_LOG(Templates, Debug, "static_assert evaluation failed in template body: ", eval_result.error_message);
		
		// Store the deferred static_assert in the current struct/class for later evaluation
		if (!struct_parsing_context_stack_.empty()) {
			const auto& struct_ctx = struct_parsing_context_stack_.back();
			if (struct_ctx.struct_node) {
				StringHandle message_handle = StringTable::getOrInternStringHandle(message);
				struct_ctx.struct_node->add_deferred_static_assert(*condition_result.node(), message_handle);
			}
		}
		
		return saved_position.success();
	}
	
	if (!eval_result.success()) {
		// If we're inside a struct body, defer - our constexpr evaluator is incomplete
		// and many standard library static_asserts use complex constexpr functions
		if (!struct_parsing_context_stack_.empty()) {
			FLASH_LOG(Parser, Debug, "Deferring static_assert with unevaluable condition in struct body: ", eval_result.error_message);
			const auto& struct_ctx = struct_parsing_context_stack_.back();
			if (struct_ctx.struct_node) {
				StringHandle message_handle = StringTable::getOrInternStringHandle(message);
				struct_ctx.struct_node->add_deferred_static_assert(*condition_result.node(), message_handle);
			}
			return saved_position.success();
		}
		return ParseResult::error(
			"static_assert condition is not a constant expression: " + eval_result.error_message,
			static_assert_keyword
		);
	}

	// Check if the assertion failed
	if (!eval_result.as_bool()) {
		// In template contexts, static_assert may evaluate to false because
		// type traits like is_constructible<_Tp, _Args...> return false_type
		// for unknown/dependent types. Defer instead of failing.
		if (is_in_template_definition || is_in_template_struct) {
			FLASH_LOG(Templates, Debug, "Deferring static_assert that evaluated to false in template context");
			if (!struct_parsing_context_stack_.empty()) {
				const auto& struct_ctx = struct_parsing_context_stack_.back();
				if (struct_ctx.struct_node) {
					StringHandle message_handle = StringTable::getOrInternStringHandle(message);
					struct_ctx.struct_node->add_deferred_static_assert(*condition_result.node(), message_handle);
				}
			}
			return saved_position.success();
		}
		std::string error_msg = "static_assert failed";
		if (!message.empty()) {
			error_msg += ": " + message;
		}
		return ParseResult::error(error_msg, static_assert_keyword);
	}

	// static_assert passed - just skip it
	return saved_position.success();
}

// Helper function to try parsing a function pointer member in struct/union context
// Pattern: type (*name)(params);
// This assumes parse_type_specifier has already been called and the next token is '('
// Returns std::optional<StructMember> - empty if not a function pointer pattern
ParseResult Parser::parse_namespace() {
	ScopedTokenPosition saved_position(*this);

	// Detect if this namespace was prefixed with 'inline'
	bool is_inline_namespace = pending_inline_namespace_;
	pending_inline_namespace_ = false;

	// Consume 'namespace' keyword
	if (!consume("namespace"_tok)) {
		return ParseResult::error("Expected 'namespace' keyword", peek_info());
	}

	// Check if this is an anonymous namespace (namespace { ... })
	std::string_view namespace_name = "";
	bool is_anonymous = false;
	
	// C++17 nested namespace declarations: namespace A::B::C { }
	// This vector holds all namespace names for nested declarations
	std::vector<std::string_view> nested_names;
	// Track which nested namespaces are inline (parallel to nested_names)
	std::vector<bool> nested_inline_flags;

	if (peek() == "{"_tok) {
		// Anonymous namespace
		is_anonymous = true;
		// For anonymous namespaces, we'll use an empty name
		// The symbol table will handle them specially
		namespace_name = "";
	} else {
		// Named namespace - parse namespace name
		auto name_token = advance();
		if (!name_token.kind().is_identifier()) {
			return ParseResult::error("Expected namespace name or '{'", name_token);
		}
		namespace_name = name_token.value();
		
		// Collect all namespace names (including the first one for nested namespaces)
		// The first namespace gets the is_inline_namespace flag from 'inline namespace' prefix
		nested_names.push_back(namespace_name);
		nested_inline_flags.push_back(is_inline_namespace);
		
		// C++17 nested namespace declarations: namespace A::B::C { }
		// Also supports C++20: namespace A::inline B::C { }
		// Continue collecting nested namespace names if present
		while (peek() == "::"_tok) {
			advance(); // consume '::'
			
			// Check for inline keyword in nested namespace: namespace A::inline B { }
			bool nested_is_inline = false;
			if (peek() == "inline"_tok) {
				advance(); // consume 'inline'
				nested_is_inline = true;
			}
			
			auto nested_name_token = advance();
			if (!nested_name_token.kind().is_identifier()) {
				return ParseResult::error("Expected namespace name after '::'", nested_name_token);
			}
			nested_names.push_back(nested_name_token.value());
			nested_inline_flags.push_back(nested_is_inline);
		}

		// Skip any attributes after the namespace name (e.g., __attribute__((__abi_tag__ ("cxx11"))))
		skip_gcc_attributes();

		// Check if this is a namespace alias: namespace alias = target;
		if (peek() == "="_tok) {
			// This is a namespace alias, not a namespace declaration
			// Restore position and parse as namespace alias
			Token alias_token = name_token;
			advance(); // consume '='

			// Parse target namespace path
			std::vector<StringType<>> target_namespace;
			while (true) {
				auto ns_token = advance();
				if (!ns_token.kind().is_identifier()) {
					return ParseResult::error("Expected namespace name", ns_token);
				}
				target_namespace.emplace_back(StringType<>(ns_token.value()));

				// Check for ::
				if (peek() == "::"_tok) {
					advance(); // consume ::
				} else {
					break;
				}
			}

			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after namespace alias", current_token_);
			}

			// Convert namespace path to handle and add the alias to the symbol table
			NamespaceHandle target_handle = gSymbolTable.resolve_namespace_handle(target_namespace);
			gSymbolTable.add_namespace_alias(alias_token.value(), target_handle);

			auto alias_node = emplace_node<NamespaceAliasNode>(alias_token, target_handle);
			return saved_position.success(alias_node);
		}
	}

	// Inline namespaces inject their members into the enclosing namespace scope
	// For nested declarations like namespace A::inline B, B is inline within A
	// We now handle this per-namespace in the enter loop below, not just for the first namespace

	// Expect opening brace
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' after namespace name", peek_info());
	}

	// Create namespace declaration node - string_view points directly into source text
	// For anonymous namespaces, use empty string_view
	// For nested namespaces (A::B::C), we use the innermost name for the AST node
	// but enter all scopes in the symbol table
	std::string_view innermost_name = nested_names.empty() ? namespace_name : nested_names.back();
	auto [namespace_node, namespace_ref] = emplace_node_ref<NamespaceDeclarationNode>(is_anonymous ? "" : innermost_name);

	// Enter namespace scope(s) and handle inline namespaces
	// For anonymous namespaces, we DON'T enter a new scope in the symbol table
	// Instead, symbols are added to the current scope but tracked separately for mangling
	// This allows them to be accessed without qualification (per C++ standard)
	// while still getting unique linkage names
	// For nested namespaces (A::B::C), enter each scope in order
	if (!is_anonymous) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		
		for (size_t i = 0; i < nested_names.size(); ++i) {
			const auto& ns_name = nested_names[i];
			bool this_ns_is_inline = nested_inline_flags.size() > i && nested_inline_flags[i];
			StringHandle name_handle = StringTable::getOrInternStringHandle(ns_name);
			NamespaceHandle next_handle = gNamespaceRegistry.getOrCreateNamespace(current_handle, name_handle);
			
			// If this namespace is inline, add a using directive BEFORE entering
			// This makes members visible in the current (parent) scope
			if (this_ns_is_inline && next_handle.isValid()) {
				gSymbolTable.add_using_directive(next_handle);
			}
			
			if (next_handle.isValid()) {
				gSymbolTable.enter_namespace(next_handle);
				current_handle = next_handle;
			} else {
				gSymbolTable.enter_namespace(ns_name);
				current_handle = gSymbolTable.get_current_namespace_handle();
			}
		}
	}

	// Track inline namespace nesting (one entry per nested level for proper cleanup)
	for (size_t i = 0; i < (nested_names.empty() ? 1 : nested_names.size()); ++i) {
		bool this_is_inline = nested_inline_flags.size() > i && nested_inline_flags[i];
		inline_namespace_stack_.push_back(this_is_inline);
	}
	// For anonymous namespaces, track the namespace in the AST but not in symbol lookup
	// Symbols will be added to current scope during declaration parsing

	// Parse declarations within the namespace
	while (!peek().is_eof() && peek() != "}"_tok) {
		ParseResult decl_result;

		// Skip empty declarations (lone semicolons) - valid in C++ (e.g., namespace foo { }; )
		if (peek() == ";"_tok) {
			advance();
			continue;
		}

		// Check if it's a using directive, using declaration, or namespace alias
		if (peek() == "using"_tok) {
			decl_result = parse_using_directive_or_declaration();
		}
		// Check if it's a nested namespace (or inline namespace)
		else if (peek() == "namespace"_tok) {
			decl_result = parse_namespace();
		}
		// Check if it's an inline namespace (inline namespace __cxx11 { ... })
		else if (peek() == "inline"_tok) {
			auto next = peek_info(1);
			if (next.kind() == "namespace"_tok) {
				advance(); // consume 'inline'
				pending_inline_namespace_ = true;
				decl_result = parse_namespace(); // parse_namespace handles the rest
			} else {
				// Just a regular inline declaration
				decl_result = parse_declaration_or_function_definition();
			}
		}
		// Check if it's a struct/class/union declaration
		else if ((peek() == "class"_tok || peek() == "struct"_tok || peek() == "union"_tok)) {
			decl_result = parse_struct_declaration();
		}
		// Check if it's an enum declaration
		else if (peek() == "enum"_tok) {
			decl_result = parse_enum_declaration();
		}
		// Check if it's a typedef declaration
		else if (peek() == "typedef"_tok) {
			decl_result = parse_typedef_declaration();
		}
		// Check if it's a template declaration
		else if (peek() == "template"_tok) {
			decl_result = parse_template_declaration();
		}
		// Check if it's an extern declaration (extern "C" or extern "C++")
		else if (peek() == "extern"_tok) {
			// Save position in case this is just a regular extern declaration
			SaveHandle extern_saved_pos = save_token_position();
			advance(); // consume 'extern'

			// Check if this is extern "C" or extern "C++"
			if (peek().is_string_literal()) {
				std::string_view linkage_str = peek_info().value();

				// Remove quotes from string literal
				if (linkage_str.size() >= 2 && linkage_str.front() == '"' && linkage_str.back() == '"') {
					linkage_str = linkage_str.substr(1, linkage_str.size() - 2);
				}

				Linkage linkage = Linkage::None;
				if (linkage_str == "C") {
					linkage = Linkage::C;
				} else if (linkage_str == "C++") {
					linkage = Linkage::CPlusPlus;
				} else {
					if (!is_anonymous) {
						gSymbolTable.exit_scope();
					}
					return ParseResult::error("Unknown linkage specification: " + std::string(linkage_str), current_token_);
				}

				advance(); // consume linkage string
				discard_saved_token(extern_saved_pos);

				// Check for block form: extern "C" { ... }
				if (peek() == "{"_tok) {
					decl_result = parse_extern_block(linkage);
				} else {
					// Single declaration form: extern "C++" int func();
					Linkage saved_linkage = current_linkage_;
					current_linkage_ = linkage;
					decl_result = parse_declaration_or_function_definition();
					current_linkage_ = saved_linkage;
				}
			} else if (peek() == "template"_tok) {
				// extern template class allocator<char>; — explicit instantiation declaration
				discard_saved_token(extern_saved_pos);
				decl_result = parse_template_declaration();
			} else {
				// Regular extern declaration (not extern "C")
				restore_token_position(extern_saved_pos);
				decl_result = parse_declaration_or_function_definition();
			}
		}
		// Otherwise, parse as function or variable declaration
		else {
			decl_result = parse_declaration_or_function_definition();
		}

		if (decl_result.is_error()) {
			// Exit all nested namespace scopes on error
			if (!is_anonymous) {
				size_t nesting_depth = nested_names.empty() ? 1 : nested_names.size();
				for (size_t i = 0; i < nesting_depth; ++i) {
					gSymbolTable.exit_scope();
				}
			}
			return decl_result;
		}

		if (auto node = decl_result.node()) {
			namespace_ref.add_declaration(*node);
		}
	}

	// Expect closing brace
	if (!consume("}"_tok)) {
		// Exit all nested namespace scopes on error
		if (!is_anonymous) {
			size_t nesting_depth = nested_names.empty() ? 1 : nested_names.size();
			for (size_t i = 0; i < nesting_depth; ++i) {
				gSymbolTable.exit_scope();
				inline_namespace_stack_.pop_back();
			}
		} else {
			inline_namespace_stack_.pop_back();
		}
		return ParseResult::error("Expected '}' after namespace body", peek_info());
	}

	// Exit namespace scope(s) (only for named namespaces, not anonymous)
	// For nested namespaces (A::B::C), exit each scope in reverse order
	if (!is_anonymous) {
		size_t nesting_depth = nested_names.empty() ? 1 : nested_names.size();
		for (size_t i = 0; i < nesting_depth; ++i) {
			gSymbolTable.exit_scope();
			inline_namespace_stack_.pop_back();
		}
	} else {
		inline_namespace_stack_.pop_back();
	}

	// Merge inline namespace symbols into parent namespace for qualified lookup
	// We need to do this for each inline namespace in the chain
	// Capture the path AFTER exiting scopes (we're back to original scope)
	if (!is_anonymous && !nested_inline_flags.empty()) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		for (size_t i = 0; i < nested_names.size(); ++i) {
			bool this_is_inline = nested_inline_flags.size() > i && nested_inline_flags[i];
			StringHandle name_handle = StringTable::getOrInternStringHandle(nested_names[i]);
			NamespaceHandle inline_handle = gNamespaceRegistry.getOrCreateNamespace(current_handle, name_handle);
			if (this_is_inline) {
				gSymbolTable.merge_inline_namespace(inline_handle, current_handle);
			}
			current_handle = inline_handle;
		}
	}

	return saved_position.success(namespace_node);
}

ParseResult Parser::parse_using_directive_or_declaration() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'using' keyword
	auto using_token_opt = peek_info();
	if (using_token_opt.kind() != "using"_tok) {
		return ParseResult::error("Expected 'using' keyword", using_token_opt);
	}
	Token using_token = using_token_opt;
	advance(); // consume 'using'

	// Check if this is a type alias or namespace alias: using identifier = ...;
	// We need to look ahead to see if there's an '=' after the first identifier
	// (possibly with [[attributes]] in between)
	SaveHandle lookahead_pos = save_token_position();
	auto first_token = peek_info();
	if (first_token.kind().is_identifier()) {
		advance(); // consume identifier
		// Skip attributes in lookahead: using name [[deprecated]] = type;
		skip_cpp_attributes();
		auto next_token = peek_info();
		if (next_token.kind() == "="_tok) {
			// This is either a type alias or namespace alias: using alias = type/namespace;
			restore_token_position(lookahead_pos);

			// Parse alias name
			auto alias_token = advance();
			if (!alias_token.kind().is_identifier()) {
				return ParseResult::error("Expected alias name after 'using'", current_token_);
			}

			// Skip C++ attributes like [[__deprecated__]] between name and '='
			skip_cpp_attributes();

			// Consume '='
			if (peek().is_eof() || peek_info().type() != Token::Type::Operator || peek() != "="_tok) {
				return ParseResult::error("Expected '=' after alias name", current_token_);
			}
			advance(); // consume '='

			// Try to parse as a type specifier (for type aliases like: using value_type = T;)
			ParseResult type_result = parse_type_specifier();
			if (!type_result.is_error()) {
				// Parse any pointer/reference modifiers after the type
				// For example: using RvalueRef = typename T::type&&;
				if (type_result.node().has_value()) {
					TypeSpecifierNode type_spec = type_result.node()->as<TypeSpecifierNode>();
					
					// Check for pointer-to-member type syntax: Type Class::*
					// This is used in <type_traits> for result_of patterns
					// Pattern: using _MemPtr = _Res _Class::*;
					if (peek().is_identifier()) {
						// Look ahead to see if this is Class::* pattern
						auto saved_pos = save_token_position();
						Token class_token = peek_info();
						advance(); // consume potential class name
						
						if (peek() == "::"_tok) {
							advance(); // consume '::'
							if (peek() == "*"_tok) {
								advance(); // consume '*'
								// This is a pointer-to-member type: Type Class::*
								// Mark the type as a pointer-to-member
								type_spec.add_pointer_level(CVQualifier::None);  // Add pointer level
								type_spec.set_member_class_name(class_token.handle());
								FLASH_LOG(Parser, Debug, "Parsed pointer-to-member type: ", type_spec.token().value(), " ", class_token.value(), "::*");
								discard_saved_token(saved_pos);
							} else {
								// Not a pointer-to-member, restore position
								restore_token_position(saved_pos);
							}
						} else {
							// Not a pointer-to-member, restore position
							restore_token_position(saved_pos);
						}
					}
					
					// Parse pointer declarators: * [const] [volatile] *...
					while (peek() == "*"_tok) {
						advance(); // consume '*'
						
						// Check for CV-qualifiers after the *
						CVQualifier ptr_cv = parse_cv_qualifiers();
						
						type_spec.add_pointer_level(ptr_cv);
					}
					
					// Check for function pointer/reference type syntax: ReturnType (&)(...) or ReturnType (*)(...) 
					// Pattern: Type (&)() = lvalue reference to function returning Type
					// Pattern: Type (&&)() = rvalue reference to function returning Type
					// Pattern: Type (*)() = pointer to function returning Type
					// This handles types like: int (&)(), _Xp (&)(), etc.
					if (peek() == "("_tok) {
						auto func_type_saved_pos = save_token_position();
						advance(); // consume '('
						
						// Check what's inside the parentheses: &, &&, or *
						bool is_function_ref = false;
						bool is_rvalue_function_ref = false;
						bool is_function_ptr = false;
						
						if (!peek().is_eof()) {
							if (peek() == "&&"_tok) {
								is_rvalue_function_ref = true;
								advance(); // consume '&&'
							} else if (peek() == "&"_tok) {
								is_function_ref = true;
								advance(); // consume '&'
							} else if (peek() == "*"_tok) {
								is_function_ptr = true;
								advance(); // consume '*'
							}
						}
						
						// After &, &&, or *, expect ')'
						if ((is_function_ref || is_rvalue_function_ref || is_function_ptr) &&
						    peek() == ")"_tok) {
							advance(); // consume ')'
							
							// Now expect '(' for the parameter list
							if (peek() == "("_tok) {
								advance(); // consume '('
								
								// Parse parameter list (can be empty or have parameters)
								std::vector<Type> param_types;
								while (!peek().is_eof() && peek() != ")"_tok) {
									// Skip parameter - can be complex types
									auto param_type_result = parse_type_specifier();
									if (!param_type_result.is_error() && param_type_result.node().has_value()) {
										const TypeSpecifierNode& param_type = param_type_result.node()->as<TypeSpecifierNode>();
										param_types.push_back(param_type.type());
									}
									
									// Check for comma
									if (peek() == ","_tok) {
										advance(); // consume ','
									} else {
										break;
									}
								}
								
								if (peek() == ")"_tok) {
									advance(); // consume ')'
									
									// Successfully parsed function reference/pointer type!
									FunctionSignature func_sig;
									func_sig.return_type = type_spec.type();
									func_sig.parameter_types = std::move(param_types);
									
									if (is_function_ptr) {
										type_spec.add_pointer_level(CVQualifier::None);
									}
									type_spec.set_function_signature(func_sig);
									
									if (is_function_ref) {
										type_spec.set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference
									} else if (is_rvalue_function_ref) {
										type_spec.set_reference_qualifier(ReferenceQualifier::RValueReference);   // rvalue reference
									}
									
									FLASH_LOG(Parser, Debug, "Parsed function reference/pointer type in global alias: ", 
									          is_function_ptr ? "pointer" : (is_rvalue_function_ref ? "rvalue ref" : "lvalue ref"),
									          " to function");
									
									// Discard saved position - we successfully parsed
									discard_saved_token(func_type_saved_pos);
								} else {
									// Parsing failed - restore position
									restore_token_position(func_type_saved_pos);
								}
							} else {
								// No parameter list follows - restore position
								restore_token_position(func_type_saved_pos);
							}
						} else {
							// Not a function type syntax - restore position
							restore_token_position(func_type_saved_pos);
						}
					}
					
					// Parse reference declarators: & or &&
					ReferenceQualifier ref_qual = parse_reference_qualifier();
					if (ref_qual != ReferenceQualifier::None) {
						type_spec.set_reference_qualifier(ref_qual);
					}
					
					// Parse array dimensions: using _Type = _Tp[_Nm];
					while (peek() == "["_tok) {
						advance(); // consume '['
						if (peek() == "]"_tok) {
							type_spec.set_array(true);
							advance(); // consume ']'
						} else {
							auto dim_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (dim_result.is_error()) {
								return dim_result;
							}
							auto dim_val = try_evaluate_constant_expression(*dim_result.node());
							size_t dim_size = dim_val.has_value() ? static_cast<size_t>(dim_val->value) : 0;
							type_spec.add_array_dimension(dim_size);
							if (!consume("]"_tok)) {
								return ParseResult::error("Expected ']' after array dimension in type alias", current_token_);
							}
						}
					}
					
					// This is a type alias
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after type alias", current_token_);
					}

					// Register the type alias in gTypesByName
					// Create a TypeInfo for the alias that points to the underlying type
					auto& alias_type_info = gTypeInfo.emplace_back(alias_token.handle(), type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
					alias_type_info.pointer_depth_ = type_spec.pointer_depth();
					alias_type_info.reference_qualifier_ = type_spec.reference_qualifier();
					// Copy function signature for function pointer/reference type aliases
					if (type_spec.has_function_signature()) {
						alias_type_info.function_signature_ = type_spec.function_signature();
					}
					
					gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
					
					// Also register with namespace-qualified name for type aliases defined in namespaces
					NamespaceHandle namespace_handle = gSymbolTable.get_current_namespace_handle();
					if (!namespace_handle.isGlobal()) {
						StringHandle alias_handle = alias_token.handle();
						auto full_qualified_name = gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, alias_handle);
						if (gTypesByName.find(full_qualified_name) == gTypesByName.end()) {
							gTypesByName.emplace(full_qualified_name, &alias_type_info);
							FLASH_LOG_FORMAT(Parser, Debug, "Registered type alias '{}' with namespace-qualified name '{}'",
							                 alias_token.value(), StringTable::getStringView(full_qualified_name));
						}
					}

					// Return success (no AST node needed for type aliases)
					return saved_position.success();
				}
				
				// If we didn't get a node from parse_type_specifier, just check for semicolon
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after type alias", current_token_);
				}
				return saved_position.success();
			} else if (parsing_template_body_ || gSymbolTable.get_current_scope_type() == ScopeType::Function) {
				// If we're in a template body OR function body and type parsing failed, it's likely a template-dependent type
				// or a complex type expression during template instantiation.
				// Skip to semicolon and continue (template aliases with dependent types can't be fully resolved now).
				// For function-local type aliases (like in template instantiations), they're often not needed
				// for code generation as the actual type is already known from the return type.
				FLASH_LOG(Parser, Debug, "Skipping unparseable using declaration in ", 
				          parsing_template_body_ ? "template body" : "function body");
				while (!peek().is_eof() && peek() != ";"_tok) {
					advance();
				}
				if (consume(";"_tok)) {
					// Successfully skipped the template-dependent using declaration
					return saved_position.success();
				}
				return ParseResult::error("Expected ';' after using declaration", current_token_);
			}

			// Not a type alias, try parsing as namespace path for namespace alias
			std::vector<StringType<>> target_namespace;
			while (true) {
				auto ns_token = advance();
				if (!ns_token.kind().is_identifier()) {
					return ParseResult::error("Expected type or namespace name", ns_token);
				}
				target_namespace.emplace_back(StringType<>(ns_token.value()));

				// Check for ::
				if (peek() == "::"_tok) {
					advance(); // consume ::
				} else {
					break;
				}
			}

			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after namespace alias", current_token_);
			}

			// Convert namespace path to handle and add the alias to the symbol table
			NamespaceHandle target_handle = gSymbolTable.resolve_namespace_handle(target_namespace);
			gSymbolTable.add_namespace_alias(alias_token.value(), target_handle);

			auto alias_node = emplace_node<NamespaceAliasNode>(alias_token, target_handle);
			return saved_position.success(alias_node);
		}
	}
	// Not a namespace alias, restore position
	restore_token_position(lookahead_pos);

	// Check if this is "using namespace" directive
	if (peek() == "namespace"_tok) {
		advance(); // consume 'namespace'

		// Parse namespace path (e.g., std::filesystem)
		std::vector<StringType<>> namespace_path;
		while (true) {
			auto ns_token = advance();
			if (!ns_token.kind().is_identifier()) {
				return ParseResult::error("Expected namespace name", ns_token);
			}
			namespace_path.emplace_back(StringType<>(ns_token.value()));

			// Check for ::
			if (peek() == "::"_tok) {
				advance(); // consume ::
			} else {
				break;
			}
		}

		// Expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after using directive", current_token_);
		}

		// Convert namespace path to handle and add the using directive to the symbol table
		NamespaceHandle namespace_handle = gSymbolTable.resolve_namespace_handle(namespace_path);
		gSymbolTable.add_using_directive(namespace_handle);

		auto directive_node = emplace_node<UsingDirectiveNode>(namespace_handle, using_token);
		return saved_position.success(directive_node);
	}

	// Check if this is C++20 "using enum" declaration
	if (peek() == "enum"_tok) {
		advance(); // consume 'enum'

		// Parse enum type name (can be qualified: namespace::EnumType or just EnumType)
		std::vector<StringType<>> namespace_path;
		Token enum_type_token;

		while (true) {
			auto token = advance();
			if (!token.kind().is_identifier()) {
				return ParseResult::error("Expected enum type name after 'using enum'", token);
			}

			// Check if followed by ::
			if (peek() == "::"_tok) {
				// This is a namespace part
				namespace_path.emplace_back(StringType<>(token.value()));
				advance(); // consume ::
			} else {
				// This is the final enum type name
				enum_type_token = token;
				break;
			}
		}

		// Expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after 'using enum' declaration", current_token_);
		}

		// Create the using enum node - CodeGen will also handle this for its local scope
		StringHandle enum_name_handle = enum_type_token.handle();
		auto using_enum_node = emplace_node<UsingEnumNode>(enum_name_handle, using_token);
		
		// Add enumerators to gSymbolTable NOW so they're available during parsing
		// This is needed because the parser needs to resolve identifiers like 'Red' when
		// parsing subsequent expressions (e.g., static_cast<int>(Red))
		auto type_it = gTypesByName.find(enum_name_handle);
		if (type_it != gTypesByName.end() && type_it->second->getEnumInfo()) {
			const EnumTypeInfo* enum_info = type_it->second->getEnumInfo();
			
			for (const auto& enumerator : enum_info->enumerators) {
				// Create a type node for the enum type
				auto enum_type_node = emplace_node<TypeSpecifierNode>(
					Type::Enum, type_it->second->type_index_, enum_info->underlying_size, enum_type_token);
				
				// Create a declaration node for the enumerator
				Token enumerator_token(Token::Type::Identifier, 
					StringTable::getStringView(enumerator.getName()), 0, 0, 0);
				auto enumerator_decl = emplace_node<DeclarationNode>(enum_type_node, enumerator_token);
				
				// Insert into gSymbolTable so it's available during parsing
				gSymbolTable.insert(StringTable::getStringView(enumerator.getName()), enumerator_decl);
			}
			
			FLASH_LOG(Parser, Debug, "Using enum '", enum_type_token.value(), 
				"' - added ", enum_info->enumerators.size(), " enumerators to parser scope");
		} else {
			FLASH_LOG(General, Error, "Enum type '", enum_type_token.value(), 
				"' not found for 'using enum' declaration");
		}
		
		return saved_position.success(using_enum_node);
	}

	// Otherwise, this is a using declaration: using std::vector; or using ::name;
	std::vector<StringType<>> namespace_path;
	Token identifier_token;

	// Check if this starts with :: (global namespace scope)
	if (peek() == "::"_tok) {
		advance(); // consume leading ::
		// After the leading ::, we need to parse the qualified name
		// This could be ::name or ::namespace::name
		while (true) {
			auto token = advance();
			if (!token.kind().is_identifier()) {
				return ParseResult::error("Expected identifier after :: in using declaration", token);
			}

			// Check if followed by ::
			if (peek() == "::"_tok) {
				// This is a namespace part
				namespace_path.emplace_back(StringType<>(token.value()));
				advance(); // consume ::
			} else {
				// This is the final identifier
				identifier_token = token;
				break;
			}
		}
	} else {
		// Parse qualified name (namespace::...::identifier)
		while (true) {
			auto token = advance();
			if (!token.kind().is_identifier()) {
				return ParseResult::error("Expected identifier in using declaration", token);
			}

			// Check if followed by ::
			if (peek() == "::"_tok) {
				// This is a namespace part
				namespace_path.emplace_back(StringType<>(token.value()));
				advance(); // consume ::
			} else {
				// This is the final identifier
				identifier_token = token;
				break;
			}
		}
	}

	// Expect semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after using declaration", current_token_);
	}

	// Convert namespace path to handle and add the using declaration to the symbol table
	NamespaceHandle namespace_handle = gSymbolTable.resolve_namespace_handle(namespace_path);
	gSymbolTable.add_using_declaration(
		std::string_view(identifier_token.value()),
		namespace_handle,
		std::string_view(identifier_token.value())
	);

	// Check if the identifier refers to an existing type in the source namespace
	// Build the source type name (either global or qualified with namespace_path)
	StringHandle source_type_name;
	if (namespace_path.empty()) {
		// using ::identifier; - refers to global namespace
		source_type_name = identifier_token.handle();
	} else {
		// using ns1::ns2::identifier; - build qualified name
		StringHandle identifier_handle = identifier_token.handle();
		source_type_name = namespace_handle.isValid()
			? gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, identifier_handle)
			: identifier_handle;
	}
	
	// Look up the type in gTypesByName
	auto existing_type_it = gTypesByName.find(source_type_name);
	
	// If not found with qualified name, try the unqualified name
	// This handles cases like: using ::__gnu_cxx::lldiv_t; where __gnu_cxx::lldiv_t
	// might itself be an alias to ::lldiv_t
	if (existing_type_it == gTypesByName.end() && !namespace_path.empty()) {
		StringHandle qualified_source = source_type_name;  // Save the qualified name for logging
		StringHandle unqualified_source = identifier_token.handle();
		auto unqualified_it = gTypesByName.find(unqualified_source);
		if (unqualified_it != gTypesByName.end()) {
			existing_type_it = unqualified_it;
			source_type_name = unqualified_source;  // Update to use the unqualified name that was found
			FLASH_LOG_FORMAT(Parser, Debug, "Using declaration: qualified name {} not found, using unqualified name {}", 
			                 StringTable::getStringView(qualified_source), StringTable::getStringView(unqualified_source));
		}
	}
	
	// If we're inside a namespace, we need to register the type with a qualified name
	// so that "std::lldiv_t" can be recognized as a type
	NamespaceHandle current_namespace_handle = gSymbolTable.get_current_namespace_handle();
	if (!current_namespace_handle.isGlobal()) {
		// Build qualified name for the target: std::identifier
		StringHandle identifier_handle = identifier_token.handle();
		StringHandle target_type_name = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace_handle, identifier_handle);
		
		// Check if target name is already registered (avoid duplicates)
		if (gTypesByName.find(target_type_name) == gTypesByName.end()) {
			if (existing_type_it != gTypesByName.end()) {
				// Found existing type - create alias pointing to it
				const TypeInfo* source_type = existing_type_it->second;
				auto& alias_type_info = gTypeInfo.emplace_back(target_type_name, source_type->type_, source_type->type_index_, source_type->type_size_);
				alias_type_info.pointer_depth_ = source_type->pointer_depth_;
				
				// If the source type has StructInfo, we don't copy it - we rely on type_index_ to point to it
				// This is the same pattern used for typedef resolution
				
				gTypesByName.emplace(target_type_name, &alias_type_info);
				FLASH_LOG_FORMAT(Parser, Debug, "Registered type alias from using declaration: {} -> {}", 
				                 StringTable::getStringView(target_type_name), StringTable::getStringView(source_type_name));
				
				// Also register with the unqualified name within the current namespace scope
				// This allows code inside the namespace to use the type without qualification
				// e.g., inside namespace std, both "std::lldiv_t" and "lldiv_t" should work
				StringHandle unqualified_name = identifier_token.handle();
				if (gTypesByName.find(unqualified_name) == gTypesByName.end()) {
					gTypesByName.emplace(unqualified_name, &alias_type_info);
					FLASH_LOG_FORMAT(Parser, Debug, "Also registered unqualified type name: {}", 
					                 StringTable::getStringView(unqualified_name));
				}
			}
		}
	}

	auto decl_node = emplace_node<UsingDeclarationNode>(namespace_handle, identifier_token, using_token);
	return saved_position.success(decl_node);
}

