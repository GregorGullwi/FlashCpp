ParseResult Parser::parse_block()
{
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' for block", current_token_);
	}

	// Enter a new scope for this block (C++ standard: each block creates a scope)
	FlashCpp::SymbolTableScope block_scope(ScopeType::Block);

	FLASH_LOG_FORMAT(Parser, Debug, "parse_block: Entered block. peek={}", 
		std::string(peek_info().value()));

	auto [block_node, block_ref] = create_node_ref(BlockNode());

	while (!consume("}"_tok)) {
		// Parse statements or declarations
		FLASH_LOG_FORMAT(Parser, Debug, "parse_block: About to parse_statement_or_declaration. peek={}", 
			std::string(peek_info().value()));
		ParseResult parse_result = parse_statement_or_declaration();
		FLASH_LOG_FORMAT(Parser, Debug, "parse_block: parse_statement_or_declaration returned. is_error={}, peek={}", 
			parse_result.is_error(),
			std::string(peek_info().value()));
		if (parse_result.is_error())
			return parse_result;

		if (auto node = parse_result.node()) {
			block_ref.add_statement_node(*node);  // Unwrap optional before passing
		}

		// Add any pending variable declarations from struct definitions
		for (auto& var_node : pending_struct_variables_) {
			block_ref.add_statement_node(var_node);
		}
		pending_struct_variables_.clear();

		consume(";"_tok);
	}

	return ParseResult::success(block_node);
}

ParseResult Parser::parse_statement_or_declaration()
{
	// Clear any leaked pending template arguments from previous expression parsing.
	// This prevents template args from one expression leaking into unrelated function calls.
	pending_explicit_template_args_.reset();

	// Define a function pointer type for parsing functions
	using ParsingFunction = ParseResult(Parser::*)();

	if (peek().is_eof()) {
		return ParseResult::error("Expected a statement or declaration",
			current_token_);
	}
	const Token& current_token = peek_info();

	FLASH_LOG_FORMAT(Parser, Debug, "parse_statement_or_declaration: current_token={}, type={}", 
		std::string(current_token.value()),
		current_token.type() == Token::Type::Keyword ? "Keyword" : 
		current_token.type() == Token::Type::Identifier ? "Identifier" : "Other");

	// Handle nested blocks
	if (peek() == "{"_tok) {
		// parse_block() creates its own scope, so no need to create one here
		return parse_block();
	}

	// Handle ::new, ::delete, and ::operator new/delete expressions at statement level
	if (peek() == "::"_tok) {
		auto next_kind = peek(1);
		if (next_kind == "new"_tok || next_kind == "delete"_tok || next_kind == "operator"_tok) {
			// This is a globally qualified new/delete/operator expression
			return parse_expression_statement();
		}
	}

	if (current_token.type() == Token::Type::Keyword) {
		// Keyword parsing function map - initialized once on first call
		static const std::unordered_map<std::string_view, ParsingFunction> keyword_parsing_functions = {
			{"if", &Parser::parse_if_statement},
			{"for", &Parser::parse_for_loop},
			{"while", &Parser::parse_while_loop},
			{"do", &Parser::parse_do_while_loop},
			{"switch", &Parser::parse_switch_statement},
			{"return", &Parser::parse_return_statement},
			{"break", &Parser::parse_break_statement},
			{"continue", &Parser::parse_continue_statement},
			{"goto", &Parser::parse_goto_statement},
			{"try", &Parser::parse_try_statement},
			{"throw", &Parser::parse_throw_statement},
			{"using", &Parser::parse_using_directive_or_declaration},
			{"namespace", &Parser::parse_namespace},
			{"typedef", &Parser::parse_typedef_declaration},
			{"template", &Parser::parse_template_declaration},
			{"struct", &Parser::parse_struct_declaration},
			{"class", &Parser::parse_struct_declaration},
			{"union", &Parser::parse_struct_declaration},
			{"static", &Parser::parse_variable_declaration},
			{"extern", &Parser::parse_variable_declaration},
			{"register", &Parser::parse_variable_declaration},
			{"mutable", &Parser::parse_variable_declaration},
			{"constexpr", &Parser::parse_variable_declaration},
			{"constinit", &Parser::parse_variable_declaration},
			{"consteval", &Parser::parse_variable_declaration},
			{"int", &Parser::parse_variable_declaration},
			{"float", &Parser::parse_variable_declaration},
			{"double", &Parser::parse_variable_declaration},
			{"char", &Parser::parse_variable_declaration},
			{"wchar_t", &Parser::parse_variable_declaration},
			{"char8_t", &Parser::parse_variable_declaration},
			{"char16_t", &Parser::parse_variable_declaration},
			{"char32_t", &Parser::parse_variable_declaration},
			{"bool", &Parser::parse_variable_declaration},
			{"void", &Parser::parse_declaration_or_function_definition},
			{"short", &Parser::parse_variable_declaration},
			{"long", &Parser::parse_variable_declaration},
			{"signed", &Parser::parse_variable_declaration},
			{"unsigned", &Parser::parse_variable_declaration},
			{"const", &Parser::parse_variable_declaration},
			{"volatile", &Parser::parse_variable_declaration},
			{"alignas", &Parser::parse_variable_declaration},
			{"auto", &Parser::parse_variable_declaration},
			{"decltype", &Parser::parse_variable_declaration},  // C++11 decltype type specifier
			// Microsoft-specific type keywords
			{"__int8", &Parser::parse_variable_declaration},
			{"__int16", &Parser::parse_variable_declaration},
			{"__int32", &Parser::parse_variable_declaration},
			{"__int64", &Parser::parse_variable_declaration},
			{"new", &Parser::parse_expression_statement},
			{"delete", &Parser::parse_expression_statement},
			{"this", &Parser::parse_expression_statement},
			{"static_cast", &Parser::parse_expression_statement},
			{"dynamic_cast", &Parser::parse_expression_statement},
			{"const_cast", &Parser::parse_expression_statement},
			{"reinterpret_cast", &Parser::parse_expression_statement},
			{"typeid", &Parser::parse_expression_statement},
			{"static_assert", &Parser::parse_static_assert},
		};

		auto keyword_iter = keyword_parsing_functions.find(current_token.value());
		if (keyword_iter != keyword_parsing_functions.end()) {
			// Call the appropriate parsing function
			FLASH_LOG_FORMAT(Parser, Debug, "parse_statement_or_declaration: Found keyword '{}', calling handler", 
				std::string(current_token.value()));
			return (this->*(keyword_iter->second))();
		}

		// Unknown keyword - consume token to avoid infinite loop and return error
		advance();
		return ParseResult::error("Unknown keyword: " + std::string(current_token.value()),
			current_token);
	}
	else if (current_token.type() == Token::Type::Identifier) {
		// Check if this is a label (identifier followed by ':')
		// We need to look ahead to see if there's a colon
		SaveHandle saved_pos = save_token_position();
		advance(); // consume the identifier
		if (peek() == ":"_tok) {
			// This is a label statement
			restore_token_position(saved_pos);
			return parse_label_statement();
		}
		// Not a label, restore position and continue
		restore_token_position(saved_pos);

		// Check if this identifier is a registered struct/class/enum/typedef type
		StringBuilder type_name_builder;
		type_name_builder.append(current_token.value());
		
		// Check for qualified name (e.g., std::size_t, ns::MyClass)
		// Need to look ahead to see if there's a :: following
		saved_pos = save_token_position();
		advance(); // consume first identifier
		while (peek() == "::"_tok) {
			advance(); // consume '::'
			if (peek().is_identifier()) {
				type_name_builder.append("::").append(peek_info().value());
				advance(); // consume next identifier
			} else {
				break;
			}
		}
		restore_token_position(saved_pos);
		
		auto type_name_handle = StringTable::getOrInternStringHandle(type_name_builder);
		auto type_info_ctx = lookupTypeInCurrentContext(type_name_handle);
		if (type_info_ctx) {
			// Check if it's a struct, enum, or typedef (but not a struct/enum that happens to have type_size_ set)
			bool is_typedef = (type_info_ctx->type_size_ > 0 && !type_info_ctx->isStruct() && !type_info_ctx->isEnum());
			if (type_info_ctx->isStruct() || type_info_ctx->isEnum() || is_typedef) {
				// Need to check if this is a functional cast / temporary construction 
				// followed by a member access, like: TypeName(args).member()
				// vs a variable declaration: TypeName varname(args);
				SaveHandle check_pos = save_token_position();
				advance();  // consume type name
				
				// Handle qualified names and template args
				while (peek() == "::"_tok) {
					advance();  // consume '::'
					if (peek().is_identifier()) {
						advance();  // consume next identifier
					} else {
						break;
					}
				}
				// Handle template arguments if any
				if (peek() == "<"_tok) {
					int angle_depth = 1;
					advance();  // consume '<'
					while (angle_depth > 0 && !peek().is_eof()) {
						if (peek() == "<"_tok) {
							advance();
							angle_depth++;
						} else if (peek() == ">"_tok) {
							advance();
							angle_depth--;
						} else if (peek() == ">>"_tok) {
							// Split >> into two > tokens for nested templates
							split_right_shift_token();
							advance();  // consume first >
							angle_depth--;
						} else {
							// Some other token inside template args, just consume it
							advance();
						}
					}
				}
				
				// If followed by '::member(' after type/template args, this is a qualified member function call
				// e.g., Base<T>::deallocate(args) is a static member function call
				// But Type<T>::type is a type alias used in a variable declaration
				if (peek() == "::"_tok) {
					SaveHandle scope_check = save_token_position();
					advance(); // consume '::'
					if (peek().is_identifier()) {
						advance(); // consume member name
						if (peek() == "("_tok) {
							// This is Type<T>::member(...) - a function call expression
							restore_token_position(scope_check);
							restore_token_position(check_pos);
							return parse_expression_statement();
						}
					}
					restore_token_position(scope_check);
				}
				
				if (peek() == "("_tok) {
					// TypeName(...) - could be declaration or functional cast
					// Skip to matching )
					advance();  // consume '('
					int paren_depth = 1;
					while (paren_depth > 0 && !peek().is_eof()) {
						auto tok = advance();
						if (tok.value() == "(") paren_depth++;
						else if (tok.value() == ")") paren_depth--;
					}
					
					// Check what follows the )
					if (!peek().is_eof()) {
						auto next_val = peek_info().value();
						// If followed by . or ->, this is an expression (temporary construction)
						if (next_val == "." || next_val == "->") {
							restore_token_position(check_pos);
							return parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						}
					}
				}
				restore_token_position(check_pos);
				
				// This is a struct/enum/typedef type declaration
				return parse_variable_declaration();
			}
		}
		
		// Check if this is a template identifier (e.g., Container<int>::Iterator)
		// Templates need to be parsed as variable declarations
		// UNLESS the next token is '(' (which indicates a function template call)
		bool is_template = gTemplateRegistry.lookupTemplate(type_name_handle).has_value();
		bool is_alias_template = gTemplateRegistry.lookup_alias_template(type_name_handle).has_value();
		
		if (is_template || is_alias_template) {
			// We need to consume the full qualified name to peek at what comes after
			advance(); // consume the first identifier
			// If the template name is qualified (e.g., ns::myfunc), consume all :: and identifiers
			while (peek() == "::"_tok) {
				advance(); // consume '::'
				if (peek().is_identifier()) {
					advance(); // consume next identifier
				} else {
					break;
				}
			}
			// Peek ahead to see if this is a function call (template_name(...))
			// or a variable declaration (template_name<...> var)
			if (!peek().is_eof()) {
				if (peek() == "("_tok) {
					// Restore position before the identifier so parse_expression can handle it
					restore_token_position(saved_pos);
					// This is a function call, parse as expression
					return parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				}
				// Check for template<args>::member( pattern (e.g., Base<T>::deallocate(args))
				// This is a qualified member function call expression, not a variable declaration
				// But template<args>::type is a type alias - only treat as expression if followed by '('
				if (peek() == "<"_tok) {
					// Lookahead: skip template args to check what follows
					SaveHandle template_check = save_token_position();
					skip_template_arguments();
					if (peek() == "("_tok) {
						// template<args>(...) - could be function call or functional cast
						// Check if the template is a function template
						auto tmpl_opt = gTemplateRegistry.lookupTemplate(type_name_handle);
						if (tmpl_opt && is_function_or_template_function(*tmpl_opt)) {
							// This is a function template call: func<Args>(...)
							restore_token_position(template_check);
							restore_token_position(saved_pos);
							return parse_expression_statement();
						}
					}
					if (peek() == "::"_tok) {
						advance(); // consume '::'
						if (peek().is_identifier()) {
							advance(); // consume member name
							if (peek() == "("_tok) {
								// This is Base<T>::member(...) - a function call expression
								restore_token_position(template_check);
								restore_token_position(saved_pos);
								return parse_expression_statement();
							}
						}
					}
					restore_token_position(template_check);
				}
			}
			// Restore position before the identifier so parse_variable_declaration can handle it
			restore_token_position(saved_pos);
			// Otherwise, it's a variable declaration with a template type
			return parse_variable_declaration();
		}

		// Check if this identifier is a template parameter name (e.g., T in template<typename T>)
		// Template parameters can be used as types in variable declarations like "T result = value;"
		if (!current_template_param_names_.empty()) {
			for (const auto& param_name : current_template_param_names_) {
				if (param_name == type_name_handle) {
					// This is a template parameter being used as a type
					// Parse as variable declaration
					return parse_variable_declaration();
				}
			}
		}

		// Check if this identifier is a member type alias in the current struct context
		// e.g., "using const_iterator = const T*;" defined in a template struct body
		// When parsing member function bodies, these type aliases need to be recognized
		{
			auto check_struct_type_alias = [&](auto* struct_node) -> bool {
				if (!struct_node) return false;
				for (const auto& alias : struct_node->type_aliases()) {
					if (alias.alias_name == type_name_handle) {
						return true;
					}
				}
				return false;
			};
			bool found_as_member_type_alias = false;
			for (auto it = member_function_context_stack_.rbegin(); it != member_function_context_stack_.rend(); ++it) {
				if (check_struct_type_alias(it->struct_node)) {
					found_as_member_type_alias = true;
					break;
				}
			}
			if (!found_as_member_type_alias) {
				for (auto it = struct_parsing_context_stack_.rbegin(); it != struct_parsing_context_stack_.rend(); ++it) {
					if (check_struct_type_alias(it->struct_node)) {
						found_as_member_type_alias = true;
						break;
					}
				}
			}
			if (found_as_member_type_alias) {
				return parse_variable_declaration();
			}
		}

		// If it starts with an identifier, it could be an assignment, expression,
		// or function call statement
		return parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	}
	else if (current_token.type() == Token::Type::Operator) {
		// Handle prefix operators as expression statements
		// e.g., ++i; or --i; or *p = 42; or +[](){}; or -x;
		std::string_view op = current_token.value();
		if (op == "++" || op == "--" || op == "*" || op == "&" || op == "+" || op == "-" || op == "!" || op == "~") {
			return parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		}
		// Unknown operator - consume token to avoid infinite loop and return error
		advance();
		return ParseResult::error("Unexpected operator: " + std::string(current_token.value()),
			current_token);
	}
	else if (current_token.type() == Token::Type::Punctuator) {
		// Handle lambda expressions and other expression statements starting with punctuators
		std::string_view punct = current_token.value();
		if (punct == ";") {
			// Empty statement (null statement) - just consume the semicolon
			Token semi_token = current_token;
			advance();
			
			// Warning: Check if this empty statement comes after a loop and is followed by a block
			// This is a common mistake: for(...); { ... } where the block is not part of the loop
			if (peek() == "{"_tok) {
				// Check if we just parsed a loop by looking at recent context
				// This is heuristic: if the semicolon is on the same line or very close,
				// it's likely an accidental empty statement after a loop
				FLASH_LOG(General, Warning, "Empty statement followed by a block. "
					"Did you mean to include the block in the loop/if statement? "
					"Location: line ", semi_token.line(), ", column ", semi_token.column());
			}
			
			return ParseResult::success();
		}
		else if (punct == "[") {
			// Lambda expression
			return parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		}
		else if (punct == "(") {
			// Parenthesized expression
			return parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		}
		// Unknown punctuator - consume token to avoid infinite loop and return error
		advance();
		return ParseResult::error("Unexpected punctuator: " + std::string(current_token.value()),
			current_token);
	}
	else if (current_token.type() == Token::Type::Literal) {
		// Handle literal expression statements (e.g., "42;")
		return parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	}
	else {
		// Unknown token type - consume token to avoid infinite loop and return error
		advance();
		return ParseResult::error("Expected a statement or declaration",
			current_token);
	}
}

ParseResult Parser::parse_variable_declaration()
{
	// Phase 1 Consolidation: Use shared specifier parsing helper
	FlashCpp::DeclarationSpecifiers specs = parse_declaration_specifiers();
	
	// Extract values for backward compatibility (will be removed in later phases)
	bool is_constexpr = specs.is_constexpr;
	bool is_constinit = specs.is_constinit;
	StorageClass storage_class = specs.storage_class;
	[[maybe_unused]] Linkage linkage = specs.linkage;

	// Parse the type specifier and identifier (name)
	ParseResult type_and_name_result = parse_type_and_name();
	if (type_and_name_result.is_error()) {
		return type_and_name_result;
	}

	// Check if this is a structured binding declaration
	if (type_and_name_result.node().has_value() && type_and_name_result.node()->is<StructuredBindingNode>()) {
		// Structured bindings have their own handling
		// They already include the initializer, so we just return the node
		// Note: Structured bindings don't support storage class specifiers or constexpr/constinit
		
		// Validate: structured bindings cannot have storage class specifiers
		if (storage_class != StorageClass::None) {
			return ParseResult::error("Structured bindings cannot have storage class specifiers (static, extern, etc.)", current_token_);
		}
		
		// Validate: structured bindings cannot be constexpr
		if (is_constexpr) {
			return ParseResult::error("Structured bindings cannot be constexpr", current_token_);
		}
		
		// Validate: structured bindings cannot be constinit
		if (is_constinit) {
			return ParseResult::error("Structured bindings cannot be constinit", current_token_);
		}
		
		FLASH_LOG(Parser, Debug, "parse_variable_declaration: Handling structured binding");
		
		// For now, just return the structured binding node directly
		// The code generation will handle decomposing it
		return type_and_name_result;
	}

	// Get the type specifier for potential additional declarations
	DeclarationNode& first_decl = type_and_name_result.node()->as<DeclarationNode>();
	TypeSpecifierNode& type_specifier = first_decl.type_node().as<TypeSpecifierNode>();

	// Helper lambda to create a single variable declaration
	auto create_var_decl = [&](DeclarationNode& decl, std::optional<ASTNode> init_expr) -> ParseResult {

		// Create and return a VariableDeclarationNode with storage class
		ASTNode var_decl_node = emplace_node<VariableDeclarationNode>(
			emplace_node<DeclarationNode>(decl),
			init_expr,
			storage_class
		);

		// Set constexpr/constinit flags
		VariableDeclarationNode& var_decl = var_decl_node.as<VariableDeclarationNode>();
		var_decl.set_is_constexpr(is_constexpr);
		var_decl.set_is_constinit(is_constinit);

		// Add the VariableDeclarationNode to the symbol table
		// This preserves the is_constexpr flag and initializer for constant expression evaluation
		const Token& identifier_token = decl.identifier_token();
		if (!gSymbolTable.insert(identifier_token.value(), var_decl_node)) {
			// Duplicate variable declaration in the same scope
			FLASH_LOG(Parser, Warning, "Variable '", identifier_token.value(), 
					  "' is being redeclared in the same scope");
			return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, identifier_token);
		}

		return ParseResult::success(var_decl_node);
	};

	// Process the first declaration
	std::optional<ASTNode> first_init_expr;

	// Phase 2 Consolidation: Check if this looks like a function declaration
	// before trying to parse as direct initialization
	// e.g., `static int func() { return 0; }` in block scope should be parsed as function
	if (peek() == "("_tok) {
		if (looks_like_function_parameters()) {
			// This is a function declaration, delegate to parse_function_declaration
			FLASH_LOG(Parser, Debug, "parse_variable_declaration: Detected function declaration, delegating to parse_function_declaration");
			
			// Create AttributeInfo for calling convention
			AttributeInfo attr_info;
			attr_info.linkage = specs.linkage;
			attr_info.calling_convention = specs.calling_convention;
			
			// Try to parse as function
			ParseResult function_result = parse_function_declaration(first_decl, attr_info.calling_convention);
			if (!function_result.is_error()) {
				// Successfully parsed as function
				if (auto func_node_ptr = function_result.node()) {
					FunctionDeclarationNode& func_node = func_node_ptr->as<FunctionDeclarationNode>();
					if (attr_info.linkage == Linkage::DllImport || attr_info.linkage == Linkage::DllExport) {
						func_node.set_linkage(attr_info.linkage);
					}
					func_node.set_is_constexpr(is_constexpr);
					func_node.set_is_constinit(is_constinit);
				}
				
				// Parse trailing specifiers
				FlashCpp::MemberQualifiers member_quals;
				FlashCpp::FunctionSpecifiers func_specs;
				auto specs_result = parse_function_trailing_specifiers(member_quals, func_specs);
				if (specs_result.is_error()) {
					return specs_result;
				}
				
				// Apply noexcept specifier
				if (func_specs.is_noexcept) {
					if (auto func_node_ptr = function_result.node()) {
						FunctionDeclarationNode& func_node = func_node_ptr->as<FunctionDeclarationNode>();
						func_node.set_noexcept(true);
						if (func_specs.noexcept_expr.has_value()) {
							func_node.set_noexcept_expression(*func_specs.noexcept_expr);
						}
					}
				}
				
				// Register function in symbol table
				const Token& identifier_token = first_decl.identifier_token();
				StringHandle func_name = identifier_token.handle();
				if (auto func_node = function_result.node()) {
					if (!gSymbolTable.insert(func_name, *func_node)) {
						return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, identifier_token);
					}
				}
				
				// Check for declaration-only (;) vs function definition ({)
				if (consume(";"_tok)) {
					return function_result;
				}
				
				// Parse function body
				if (peek() == "{"_tok) {
					// Enter function scope
					FlashCpp::SymbolTableScope func_scope(ScopeType::Function);
					
					// Add function parameters to symbol table
					if (auto func_node_ptr = function_result.node()) {
						FunctionDeclarationNode& func_decl = func_node_ptr->as<FunctionDeclarationNode>();
						for (const ASTNode& param_node : func_decl.parameter_nodes()) {
							if (param_node.is<VariableDeclarationNode>()) {
								const VariableDeclarationNode& var_decl = param_node.as<VariableDeclarationNode>();
								const DeclarationNode& param_decl = var_decl.declaration();
								gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
							} else if (param_node.is<DeclarationNode>()) {
								const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
								gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
							}
						}
					}
					
					// Parse function body
					ParseResult body_result = parse_block();
					if (body_result.is_error()) {
						return body_result;
					}
					
					// Set function body
					if (auto func_node_ptr = function_result.node()) {
						FunctionDeclarationNode& func_decl = func_node_ptr->as<FunctionDeclarationNode>();
						if (body_result.node().has_value()) {
							func_decl.set_definition(*body_result.node());
							// Deduce auto return types from function body
							deduce_and_update_auto_return_type(func_decl);
						}
					}
				}
				
				return function_result;
			}
			// If function parsing fails, fall through to try direct initialization
		}
	}

	// Phase 3 Consolidation: Use shared initialization helpers
	// Check for direct initialization with parentheses: Type var(args)
	if (peek() == "("_tok) {
		auto init_result = parse_direct_initialization();
		if (init_result.has_value()) {
			first_init_expr = init_result;
			// After closing ), skip any trailing specifiers (this might be a function forward declaration)
			// e.g., void func() noexcept; or void func() const;
			FlashCpp::MemberQualifiers member_quals;
			skip_function_trailing_specifiers(member_quals);
		} else {
			return ParseResult::error("Expected ')' after direct initialization arguments", current_token_);
		}
	}
	// Check for copy initialization: Type var = expr or Type var = {args}
	else if (peek() == "="_tok) {
		auto init_result = parse_copy_initialization(first_decl, type_specifier);
		if (init_result.has_value()) {
			first_init_expr = init_result;
		} else {
			return ParseResult::error("Failed to parse initializer expression", current_token_);
		}
	}
	// Check for direct brace initialization: Type var{args}
	else if (peek() == "{"_tok) {
		// Direct list initialization: Type var{args}
		ParseResult init_list_result = parse_brace_initializer(type_specifier);
		if (init_list_result.is_error()) {
			return init_list_result;
		}
		first_init_expr = init_list_result.node();
	}

	if (first_init_expr.has_value() && first_init_expr->is<InitializerListNode>()) {
		try_apply_deduction_guides(type_specifier, first_init_expr->as<InitializerListNode>());
	}

	// Check if there are more declarations (comma-separated)
	if (peek() == ","_tok) {
		// Create a block to hold multiple declarations
		auto [block_node, block_ref] = create_node_ref(BlockNode());

		// Add the first declaration to the block
		ParseResult first_result = create_var_decl(first_decl, first_init_expr);
		if (first_result.is_error()) {
			return first_result;
		}
		block_ref.add_statement_node(*first_result.node());

		// Parse additional declarations
		while (consume(","_tok)) {
			// Parse the identifier (name) - reuse the same type
			Token identifier_tok = advance();
			if (identifier_tok.type() != Token::Type::Identifier) {
				return ParseResult::error("Expected identifier after comma in declaration list", identifier_tok);
			}

			// Create a new DeclarationNode with the same type
			DeclarationNode& new_decl = emplace_node<DeclarationNode>(
				emplace_node<TypeSpecifierNode>(type_specifier),
				identifier_tok
			).as<DeclarationNode>();

			// Check for initialization
			std::optional<ASTNode> init_expr;
			if (peek() == "="_tok) {
				advance(); // consume the '=' operator

				// Check if this is a brace initializer
				if (peek() == "{"_tok) {
					ParseResult init_list_result = parse_brace_initializer(type_specifier);
					if (init_list_result.is_error()) {
						return init_list_result;
					}
					init_expr = init_list_result.node();
				} else {
					// Parse expression with precedence > comma operator (precedence 1)
					// This prevents comma from being treated as an operator in declaration lists
					FLASH_LOG(Parser, Debug, "parse_variable_declaration: About to parse initializer expression, current token: ", std::string(peek_info().value()));
					ParseResult init_expr_result = parse_expression(2, ExpressionContext::Normal);
					if (init_expr_result.is_error()) {
						return init_expr_result;
					}
					init_expr = init_expr_result.node();
				}
			} else if (peek() == "("_tok) {
				// Constructor-style initialization for comma-separated declaration: Type var1, var2(args)
				auto init_result = parse_direct_initialization();
				if (init_result.has_value()) {
					init_expr = init_result;
				} else {
					return ParseResult::error("Failed to parse direct initialization", current_token_);
				}
			} else if (peek() == "{"_tok) {
				// Direct list initialization for comma-separated declaration: Type var1, var2{args}
				ParseResult init_list_result = parse_brace_initializer(type_specifier);
				if (init_list_result.is_error()) {
					return init_list_result;
				}
				init_expr = init_list_result.node();
			}

			// Add this declaration to the block
			ParseResult decl_result = create_var_decl(new_decl, init_expr);
			if (decl_result.is_error()) {
				return decl_result;
			}
			block_ref.add_statement_node(*decl_result.node());
		}

		// Return the block containing all declarations
		return ParseResult::success(block_node);
	}
	else {
		// Single declaration - return it directly
		return create_var_decl(first_decl, first_init_expr);
	}
}

// Phase 3 Consolidation: Parse direct initialization with parentheses: Type var(args)
// Returns the initializer node (InitializerListNode) or std::nullopt if not at '('
// Assumes we're positioned at '(' when called
std::optional<ASTNode> Parser::parse_direct_initialization()
{
	// Must be at '('
	if (peek() != "("_tok) {
		return std::nullopt;
	}
	
	advance(); // consume '('

	// Create an InitializerListNode to hold the arguments
	auto [init_list_node, init_list_ref] = create_node_ref(InitializerListNode());

	// Parse argument list
	while (true) {
		// Check if we've reached the end
		if (peek() == ")"_tok) {
			break;
		}

		ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (arg_result.is_error()) {
			// Return nullopt on error - caller should handle
			return std::nullopt;
		}

		if (auto arg_node = arg_result.node()) {
			init_list_ref.add_initializer(*arg_node);
		}

		// Check for comma (more arguments) or closing paren
		if (!consume(","_tok)) {
			// No comma, so we expect a closing paren on the next iteration
			break;
		}
	}

	if (!consume(")"_tok)) {
		// Return nullopt on error - caller should handle
		return std::nullopt;
	}

	return init_list_node;
}

// Phase 3 Consolidation: Parse copy initialization: Type var = expr or Type var = {args}
// Returns the initializer expression/list node or std::nullopt if not at '='
// Also handles auto type deduction and array size inference
std::optional<ASTNode> Parser::parse_copy_initialization(DeclarationNode& decl_node, TypeSpecifierNode& type_specifier)
{
	// Must be at '='
	if (peek() != "="_tok) {
		return std::nullopt;
	}
	
	advance(); // consume '='

	// Check if this is a brace initializer (e.g., Point p = {10, 20} or int arr[5] = {1, 2, 3, 4, 5})
	if (peek() == "{"_tok) {
		// If this is an array declaration, set the array info on type_specifier
		if (decl_node.is_array()) {
			std::optional<size_t> array_size_val;
			if (decl_node.array_size().has_value()) {
				// Try to evaluate the array size as a constant expression
				ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
				auto eval_result = ConstExpr::Evaluator::evaluate(*decl_node.array_size(), eval_ctx);
				if (eval_result.success()) {
					array_size_val = static_cast<size_t>(eval_result.as_int());
				}
			}
			// Note: for unsized arrays (int arr[] = {...}), array_size_val will remain empty
			// and will be set after parsing the initializer list
			type_specifier.set_array(true, array_size_val);
		}

		// Parse brace initializer list
		ParseResult init_list_result = parse_brace_initializer(type_specifier);
		if (init_list_result.is_error()) {
			return std::nullopt;
		}
		
		auto initializer = init_list_result.node();

		// For unsized arrays, infer the size from the initializer list
		if (decl_node.is_unsized_array() && initializer.has_value() && 
		    initializer->is<InitializerListNode>()) {
			const InitializerListNode& init_list = initializer->as<InitializerListNode>();
			size_t inferred_size = init_list.initializers().size();
			type_specifier.set_array(true, inferred_size);
		}
		
		return initializer;
	} else {
		// Regular expression initializer
		ParseResult init_expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (init_expr_result.is_error()) {
			return std::nullopt;
		}
		auto initializer = init_expr_result.node();

		// If the type is auto, deduce the type from the initializer
		if (type_specifier.type() == Type::Auto && initializer.has_value()) {
			// IMPORTANT: Save the original reference and CV qualifiers before type deduction
			// Auto type deduction replaces the entire TypeSpecifierNode, which would lose
			// qualifiers set during parsing (e.g., const auto&, auto&&)
			// We must preserve these to generate correct code for references
			ReferenceQualifier original_ref_qual = type_specifier.reference_qualifier();
			CVQualifier original_cv_qual = type_specifier.cv_qualifier();
			
			// Get the full type specifier from the initializer expression
			auto deduced_type_spec_opt = get_expression_type(*initializer);
			if (deduced_type_spec_opt.has_value()) {
				// Use the full deduced type specifier (preserves struct type index, etc.)
				type_specifier = *deduced_type_spec_opt;
				FLASH_LOG(Parser, Debug, "Deduced auto variable type from initializer: type=", 
						  (int)type_specifier.type(), " size=", (int)type_specifier.size_in_bits());
			} else {
				// Fallback: deduce basic type
				Type deduced_type = deduce_type_from_expression(*initializer);
				unsigned char deduced_size = get_type_size_bits(deduced_type);
				type_specifier = TypeSpecifierNode(deduced_type, TypeQualifier::None, deduced_size, decl_node.identifier_token(), original_cv_qual);
				FLASH_LOG(Parser, Debug, "Deduced auto variable type (fallback): type=", 
						  (int)type_specifier.type(), " size=", (int)deduced_size);
			}
			
			// Restore the original reference qualifier and CV qualifier (for const auto&, auto&, auto&& etc.)
			type_specifier.set_reference_qualifier(original_ref_qual);
			// Also ensure CV qualifier is preserved (especially for const auto&)
			if (original_cv_qual != CVQualifier::None) {
				type_specifier.set_cv_qualifier(original_cv_qual);
			}
		}
		
		return initializer;
	}
}

// Check if a type name is std::initializer_list (or just initializer_list in std namespace)
// Returns the element type index if it is, or nullopt otherwise
std::optional<TypeIndex> Parser::is_initializer_list_type(const TypeSpecifierNode& type_spec) const {
	if (type_spec.type() != Type::Struct) {
		return std::nullopt;
	}
	
	TypeIndex type_index = type_spec.type_index();
	if (type_index >= gTypeInfo.size()) {
		return std::nullopt;
	}
	
	const TypeInfo& type_info = gTypeInfo[type_index];
	
	// Phase 6: Use TypeInfo::isTemplateInstantiation() to check for initializer_list
	// Check if this is a template instantiation of std::initializer_list
	// Note: baseTemplateName() stores the unqualified name (without namespace prefix)
	// so we check for "initializer_list" and verify it's in the std namespace
	if (type_info.isTemplateInstantiation()) {
		std::string_view base_name = StringTable::getStringView(type_info.baseTemplateName());
		if (base_name == "initializer_list") {
			// Verify this is from the std namespace by checking the full type name
			std::string_view full_name = StringTable::getStringView(type_info.name_);
			if (full_name.starts_with("std::initializer_list")) {
				// This is an initializer_list type from the std namespace
				FLASH_LOG(Parser, Debug, "is_initializer_list_type: detected as initializer_list type");
				return type_index;
			}
		}
	}
	
	return std::nullopt;
}

// Find a constructor in struct_info that takes std::initializer_list<T> as its parameter
// Returns the constructor and the element type if found
std::optional<std::pair<const StructMemberFunction*, TypeIndex>> 
Parser::find_initializer_list_constructor(const StructTypeInfo& struct_info) const {
	FLASH_LOG(Parser, Debug, "find_initializer_list_constructor: checking struct '", 
	          StringTable::getStringView(struct_info.getName()), "' with ", 
	          struct_info.member_functions.size(), " member functions");
	
	for (const auto& member_func : struct_info.member_functions) {
		if (!member_func.is_constructor) continue;
		
		FLASH_LOG(Parser, Debug, "  found constructor, checking parameters...");
		
		// Check if this constructor has a function declaration
		if (!member_func.function_decl.has_value()) {
			FLASH_LOG(Parser, Debug, "    no function_decl");
			continue;
		}
		
		// Constructors can be stored as ConstructorDeclarationNode or FunctionDeclarationNode
		const std::vector<ASTNode>* params = nullptr;
		
		if (member_func.function_decl.is<ConstructorDeclarationNode>()) {
			const ConstructorDeclarationNode& ctor_decl = member_func.function_decl.as<ConstructorDeclarationNode>();
			params = &ctor_decl.parameter_nodes();
			FLASH_LOG(Parser, Debug, "    is ConstructorDeclarationNode with ", params->size(), " parameters");
		} else if (member_func.function_decl.is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
			params = &func_decl.parameter_nodes();
			FLASH_LOG(Parser, Debug, "    is FunctionDeclarationNode with ", params->size(), " parameters");
		} else {
			FLASH_LOG(Parser, Debug, "    unknown node type");
			continue;
		}
		
		// Look for constructor taking exactly one initializer_list parameter
		if (params->size() != 1) continue;
		
		const ASTNode& param_node = (*params)[0];
		if (!param_node.is<DeclarationNode>()) {
			FLASH_LOG(Parser, Debug, "    param is not DeclarationNode");
			continue;
		}
		
		const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
		if (!param_decl.type_node().is<TypeSpecifierNode>()) {
			FLASH_LOG(Parser, Debug, "    param type is not TypeSpecifierNode");
			continue;
		}
		
		const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
		FLASH_LOG(Parser, Debug, "    param type: ", (int)param_type.type(), 
		          " index: ", param_type.type_index());
		
		auto element_type_opt = is_initializer_list_type(param_type);
		
		if (element_type_opt.has_value()) {
			FLASH_LOG(Parser, Debug, "    FOUND initializer_list constructor!");
			return std::make_pair(&member_func, *element_type_opt);
		}
	}
	
	return std::nullopt;
}

ParseResult Parser::parse_brace_initializer(const TypeSpecifierNode& type_specifier)
{
	// Parse brace initializer list: { expr1, expr2, ... }
	// This is used for struct initialization like: Point p = {10, 20};
	// or for array initialization like: int arr[5] = {1, 2, 3, 4, 5};

	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' for brace initializer", current_token_);
	}

	// Create an InitializerListNode to hold the initializer expressions
	auto [init_list_node, init_list_ref] = create_node_ref(InitializerListNode());

	// Handle array brace initialization
	if (type_specifier.is_array()) {
		// Get the array size if specified
		std::optional<size_t> array_size = type_specifier.array_size();
		size_t element_count = 0;

		// Parse comma-separated initializer expressions
		while (true) {
			// Check if we've reached the end of the initializer list
			if (peek() == "}"_tok) {
				break;
			}

			// Check if we have too many initializers
			if (array_size.has_value() && element_count >= *array_size) {
				return ParseResult::error("Too many initializers for array", current_token_);
			}

			// Parse the initializer expression with precedence > comma operator (precedence 1)
			// This prevents comma from being treated as an operator in initializer lists
			ParseResult init_expr_result = parse_expression(2, ExpressionContext::Normal);
			if (init_expr_result.is_error()) {
				return init_expr_result;
			}

			// Add the initializer to the list
			if (init_expr_result.node().has_value()) {
				init_list_ref.add_initializer(*init_expr_result.node());
			} else {
				return ParseResult::error("Expected initializer expression", current_token_);
			}

			element_count++;

			// Check for comma or end of list
			if (peek() == ","_tok) {
				advance(); // consume the comma

				// Allow trailing comma before '}'
				if (peek() == "}"_tok) {
					break;
				}
			} else {
				// No comma, so we should be at the end
				break;
			}
		}

		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' to close brace initializer", current_token_);
		}

		return ParseResult::success(init_list_node);
	}

	// Handle scalar type brace initialization (C++11): int x = {10}; or int x{};
	// For scalar types, braced initializer should have exactly one element or be empty (value initialization)
	// Note: Template instantiations are stored as Type::UserDefined, so we need to check if it's a struct-like type
	bool is_struct_like_type = (type_specifier.type() == Type::Struct);
	if (!is_struct_like_type && type_specifier.type() == Type::UserDefined) {
		// Check if this UserDefined type is actually a struct (e.g., instantiated template)
		TypeIndex type_index = type_specifier.type_index();
		if (type_index < gTypeInfo.size() && gTypeInfo[type_index].struct_info_) {
			is_struct_like_type = true;
		}
	}
	// In template bodies, dependent UserDefined types (e.g., node_type, value_type) may not have
	// struct_info_ yet but could resolve to structs at instantiation time. Treat them as struct-like
	// to allow multi-element brace-init lists like: return { expr1, expr2 };
	if (!is_struct_like_type && type_specifier.type() == Type::UserDefined &&
		(parsing_template_body_ || !struct_parsing_context_stack_.empty())) {
		is_struct_like_type = true;
	}
	if (!is_struct_like_type) {
		// Check if this is an empty brace initializer: int x{};
		if (peek() == "}"_tok) {
			advance(); // consume '}'
			
			// Create a zero literal of the appropriate type
			Token zero_token(Token::Type::Literal, "0"sv, 0, 0, 0);
			
			// Use 0.0 for floating point types, 0ULL for integral types
			if (type_specifier.type() == Type::Double || type_specifier.type() == Type::Float) {
				auto zero_expr = emplace_node<ExpressionNode>(
					NumericLiteralNode(zero_token, 0.0, type_specifier.type(), TypeQualifier::None, get_type_size_bits(type_specifier.type()))
				);
				return ParseResult::success(zero_expr);
			} else {
				auto zero_expr = emplace_node<ExpressionNode>(
					NumericLiteralNode(zero_token, 0ULL, type_specifier.type(), TypeQualifier::None, get_type_size_bits(type_specifier.type()))
				);
				return ParseResult::success(zero_expr);
			}
		}
		
		// Parse the single initializer expression with precedence > comma operator (precedence 1)
		// This prevents comma from being treated as an operator in initializer lists
		ParseResult init_expr_result = parse_expression(2, ExpressionContext::Normal);
		if (init_expr_result.is_error()) {
			return init_expr_result;
		}

		if (!init_expr_result.node().has_value()) {
			return ParseResult::error("Expected initializer expression", current_token_);
		}

		// For scalar types, only allow a single initializer (no comma)
		if (peek() == ","_tok) {
			return ParseResult::error("Too many initializers for scalar type", current_token_);
		}

		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' to close brace initializer", current_token_);
		}

		// For scalar types, unwrap the expression from the brace initializer
		// Return the expression directly instead of an InitializerListNode
		return ParseResult::success(*init_expr_result.node());
	}

	TypeIndex type_index = type_specifier.type_index();
	if (type_index >= gTypeInfo.size() ||
		(type_index < gTypeInfo.size() && !gTypeInfo[type_index].struct_info_)) {
		// In template bodies, dependent types may not have struct_info_ yet.
		// Parse as a generic initializer list (comma-separated expressions).
		if (parsing_template_body_ || !struct_parsing_context_stack_.empty()) {
			while (true) {
				if (peek() == "}"_tok) break;
				ParseResult init_expr_result = parse_expression(2, ExpressionContext::Normal);
				if (init_expr_result.is_error()) return init_expr_result;
				if (init_expr_result.node().has_value()) {
					init_list_ref.add_initializer(*init_expr_result.node());
				} else {
					return ParseResult::error("Expected initializer expression", current_token_);
				}
				if (peek() == ","_tok) {
					advance();
					if (peek() == "}"_tok) break;
				} else {
					break;
				}
			}
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' to close brace initializer", current_token_);
			}
			return ParseResult::success(init_list_node);
		}
		if (type_index >= gTypeInfo.size()) {
			return ParseResult::error("Invalid struct type index", current_token_);
		}
		return ParseResult::error("Type is not a struct", current_token_);
	}

	const TypeInfo& type_info = gTypeInfo[type_index];
	const StructTypeInfo& struct_info = *type_info.struct_info_;

	// Check if this struct has an initializer_list constructor
	// If so, we need to handle brace-init-list specially by creating an InitializerListConstructionNode
	auto init_list_ctor = find_initializer_list_constructor(struct_info);
	if (init_list_ctor.has_value()) {
		// This struct has an initializer_list constructor
		// Parse all the brace elements first
		std::vector<ASTNode> elements;
		Token brace_token = current_token_;  // Save the '{' token location
		
		while (true) {
			// Check if we've reached the end of the initializer list
			if (peek() == "}"_tok) {
				break;
			}
			
			// Parse the initializer expression
			ParseResult init_expr_result = parse_expression(2, ExpressionContext::Normal);
			if (init_expr_result.is_error()) {
				return init_expr_result;
			}
			
			if (init_expr_result.node().has_value()) {
				elements.push_back(*init_expr_result.node());
			} else {
				return ParseResult::error("Expected initializer expression", current_token_);
			}
			
			// Check for comma or end of list
			if (peek() == ","_tok) {
				advance(); // consume the comma
				
				// Allow trailing comma before '}'
				if (peek() == "}"_tok) {
					break;
				}
			} else {
				// No comma, so we should be at the end
				break;
			}
		}
		
		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' to close brace initializer", current_token_);
		}
		
		// Get element type from the initializer_list type
		// The initializer_list struct stores the element type in its first member's type_index
		TypeIndex init_list_type_index = init_list_ctor->second;
		ASTNode element_type_node;
		
		// Extract element type from the initializer_list struct's first member
		// The first member is typically a pointer (const T*), and type_index points to T
		if (init_list_type_index < gTypeInfo.size()) {
			const TypeInfo& init_list_info = gTypeInfo[init_list_type_index];
			if (init_list_info.struct_info_ && !init_list_info.struct_info_->members.empty()) {
				const StructMember& first_member = init_list_info.struct_info_->members[0];
				// The first member's type_index should point to the element type
				if (first_member.type_index > 0 && first_member.type_index < gTypeInfo.size()) {
					const TypeInfo& elem_info = gTypeInfo[first_member.type_index];
					Type elem_type = elem_info.type_;
					int elem_size = elem_info.type_size_ > 0 ? elem_info.type_size_ : get_type_size_bits(elem_type);
					
					auto elem_type_spec = emplace_node<TypeSpecifierNode>(
						elem_type, 
						TypeQualifier::None,
						static_cast<unsigned char>(elem_size),
						brace_token
					);
					// If it's a struct type, preserve the type_index
					if (elem_type == Type::Struct) {
						elem_type_spec.as<TypeSpecifierNode>().set_type_index(first_member.type_index);
					}
					element_type_node = elem_type_spec;
				} else {
					// Fall back to using the member's type directly (for primitive types)
					int elem_size = get_type_size_bits(first_member.type);
					element_type_node = emplace_node<TypeSpecifierNode>(
						first_member.type,
						TypeQualifier::None,
						static_cast<unsigned char>(elem_size),
						brace_token
					);
				}
			}
		}
		
		// Fallback to int if we couldn't extract the element type
		if (!element_type_node.has_value()) {
			element_type_node = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, brace_token);
		}
		
		// Try to get the actual element type from the parameter
		const StructMemberFunction* ctor = init_list_ctor->first;
		ASTNode target_type_node;
		
		// Constructors can be stored as ConstructorDeclarationNode or FunctionDeclarationNode
		if (ctor && ctor->function_decl.is<ConstructorDeclarationNode>()) {
			const ConstructorDeclarationNode& ctor_decl = ctor->function_decl.as<ConstructorDeclarationNode>();
			if (!ctor_decl.parameter_nodes().empty()) {
				const ASTNode& param = ctor_decl.parameter_nodes()[0];
				if (param.is<DeclarationNode>()) {
					const DeclarationNode& param_decl = param.as<DeclarationNode>();
					if (param_decl.type_node().is<TypeSpecifierNode>()) {
						target_type_node = param_decl.type_node();
					}
				}
			}
		} else if (ctor && ctor->function_decl.is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& func_decl = ctor->function_decl.as<FunctionDeclarationNode>();
			if (!func_decl.parameter_nodes().empty()) {
				const ASTNode& param = func_decl.parameter_nodes()[0];
				if (param.is<DeclarationNode>()) {
					const DeclarationNode& param_decl = param.as<DeclarationNode>();
					if (param_decl.type_node().is<TypeSpecifierNode>()) {
						target_type_node = param_decl.type_node();
					}
				}
			}
		}
		
		// If we found the target type, create the InitializerListConstructionNode
		if (target_type_node.has_value()) {
			// Create InitializerListConstructionNode
			auto init_list_construction = emplace_node<ExpressionNode>(
				InitializerListConstructionNode(
					element_type_node,
					target_type_node,
					std::move(elements),
					brace_token
				)
			);
			
			// Now wrap this in a ConstructorCallNode to call the actual constructor
			ChunkedVector<ASTNode> ctor_args;
			ctor_args.push_back(init_list_construction);
			
			auto type_spec_node = emplace_node<TypeSpecifierNode>(
				Type::Struct, type_index, 
				static_cast<unsigned char>(struct_info.total_size * 8),
				brace_token
			);
			
			return ParseResult::success(
				emplace_node<ExpressionNode>(
					ConstructorCallNode(type_spec_node, std::move(ctor_args), brace_token)
				)
			);
		}
		
		// Fallback: if we couldn't get the target type, return an error
		return ParseResult::error("Could not determine initializer_list element type", brace_token);
	}

	// Check if this struct has constructors and no data members (or not enough for aggregate init)
	// In this case, we should use constructor initialization rather than aggregate initialization
	// This handles patterns like: inline constexpr nullopt_t nullopt { nullopt_t::_Construct::_Token };
	if (struct_info.members.empty()) {
		// No data members - must use constructor initialization
		// Parse all the brace elements first
		std::vector<ASTNode> elements;
		Token brace_token = current_token_;  // Save location for error reporting
		
		while (true) {
			// Check if we've reached the end of the initializer list
			if (peek() == "}"_tok) {
				break;
			}
			
			// Parse the initializer expression
			ParseResult init_expr_result = parse_expression(2, ExpressionContext::Normal);
			if (init_expr_result.is_error()) {
				return init_expr_result;
			}
			
			if (init_expr_result.node().has_value()) {
				elements.push_back(*init_expr_result.node());
			} else {
				return ParseResult::error("Expected initializer expression", current_token_);
			}
			
			// Check for comma or end of list
			if (peek() == ","_tok) {
				advance(); // consume the comma
				
				// Allow trailing comma before '}'
				if (peek() == "}"_tok) {
					break;
				}
			} else {
				// No comma, so we should be at the end
				break;
			}
		}
		
		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' to close brace initializer", current_token_);
		}
		
		// Check if there's a constructor that matches the argument count and types
		bool found_matching_ctor = false;
		for (const auto& member_func : struct_info.member_functions) {
			if (!member_func.is_constructor) continue;
			if (!member_func.function_decl.has_value()) continue;
			
			// Get parameters from constructor
			const std::vector<ASTNode>* params = nullptr;
			if (member_func.function_decl.is<ConstructorDeclarationNode>()) {
				params = &member_func.function_decl.as<ConstructorDeclarationNode>().parameter_nodes();
			} else if (member_func.function_decl.is<FunctionDeclarationNode>()) {
				params = &member_func.function_decl.as<FunctionDeclarationNode>().parameter_nodes();
			}
			
			if (!params || params->size() != elements.size()) {
				continue;
			}
			
			// Match parameter types with argument types
			bool types_match = true;
			for (size_t i = 0; i < params->size() && types_match; ++i) {
				const ASTNode& param_node = (*params)[i];
				const ASTNode& arg_node = elements[i];
				
				// Get parameter type
				const TypeSpecifierNode* param_type = nullptr;
				if (param_node.is<VariableDeclarationNode>()) {
					const VariableDeclarationNode& var = param_node.as<VariableDeclarationNode>();
					if (var.declaration().type_node().is<TypeSpecifierNode>()) {
						param_type = &var.declaration().type_node().as<TypeSpecifierNode>();
					}
				} else if (param_node.is<DeclarationNode>()) {
					const DeclarationNode& decl = param_node.as<DeclarationNode>();
					if (decl.type_node().is<TypeSpecifierNode>()) {
						param_type = &decl.type_node().as<TypeSpecifierNode>();
					}
				}
				
				if (!param_type) {
					// Can't determine parameter type - skip type checking for this param
					continue;
				}
				
				// Get argument type
				auto arg_type_opt = get_expression_type(arg_node);
				if (!arg_type_opt.has_value()) {
					// Can't determine argument type - skip type checking for this param
					// This handles dependent expressions and complex cases
					continue;
				}
				
				const TypeSpecifierNode& arg_type = *arg_type_opt;
				
				// Compare types (allowing for implicit conversions in some cases)
				// For enum class types, we need exact match (no implicit conversions)
				if (param_type->type() == Type::Enum && arg_type.type() == Type::Enum) {
					// Enum types must match exactly by type_index
					if (param_type->type_index() != arg_type.type_index()) {
						types_match = false;
					}
				} else if (param_type->type() != arg_type.type()) {
					// Different base types - check for compatible integer/enum types
					// Allow enum -> int conversions for scoped enums passed as their underlying type
					bool compatible = false;
					if (arg_type.type() == Type::Enum && 
					    (param_type->type() == Type::Int || param_type->type() == Type::UnsignedInt ||
					     param_type->type() == Type::Long || param_type->type() == Type::UnsignedLong)) {
						// Enum to integer conversion (for scoped enum underlying type)
						compatible = true;
					}
					if (!compatible) {
						types_match = false;
					}
				} else if (param_type->type() == Type::UserDefined || param_type->type() == Type::Struct) {
					// For user-defined/struct types, check type_index
					if (param_type->type_index() != arg_type.type_index()) {
						types_match = false;
					}
				}
				
				// Check pointer depth - must match exactly
				if (types_match && param_type->pointer_depth() != arg_type.pointer_depth()) {
					types_match = false;
				}
				
				// Check reference qualifiers - must match exactly
				if (types_match && param_type->is_reference() != arg_type.is_reference()) {
					types_match = false;
				}
			}
			
			if (types_match) {
				found_matching_ctor = true;
				break;
			}
		}
		
		if (found_matching_ctor) {
			// Create a ConstructorCallNode
			auto type_spec_node = emplace_node<TypeSpecifierNode>(
				Type::Struct, type_index, 
				static_cast<unsigned char>(struct_info.total_size * 8),
				brace_token
			);
			
			ChunkedVector<ASTNode> ctor_args;
			for (auto& elem : elements) {
				ctor_args.push_back(std::move(elem));
			}
			
			return ParseResult::success(
				emplace_node<ExpressionNode>(
					ConstructorCallNode(type_spec_node, std::move(ctor_args), brace_token)
				)
			);
		}
		
		// No matching constructor and no members - this is an error
		return ParseResult::error("No matching constructor for brace initialization", brace_token);
	}

	// Parse comma-separated initializer expressions (positional or designated)
	size_t member_index = 0;
	bool has_designated = false;  // Track if we've seen any designated initializers
	std::unordered_set<std::string_view> used_members;  // Track which members have been initialized

	while (true) {
		// Check if we've reached the end of the initializer list
		if (peek() == "}"_tok) {
			break;
		}

		// Check for designated initializer syntax: .member = value
		if (peek() == "."_tok) {
			has_designated = true;
			advance();  // consume '.'

			// Parse member name
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected member name after '.' in designated initializer", current_token_);
			}
			std::string_view member_name = peek_info().value();
			advance();

			FLASH_LOG(Parser, Debug, "Parsing designated initializer for member: ", member_name);

			// Validate member name exists in struct
			bool member_found = false;
			const StructMember* target_member = nullptr;
			for (const auto& member : struct_info.members) {
				if (member.getName() == StringTable::getOrInternStringHandle(member_name)) {
					member_found = true;
					target_member = &member;
					break;
				}
			}
			if (!member_found) {
				return ParseResult::error("Unknown member '" + std::string(member_name) + "' in designated initializer", current_token_);
			}

			// Check for duplicate member initialization
			if (used_members.count(member_name)) {
				return ParseResult::error("Member '" + std::string(member_name) + "' already initialized", current_token_);
			}
			used_members.insert(member_name);

			// Expect '='
			if (peek() != "="_tok) {
				return ParseResult::error("Expected '=' after member name in designated initializer", current_token_);
			}
			advance();  // consume '='

			// Check if the next token is a brace - if so, we need to parse it as a nested brace initializer
			ParseResult init_expr_result;
			if (peek() == "{"_tok) {
				FLASH_LOG(Parser, Debug, "Detected nested brace initializer for member: ", member_name);
				// Create a type specifier for the member's type to properly parse the nested brace initializer
				if (target_member && target_member->type_index > 0 && target_member->type_index < gTypeInfo.size()) {
					const TypeInfo& member_type_info = gTypeInfo[target_member->type_index];
					auto [member_type_node, member_type_ref] = emplace_node_ref<TypeSpecifierNode>(
						member_type_info.type_, target_member->type_index, member_type_info.type_size_ * 8, Token()
					);
					FLASH_LOG(Parser, Debug, "Parsing nested brace initializer with type index: ", target_member->type_index);
					init_expr_result = parse_brace_initializer(member_type_ref);
				} else {
					FLASH_LOG(Parser, Warning, "Could not determine member type for nested brace initializer, falling back to expression parsing");
					init_expr_result = parse_expression(2, ExpressionContext::Normal);
				}
			} else {
				// Parse the initializer expression with precedence > comma operator (precedence 1)
				// This prevents comma from being treated as an operator in initializer lists
				FLASH_LOG(Parser, Debug, "Parsing simple expression initializer for member: ", member_name);
				init_expr_result = parse_expression(2, ExpressionContext::Normal);
			}
			if (init_expr_result.is_error()) {
				return init_expr_result;
			}

			// Add the designated initializer to the list
			if (init_expr_result.node().has_value()) {
				init_list_ref.add_designated_initializer(StringTable::getOrInternStringHandle(member_name), *init_expr_result.node());
			} else {
				return ParseResult::error("Expected initializer expression", current_token_);
			}
		} else {
			// Positional initializer
			if (has_designated) {
				return ParseResult::error("Positional initializers cannot follow designated initializers", current_token_);
			}

			// Check if we have too many initializers
			if (member_index >= struct_info.members.size()) {
				return ParseResult::error("Too many initializers for struct", current_token_);
			}

			FLASH_LOG(Parser, Debug, "Parsing positional initializer for member index: ", member_index);

			// Check if the next token is a brace - if so, we need to parse it as a nested brace initializer
			ParseResult init_expr_result;
			if (peek() == "{"_tok) {
				FLASH_LOG(Parser, Debug, "Detected nested brace initializer for positional member at index: ", member_index);
				// Create a type specifier for the member's type to properly parse the nested brace initializer
				const StructMember& target_member = struct_info.members[member_index];
				if (target_member.type_index > 0 && target_member.type_index < gTypeInfo.size()) {
					const TypeInfo& member_type_info = gTypeInfo[target_member.type_index];
					auto [member_type_node, member_type_ref] = emplace_node_ref<TypeSpecifierNode>(
						member_type_info.type_, target_member.type_index, member_type_info.type_size_ * 8, Token()
					);
					FLASH_LOG(Parser, Debug, "Parsing nested brace initializer with type index: ", target_member.type_index);
					init_expr_result = parse_brace_initializer(member_type_ref);
				} else {
					FLASH_LOG(Parser, Warning, "Could not determine member type for nested brace initializer, falling back to expression parsing");
					init_expr_result = parse_expression(2, ExpressionContext::Normal);
				}
			} else {
				// Parse the initializer expression with precedence > comma operator (precedence 1)
				// This prevents comma from being treated as an operator in initializer lists
				FLASH_LOG(Parser, Debug, "Parsing simple expression initializer for positional member at index: ", member_index);
				init_expr_result = parse_expression(2, ExpressionContext::Normal);
			}
			if (init_expr_result.is_error()) {
				return init_expr_result;
			}

			// Add the initializer to the list
			if (init_expr_result.node().has_value()) {
				init_list_ref.add_initializer(*init_expr_result.node());
			} else {
				return ParseResult::error("Expected initializer expression", current_token_);
			}

			member_index++;
		}

		// Check for comma or end of list
		if (peek() == ","_tok) {
			advance(); // consume the comma

			// Allow trailing comma before '}'
			if (peek() == "}"_tok) {
				break;
			}
		} else {
			// No comma, so we should be at the end
			break;
		}
	}

	if (!consume("}"_tok)) {
		return ParseResult::error("Expected '}' to close brace initializer", current_token_);
	}

	// Check if we have too few initializers
	if (member_index < struct_info.members.size()) {
		// This is allowed in C++ - remaining members are zero-initialized
		// For now, we'll just accept it
	}

	return ParseResult::success(init_list_node);
}

bool Parser::try_apply_deduction_guides(TypeSpecifierNode& type_specifier, const InitializerListNode& init_list)
{
	if (init_list.has_any_designated()) {
		return false;
	}

	// CTAD only applies to unresolved template class names, not already-instantiated structs.
	// When explicit template args are provided (e.g., Processor<char>), the type has a valid
	// type_index_ from template instantiation, so CTAD should not override it.
	if (type_specifier.type() != Type::UserDefined && type_specifier.type() != Type::Struct) {
		return false;
	}

	std::string_view class_name = type_specifier.token().value();
	if (class_name.empty()) {
		return false;
	}

	// If the type already has a non-zero size, it was explicitly instantiated with template
	// arguments (e.g., Processor<char>). CTAD should only apply when no template args were
	// specified (e.g., Box b(42)), in which case the template has size 0 pre-deduction.
	if (type_specifier.size_in_bits() > 0) {
		return false;
	}

	auto template_opt = gTemplateRegistry.lookupTemplate(class_name);
	if (!template_opt.has_value()) {
		return false;
	}

	// Get argument types from the initializer list
	std::vector<TypeSpecifierNode> argument_types;
	argument_types.reserve(init_list.initializers().size());
	for (const auto& arg_expr : init_list.initializers()) {
		auto arg_type_opt = get_expression_type(arg_expr);
		if (!arg_type_opt.has_value()) {
			return false;
		}
		argument_types.push_back(*arg_type_opt);
	}

	// Try explicit deduction guides first
	auto guide_nodes = gTemplateRegistry.lookup_deduction_guides(class_name);
	if (!guide_nodes.empty()) {
		std::vector<TemplateTypeArg> deduced_args;
		for (const auto& guide_node : guide_nodes) {
			if (!guide_node.is<DeductionGuideNode>()) {
				continue;
			}
			const auto& guide = guide_node.as<DeductionGuideNode>();
			if (deduce_template_arguments_from_guide(guide, argument_types, deduced_args)) {
				if (instantiate_deduced_template(class_name, deduced_args, type_specifier)) {
					return true;
				}
			}
		}
	}

	// Implicit CTAD: deduce template arguments from constructor parameters
	if (!template_opt->is<TemplateClassDeclarationNode>()) {
		return false;
	}
	const auto& template_class = template_opt->as<TemplateClassDeclarationNode>();
	const auto& template_params = template_class.template_parameters();
	const auto& struct_decl = template_class.class_decl_node();

	// Build template parameter name set for matching
	std::unordered_map<std::string_view, size_t> tparam_name_to_index;
	for (size_t i = 0; i < template_params.size(); ++i) {
		if (template_params[i].is<TemplateParameterNode>()) {
			const auto& tparam = template_params[i].as<TemplateParameterNode>();
			if (tparam.kind() == TemplateParameterKind::Type) {
				tparam_name_to_index[tparam.name()] = i;
			}
		}
	}

	// Try each constructor to deduce template arguments
	for (const auto& member_func : struct_decl.member_functions()) {
		if (!member_func.is_constructor) continue;

		// Get parameter nodes from either ConstructorDeclarationNode or FunctionDeclarationNode
		const std::vector<ASTNode>* params_ptr = nullptr;
		if (member_func.function_declaration.is<ConstructorDeclarationNode>()) {
			params_ptr = &member_func.function_declaration.as<ConstructorDeclarationNode>().parameter_nodes();
		} else if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
			params_ptr = &member_func.function_declaration.as<FunctionDeclarationNode>().parameter_nodes();
		} else {
			continue;
		}
		const auto& params = *params_ptr;

		if (params.size() != argument_types.size()) continue;

		// Try to deduce template arguments from constructor parameter types
		std::vector<TemplateTypeArg> deduced_args(template_params.size());
		std::vector<bool> deduced(template_params.size(), false);
		bool match = true;

		for (size_t i = 0; i < params.size() && match; ++i) {
			if (!params[i].is<DeclarationNode>()) { match = false; break; }
			const auto& param_decl = params[i].as<DeclarationNode>();
			ASTNode param_type_node = param_decl.type_node();
			if (!param_type_node.is<TypeSpecifierNode>()) { match = false; break; }
			const auto& param_type = param_type_node.as<TypeSpecifierNode>();
			std::string_view param_type_name = param_type.token().value();

			auto tparam_it = tparam_name_to_index.find(param_type_name);
			if (tparam_it != tparam_name_to_index.end()) {
				size_t idx = tparam_it->second;
				deduced_args[idx] = TemplateTypeArg(argument_types[i]);
				deduced[idx] = true;
			}
		}

		if (!match) continue;

		// Check all template type params were deduced
		bool all_deduced = true;
		for (size_t i = 0; i < template_params.size(); ++i) {
			if (tparam_name_to_index.count(template_params[i].is<TemplateParameterNode>()
				? template_params[i].as<TemplateParameterNode>().name() : "") > 0 && !deduced[i]) {
				all_deduced = false;
				break;
			}
		}
		if (!all_deduced) continue;

		if (instantiate_deduced_template(class_name, deduced_args, type_specifier)) {
			return true;
		}
	}

	return false;
}

bool Parser::deduce_template_arguments_from_guide(const DeductionGuideNode& guide,
	const std::vector<TypeSpecifierNode>& argument_types,
	std::vector<TemplateTypeArg>& out_template_args) const
{
	if (guide.guide_parameters().size() != argument_types.size()) {
		return false;
	}

	std::unordered_map<std::string_view, const TemplateParameterNode*> template_params;
	for (const auto& param_node : guide.template_parameters()) {
		if (!param_node.is<TemplateParameterNode>()) {
			continue;
		}
		const auto& tparam = param_node.as<TemplateParameterNode>();
		if (tparam.kind() == TemplateParameterKind::Type) {
			template_params.emplace(tparam.name(), &tparam);
		}
	}

	std::unordered_map<std::string_view, TypeSpecifierNode> bindings;
	for (size_t i = 0; i < guide.guide_parameters().size(); ++i) {
		if (!guide.guide_parameters()[i].is<TypeSpecifierNode>()) {
			return false;
		}
		const auto& param_type = guide.guide_parameters()[i].as<TypeSpecifierNode>();
		const auto& arg_type = argument_types[i];
		if (!match_template_parameter_type(param_type, arg_type, template_params, bindings)) {
			return false;
		}
	}

	out_template_args.clear();
	out_template_args.reserve(guide.deduced_template_args_nodes().size());
	for (const auto& rhs_node : guide.deduced_template_args_nodes()) {
		if (!rhs_node.is<TypeSpecifierNode>()) {
			return false;
		}
		const auto& rhs_type = rhs_node.as<TypeSpecifierNode>();
		auto placeholder = extract_template_param_name(rhs_type, template_params);
		if (placeholder.has_value()) {
			auto binding_it = bindings.find(*placeholder);
			if (binding_it == bindings.end()) {
				return false;
			}
			out_template_args.emplace_back(binding_it->second);
			continue;
		}

		out_template_args.emplace_back(rhs_type);
	}

	return !out_template_args.empty();
}

bool Parser::match_template_parameter_type(TypeSpecifierNode param_type,
	TypeSpecifierNode argument_type,
	const std::unordered_map<std::string_view, const TemplateParameterNode*>& template_params,
	std::unordered_map<std::string_view, TypeSpecifierNode>& bindings) const
{
	auto bind_placeholder = [&](std::string_view name, const TypeSpecifierNode& deduced_type) {
		auto [it, inserted] = bindings.emplace(name, deduced_type);
		if (!inserted && !types_equivalent(it->second, deduced_type)) {
			return false;
		}
		return true;
	};

	if (param_type.is_reference()) {
		bool requires_rvalue = param_type.is_rvalue_reference();
		if (requires_rvalue && argument_type.is_reference() && !argument_type.is_rvalue_reference()) {
			return false;
		}
		param_type.set_lvalue_reference(false);
		if (argument_type.is_reference()) {
			argument_type.set_lvalue_reference(false);
		}
	}

	while (param_type.pointer_depth() > 0) {
		if (argument_type.pointer_depth() == 0) {
			return false;
		}
		const auto& param_level = param_type.pointer_levels().back();
		const auto& arg_level = argument_type.pointer_levels().back();
		if (param_level.cv_qualifier != arg_level.cv_qualifier) {
			return false;
		}
		param_type.remove_pointer_level();
		argument_type.remove_pointer_level();
	}

	auto placeholder = extract_template_param_name(param_type, template_params);
	if (placeholder.has_value()) {
		return bind_placeholder(*placeholder, argument_type);
	}

	return types_equivalent(param_type, argument_type);
}

std::optional<std::string_view> Parser::extract_template_param_name(const TypeSpecifierNode& type_spec,
	const std::unordered_map<std::string_view, const TemplateParameterNode*>& template_params) const
{
	if (!template_params.empty()) {
		std::string_view token_name = type_spec.token().value();
		if (!token_name.empty()) {
			auto it = template_params.find(token_name);
			if (it != template_params.end()) {
				return it->first;
			}
		}
	}

	if (type_spec.type_index() < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
		std::string_view type_name = StringTable::getStringView(type_info.name());
		auto it = template_params.find(type_name);
		if (it != template_params.end()) {
			return it->first;
		}
	}

	return std::nullopt;
}

bool Parser::types_equivalent(const TypeSpecifierNode& lhs, const TypeSpecifierNode& rhs) const
{
	if (lhs.type() != rhs.type()) return false;
	if (lhs.type_index() != rhs.type_index()) return false;
	if (lhs.cv_qualifier() != rhs.cv_qualifier()) return false;
	if (lhs.pointer_depth() != rhs.pointer_depth()) return false;
	if (lhs.is_reference() != rhs.is_reference()) return false;
	if (lhs.is_rvalue_reference() != rhs.is_rvalue_reference()) return false;

	const auto& lhs_levels = lhs.pointer_levels();
	const auto& rhs_levels = rhs.pointer_levels();
	for (size_t i = 0; i < lhs_levels.size(); ++i) {
		if (lhs_levels[i].cv_qualifier != rhs_levels[i].cv_qualifier) {
			return false;
		}
	}

	return true;
}

bool Parser::instantiate_deduced_template(std::string_view class_name,
	const std::vector<TemplateTypeArg>& template_args,
	TypeSpecifierNode& type_specifier)
{
	if (template_args.empty()) {
		return false;
	}

	auto instantiated_class = try_instantiate_class_template(class_name, template_args);
	if (instantiated_class.has_value() && instantiated_class->is<StructDeclarationNode>()) {
		ast_nodes_.push_back(*instantiated_class);
	}

	std::string_view instantiated_name = get_instantiated_class_name(class_name, template_args);
	auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_name));
	if (type_it == gTypesByName.end() || !type_it->second->isStruct()) {
		return false;
	}

	const TypeInfo* struct_type_info = type_it->second;
	int size_bits = 0;
	if (const StructTypeInfo* struct_info = struct_type_info->getStructInfo()) {
		size_bits = static_cast<int>(struct_info->total_size * 8);
	}

	TypeSpecifierNode resolved(Type::Struct, struct_type_info->type_index_, size_bits, type_specifier.token(), type_specifier.cv_qualifier());
	resolved.copy_indirection_from(type_specifier);
	type_specifier = resolved;
	return true;
}

