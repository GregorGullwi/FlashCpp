ParseResult Parser::parse_declaration_or_function_definition()
{
	ScopedTokenPosition saved_position(*this);
	
	FLASH_LOG(Parser, Debug, "parse_declaration_or_function_definition: Starting, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	
	// Phase 1 Consolidation: Use shared specifier parsing helper
	FlashCpp::DeclarationSpecifiers specs = parse_declaration_specifiers();
	
	// Extract values for backward compatibility (will be removed in later phases)
	bool is_constexpr = specs.is_constexpr();
	bool is_constinit = specs.is_constinit();
	bool is_consteval = specs.is_consteval();
	[[maybe_unused]] bool is_inline = specs.is_inline;
	
	// Create AttributeInfo for backward compatibility with existing code paths
	AttributeInfo attr_info;
	attr_info.linkage = specs.linkage;
	attr_info.calling_convention = specs.calling_convention;

	// Check for inline/constexpr struct/class definition pattern:
	// inline constexpr struct Name { ... } variable = {};
	// This is a struct definition combined with a variable declaration
	if (peek().is_keyword() &&
	    (peek() == "struct"_tok || peek() == "class"_tok)) {
		// Delegate to struct parsing which will handle the full definition
		// and any trailing variable declarations
		// TODO: Pass specs (is_constexpr, is_inline, etc.) to parse_struct_declaration()
		// so they can be applied to trailing variable declarations after the struct body.
		// Currently, these specifiers are parsed but not propagated.
		auto result = parse_struct_declaration();
		if (!result.is_error()) {
			// Successfully parsed struct, propagate the result
			return saved_position.propagate(std::move(result));
		}
		// If struct parsing fails, fall through to normal parsing
	}

	// Check for out-of-line constructor/destructor pattern: ClassName::ClassName(...) or ClassName::~ClassName()
	// These have no return type, so we need to detect them before parse_type_and_name()
	if (peek().is_identifier()) {
		std::string_view first_id = peek_info().value();
		
		// Build fully qualified name using current namespace and buildQualifiedNameFromHandle
		NamespaceHandle current_namespace_handle = gSymbolTable.get_current_namespace_handle();
		std::string_view qualified_class_name = current_namespace_handle.isGlobal() 
			? first_id 
			: buildQualifiedNameFromHandle(current_namespace_handle, first_id);
		
		// Try to find the class, first with qualified name, then unqualified
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_class_name));
		if (type_it == gTypesByName.end()) {
			// Try unqualified name
			type_it = gTypesByName.find(StringTable::getOrInternStringHandle(first_id));
		}
		
		if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
			// Save position to look ahead
			SaveHandle lookahead_pos = save_token_position();
			advance();  // consume first identifier
			
			// Check for :: followed by same identifier (constructor) or ~identifier (destructor)
			if (peek() == "::"_tok) {
				advance();  // consume ::
				
				bool is_destructor = false;
				if (peek() == "~"_tok) {
					is_destructor = true;
					advance();  // consume ~
				}
				
				// Check if next identifier matches the class name (constructor/destructor pattern)
				if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
				    peek_info().value() == first_id) {
					// This is an out-of-line constructor or destructor definition!
					// Restore and parse it specially
					restore_token_position(lookahead_pos);
					// Pass the qualified name so the function can find the struct
					// Use propagate() to avoid restoring position when ScopedTokenPosition destructor runs
					return saved_position.propagate(parse_out_of_line_constructor_or_destructor(qualified_class_name, is_destructor, specs));
				}
			}
			// Not a constructor/destructor pattern, restore and continue normal parsing
			restore_token_position(lookahead_pos);
		}
	}

	// Parse the type specifier and identifier (name)
	// This will also extract any calling convention that appears after the type
	FLASH_LOG(Parser, Debug, "parse_declaration_or_function_definition: About to parse type_and_name, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	ParseResult type_and_name_result = parse_type_and_name();
	if (type_and_name_result.is_error()) {
		FLASH_LOG(Parser, Debug, "parse_declaration_or_function_definition: parse_type_and_name failed: ", type_and_name_result.error_message());
		return type_and_name_result;
	}

	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: parse_type_and_name succeeded. current_token={}, peek={}", 
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");

	// Handle structured bindings (e.g., auto [a, b] = expr;)
	// parse_type_and_name may return a StructuredBindingNode instead of a DeclarationNode
	if (type_and_name_result.node().has_value() && type_and_name_result.node()->is<StructuredBindingNode>()) {
		// Validate: structured bindings cannot have storage class specifiers
		if (specs.storage_class != StorageClass::None) {
			return ParseResult::error("Structured bindings cannot have storage class specifiers (static, extern, etc.)", current_token_);
		}
		if (is_constexpr) {
			return ParseResult::error("Structured bindings cannot be constexpr", current_token_);
		}
		if (is_constinit) {
			return ParseResult::error("Structured bindings cannot be constinit", current_token_);
		}
		return saved_position.success(type_and_name_result.node().value());
	}

	// Check for out-of-line member function definition: ClassName::functionName(...)
	// Pattern: ReturnType ClassName::functionName(...) { ... }
	// Also handles template specializations: ReturnType ClassName<Args>::functionName(...) { ... }
	DeclarationNode& decl_node = as<DeclarationNode>(type_and_name_result);
	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: Got decl_node, identifier={}. About to check for '::', current_token={}, peek={}", 
		std::string(decl_node.identifier_token().value()),
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");

	// Handle out-of-line member function for template specialization: ClassName<Args>::func(...)
	// After parse_type_and_name parsed "bool ctype", next token is "<" for "ctype<char>::is(...)"
	if (peek() == "<"_tok) {
		std::string_view base_name = decl_node.identifier_token().value();
		// Try to parse <Args>:: pattern for template specialization out-of-line definitions
		// Save position and try: <Args>::func(...)
		SaveHandle spec_pos = save_token_position();
		auto template_args_opt = parse_explicit_template_arguments();
		if (template_args_opt.has_value() && peek() == "::"_tok) {
			// Build the instantiated class name: ClassName<Args> and verify it exists
			auto inst_name_sv = get_instantiated_class_name(base_name, *template_args_opt);
			auto inst_name = StringTable::getOrInternStringHandle(inst_name_sv);
			FLASH_LOG(Parser, Debug, "Out-of-line template spec: base=", base_name, " instantiated=", inst_name_sv);
			auto inst_type_it = gTypesByName.find(inst_name);
			if (inst_type_it == gTypesByName.end()) {
				// Try namespace-qualified instantiated name
				NamespaceHandle current_namespace_handle = gSymbolTable.get_current_namespace_handle();
				if (!current_namespace_handle.isGlobal()) {
					auto qual_inst_name = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace_handle, inst_name);
					FLASH_LOG(Parser, Debug, "Out-of-line template spec: trying qualified name=", qual_inst_name.view());
					inst_type_it = gTypesByName.find(qual_inst_name);
					if (inst_type_it != gTypesByName.end()) {
						inst_name = qual_inst_name;
					}
				}
			}
			if (inst_type_it != gTypesByName.end() && inst_type_it->second->isStruct()) {
				FLASH_LOG(Parser, Debug, "Out-of-line template spec: found type for ", inst_name.view());
				// Update decl_node's identifier to be the instantiated name
				Token inst_token(Token::Type::Identifier, StringTable::getStringView(inst_name),
					decl_node.identifier_token().line(), decl_node.identifier_token().column(),
					decl_node.identifier_token().file_index());
				decl_node.set_identifier_token(inst_token);
				discard_saved_token(spec_pos);
				// Fall through to the normal :: handling below
			} else {
				FLASH_LOG(Parser, Debug, "Out-of-line template spec: type NOT found for ", inst_name.view());
				// Instantiated type not found, restore
				restore_token_position(spec_pos);
			}
		} else {
			// Not a template specialization followed by ::, restore
			restore_token_position(spec_pos);
		}
	}

	if (peek() == "::"_tok) {
		// This is an out-of-line member function definition
		advance();  // consume '::'
		
		// The class name is in decl_node.identifier_token()
		StringHandle class_name = decl_node.identifier_token().handle();
		
		// Parse the actual function name - this can be an identifier or 'operator' keyword
		Token function_name_token;
		[[maybe_unused]] bool is_operator = false;
		
		if (peek() == "operator"_tok) {
			// Out-of-line operator definition: ClassName::operator=(...) etc.
			is_operator = true;
			function_name_token = peek_info();
			advance();  // consume 'operator'
			
			// Consume the operator symbol (=, ==, !=, <<, >>, etc.)
			if (peek().is_eof()) {
				FLASH_LOG(Parser, Error, "Expected operator symbol after 'operator'");
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			
			// Build the full operator name using StringBuilder
			StringBuilder operator_builder;
			operator_builder.append("operator");
			std::string_view op = peek_info().value();
			operator_builder.append(op);
			advance();
			
			// Handle multi-character operators like >>=, <<=, etc.
			while (!peek().is_eof()) {
				std::string_view next = peek_info().value();
				if (next == "=" || next == ">" || next == "<") {
					// Could be part of >>=, <<=, etc.
					if (op == ">" && (next == ">" || next == "=")) {
						operator_builder.append(next);
						advance();
						op = next;
					} else if (op == "<" && (next == "<" || next == "=")) {
						operator_builder.append(next);
						advance();
						op = next;
					} else if ((op == ">" || op == "<" || op == "!" || op == "=") && next == "=") {
						operator_builder.append(next);
						advance();
						break;
					} else {
						break;
					}
				} else {
					break;
				}
			}
			
			// Create a token with the full operator name
			std::string_view operator_symbol = operator_builder.commit();
			function_name_token = Token(Token::Type::Identifier, operator_symbol, 
				function_name_token.line(), function_name_token.column(), function_name_token.file_index());
		} else if (peek().is_identifier()) {
			function_name_token = peek_info();
			advance();
		} else {
			FLASH_LOG(Parser, Error, "Expected function name or 'operator' after '::'");
			return ParseResult::error(ParserError::UnexpectedToken, peek_info());
		}
		
		// Find the struct in the type registry
		auto struct_iter = gTypesByName.find(class_name);
		if (struct_iter == gTypesByName.end()) {
			FLASH_LOG(Parser, Error, "Unknown class '", class_name.view(), "' in out-of-line member function definition");
			return ParseResult::error(ParserError::UnexpectedToken, decl_node.identifier_token());
		}
		
		TypeInfo* type_info = struct_iter->second;
		StructTypeInfo* struct_info = type_info->getStructInfo();
		if (!struct_info) {
			// Type alias resolution: follow type_index_ to find the actual struct type
			// e.g., using Alias = SomeStruct; then Alias::member() needs to resolve to SomeStruct
			if (type_info->type_index_ < gTypeInfo.size() && &gTypeInfo[type_info->type_index_] != type_info) {
				TypeInfo& resolved_type = gTypeInfo[type_info->type_index_];
				struct_info = resolved_type.getStructInfo();
			}
		}
		if (!struct_info) {
			FLASH_LOG(Parser, Error, "'", class_name.view(), "' is not a struct/class type");
			return ParseResult::error(ParserError::UnexpectedToken, decl_node.identifier_token());
		}
		
		// Check if this is an out-of-line static member variable definition with parenthesized initializer
		// Pattern: inline constexpr Type ClassName::member_name(initializer);
		// This must be checked BEFORE assuming it's a function definition
		StringHandle member_name_handle = function_name_token.handle();
		StructStaticMember* static_member = struct_info->findStaticMember(member_name_handle);
		if (static_member != nullptr && peek() == "("_tok) {
			// This is a static member variable definition with parenthesized initializer
			FLASH_LOG(Parser, Debug, "Found out-of-line static member variable definition: ", 
			          class_name.view(), "::", function_name_token.value());
			
			advance();  // consume '('
			
			// Parse the initializer expression
			auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (init_result.is_error() || !init_result.node().has_value()) {
				FLASH_LOG(Parser, Error, "Failed to parse initializer for static member variable '",
				          class_name.view(), "::", function_name_token.value(), "'");
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			
			// Expect closing parenthesis
			if (!consume(")"_tok)) {
				FLASH_LOG(Parser, Error, "Expected ')' after static member variable initializer");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			// Expect semicolon
			if (!consume(";"_tok)) {
				FLASH_LOG(Parser, Error, "Expected ';' after static member variable definition");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			return finalize_static_member_init(static_member, *init_result.node(), decl_node, function_name_token, saved_position);
		}
		
		// Check if this is an out-of-line static member variable definition with brace initializer
		// Pattern: inline Type ClassName::member_name{};  or  ClassName::member_name{value};
		if (static_member != nullptr && peek() == "{"_tok) {
			FLASH_LOG(Parser, Debug, "Found out-of-line static member variable definition with brace init: ", 
			          class_name.view(), "::", function_name_token.value());
			
			advance();  // consume '{'
			
			// Parse the initializer expression if present (empty braces = default init)
			std::optional<ASTNode> init_expr;
			if (peek() != "}"_tok) {
				auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (init_result.is_error() || !init_result.node().has_value()) {
					FLASH_LOG(Parser, Error, "Failed to parse brace initializer for static member variable '",
					          class_name.view(), "::", function_name_token.value(), "'");
					return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
				}
				init_expr = *init_result.node();
			}
			
			// Expect closing brace
			if (!consume("}"_tok)) {
				FLASH_LOG(Parser, Error, "Expected '}' after static member variable brace initializer");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			// Expect semicolon
			if (!consume(";"_tok)) {
				FLASH_LOG(Parser, Error, "Expected ';' after static member variable brace initializer");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			// Finalize the static member initializer (handling empty brace-init) and return the variable node
			return finalize_static_member_init(static_member, init_expr, decl_node, function_name_token, saved_position);
		}
		
		// Check if this is an out-of-line static member variable definition with copy initializer
		// Pattern: Type ClassName::member_name = expr;
		if (static_member != nullptr && peek() == "="_tok) {
			FLASH_LOG(Parser, Debug, "Found out-of-line static member variable definition with = init: ", 
			          class_name.view(), "::", function_name_token.value());
			
			advance();  // consume '='
			
			// Parse the initializer expression
			auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (init_result.is_error() || !init_result.node().has_value()) {
				FLASH_LOG(Parser, Error, "Failed to parse initializer for static member variable '",
				          class_name.view(), "::", function_name_token.value(), "'");
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			
			// Expect semicolon
			if (!consume(";"_tok)) {
				FLASH_LOG(Parser, Error, "Expected ';' after static member variable definition");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			return finalize_static_member_init(static_member, *init_result.node(), decl_node, function_name_token, saved_position);
		}
		
		// Create a new declaration node with the function name
		ASTNode return_type_node = decl_node.type_node();
		auto [func_decl_node, func_decl_ref] = emplace_node_ref<DeclarationNode>(return_type_node, function_name_token);
		
		// Create the FunctionDeclarationNode with parent struct name (marks it as member function)
		auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(func_decl_ref, class_name);
		
		// Parse the function parameters using unified parameter list parsing (Phase 1)
		FlashCpp::ParsedParameterList params;
		auto param_result = parse_parameter_list(params, attr_info.calling_convention);
		if (param_result.is_error()) {
			FLASH_LOG(Parser, Error, "Error parsing parameter list");
			return param_result;
		}

		// Skip optional qualifiers after parameter list using existing helper
		// Note: skip_function_trailing_specifiers() doesn't skip override/final as they have semantic meaning
		// For out-of-line definitions, we also skip override/final as they're already recorded in the declaration
		FlashCpp::MemberQualifiers member_quals;
		skip_function_trailing_specifiers(member_quals);
		
		// Also skip override/final for out-of-line definitions
		while (!peek().is_eof()) {
			auto next_val = peek_info().value();
			if (next_val == "override" || next_val == "final") {
				advance();
			} else {
				break;
			}
		}
		
		// Skip trailing requires clause for out-of-line definitions
		// (the constraint was already recorded during the in-class declaration)
		skip_trailing_requires_clause();

		// Apply parsed parameters to the function
		for (const auto& param : params.parameters) {
			func_ref.add_parameter_node(param);
		}
		func_ref.set_is_variadic(params.is_variadic);
		
		// Apply attributes
		func_ref.set_calling_convention(attr_info.calling_convention);
		if (attr_info.linkage == Linkage::DllImport || attr_info.linkage == Linkage::DllExport) {
			func_ref.set_linkage(attr_info.linkage);
		}
		func_ref.set_is_constexpr(is_constexpr);
		func_ref.set_is_constinit(is_constinit);
		func_ref.set_is_consteval(is_consteval);
		
		// Search for existing member function declaration with the same name, const qualification, and matching signature
		StructMemberFunction* existing_member = nullptr;
		size_t def_param_count = func_ref.parameter_nodes().size();
		for (auto& member : struct_info->member_functions) {
			if (member.getName() == function_name_token.handle() &&
				member.is_const == member_quals.is_const() &&
				member.is_volatile == member_quals.is_volatile()) {
				// Also check parameter count for overload resolution
				if (member.function_decl.is<FunctionDeclarationNode>()) {
					const auto& decl_func = member.function_decl.as<FunctionDeclarationNode>();
					if (decl_func.parameter_nodes().size() == def_param_count) {
						existing_member = &member;
						break;
					}
				} else {
					// If not a FunctionDeclarationNode, accept the first match
					existing_member = &member;
					break;
				}
			}
		}
		
		if (!existing_member) {
			// Check if there's a declaration with the same name but different signature
			// (e.g., const mismatch or parameter count mismatch for overloads)
			bool has_name_match = false;
			bool has_qualifier_match = false;
			for (const auto& member : struct_info->member_functions) {
				if (member.getName() == function_name_token.handle()) {
					has_name_match = true;
					if (member.is_const == member_quals.is_const() &&
						member.is_volatile == member_quals.is_volatile()) {
						has_qualifier_match = true;
					}
				}
			}
			if (has_name_match && !has_qualifier_match) {
				// Name matches but const/volatile qualifiers don't match
				FLASH_LOG(Parser, Error, "Out-of-line definition of '", class_name.view(), "::", function_name_token.value(), 
				          "' does not match any declaration in the class (const/volatile qualifier mismatch)");
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			if (has_name_match && has_qualifier_match) {
				// Name and qualifiers match but parameter count differs (overload not found)
				FLASH_LOG(Parser, Error, "Out-of-line definition of '", class_name.view(), "::", function_name_token.value(), 
				          "' does not match any declaration in the class (parameter count mismatch)");
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			
			// No declaration at all for this name. This can happen for:
			// 1. Template specializations where the body parser doesn't register in StructTypeInfo
			// 2. Out-of-line definitions that serve as the first declaration
			// Create the member function as a new entry (combined declaration+definition)
			FLASH_LOG(Parser, Debug, "No matching in-class declaration for '", class_name.view(), "::", function_name_token.value(), 
			          "' - creating new member function entry");
			
			// Note: const qualification is handled by the member function's StructMemberFunction entry
			
			struct_info->addMemberFunction(function_name_token.handle(), func_node,
				AccessSpecifier::Public,
				/*is_virtual=*/false,
				/*is_pure_virtual=*/false,
				/*is_override=*/false,
				/*is_final_func=*/false);
			// Propagate const/volatile qualifiers to the newly added member
			if (!struct_info->member_functions.empty()) {
				struct_info->member_functions.back().is_const = member_quals.is_const();
				struct_info->member_functions.back().is_volatile = member_quals.is_volatile();
			}

			// Check for declaration only (;) or function definition ({)
			if (consume(";"_tok)) {
				ast_nodes_.push_back(func_node);
				return saved_position.success(func_node);
			}
			
			// Parse function body
			if (peek() != "{"_tok) {
				FLASH_LOG(Parser, Error, "Expected '{' or ';' after function declaration, got: '",
					(!peek().is_eof() ? std::string(peek_info().value()) : "<EOF>"), "'");
				return ParseResult::error(ParserError::UnexpectedToken, peek_info());
			}
			
			// Enter function scope with RAII guard (Phase 3)
			FlashCpp::SymbolTableScope func_scope(ScopeType::Function);
			
			// Push member function context
			member_function_context_stack_.push_back({
				class_name,
				type_info->type_index_,
				nullptr,
				nullptr
			});
			
			// Add 'this' pointer to symbol table
			auto [this_type_node2, this_type_ref2] = emplace_node_ref<TypeSpecifierNode>(
				Type::Struct, type_info->type_index_, 
				static_cast<int>(struct_info->total_size * 8), Token()
			);
			this_type_ref2.add_pointer_level();
			
			Token this_token2(Token::Type::Keyword, "this"sv, 0, 0, 0);
			auto [this_decl_node2, this_decl_ref2] = emplace_node_ref<DeclarationNode>(this_type_node2, this_token2);
			gSymbolTable.insert("this"sv, this_decl_node2);
			
			// Add function parameters to symbol table
			for (const ASTNode& param_node : func_ref.parameter_nodes()) {
				if (param_node.is<VariableDeclarationNode>()) {
					const VariableDeclarationNode& var_decl = param_node.as<VariableDeclarationNode>();
					const DeclarationNode& param_decl = var_decl.declaration();
					gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
				} else if (param_node.is<DeclarationNode>()) {
					const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
					gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
				}
			}
			
			// Parse body using delayed parsing
			SaveHandle body_start = save_token_position();
			skip_balanced_braces();
			
			delayed_function_bodies_.push_back({
				&func_ref,                  // func_node
				body_start,                 // body_start
				{},                         // initializer_list_start (not used)
				class_name,                 // struct_name
				type_info->type_index_,     // struct_type_index
				nullptr,                    // struct_node (not available for out-of-line defs)
				false,                      // has_initializer_list
				false,                      // is_constructor
				false,                      // is_destructor
				nullptr,                    // ctor_node
				nullptr,                    // dtor_node
				{}                          // template_param_names (empty for non-template)
			});
			
			member_function_context_stack_.pop_back();
			
			ast_nodes_.push_back(func_node);
			return saved_position.success(func_node);
		}
		
		// Validate that the existing declaration is a FunctionDeclarationNode
		if (!existing_member->function_decl.is<FunctionDeclarationNode>()) {
			FLASH_LOG(Parser, Error, "Member '", function_name_token.value(), "' is not a function");
			return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
		}
	
		FunctionDeclarationNode& existing_func_ref = existing_member->function_decl.as<FunctionDeclarationNode>();
	
		// Phase 7: Use unified signature validation
		auto validation_result = validate_signature_match(existing_func_ref, func_ref);
		if (!validation_result.is_match()) {
			FLASH_LOG(Parser, Error, validation_result.error_message, " in out-of-line definition of '", 
					  class_name.view(), "::", function_name_token.value(), "'");
			return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
		}
		
		// Check for declaration only (;) or function definition ({)
		if (consume(";"_tok)) {
			// Declaration only
			return saved_position.success(func_node);
		}
		
		// Parse function body
		if (peek() != "{"_tok) {
			FLASH_LOG(Parser, Error, "Expected '{' or ';' after function declaration, got: '",
				(!peek().is_eof() ? std::string(peek_info().value()) : "<EOF>"), "'");
			return ParseResult::error(ParserError::UnexpectedToken, peek_info());
		}
		
		// Enter function scope with RAII guard (Phase 3)
		FlashCpp::SymbolTableScope func_scope(ScopeType::Function);
		
		// Push member function context so that member variables are resolved correctly
		member_function_context_stack_.push_back({
			class_name,
			type_info->type_index_,
			nullptr,  // struct_node - we don't have access to it here, but struct_type_index should be enough
			nullptr   // local_struct_info - not needed here since TypeInfo is already populated
		});
		
		// Add 'this' pointer to symbol table
		auto [this_type_node, this_type_ref] = emplace_node_ref<TypeSpecifierNode>(
			Type::Struct, type_info->type_index_, 
			static_cast<int>(struct_info->total_size * 8), Token()
		);
		this_type_ref.add_pointer_level();  // Make it a pointer
		
		Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
		auto [this_decl_node, this_decl_ref] = emplace_node_ref<DeclarationNode>(this_type_node, this_token);
		gSymbolTable.insert("this"sv, this_decl_node);
		
		// Add function parameters to symbol table using the DEFINITION's parameter names
		// (not the declaration's names - C++ allows different names between declaration and definition)
		// The types have already been validated to match via validate_signature_match()
		for (const ASTNode& param_node : func_ref.parameter_nodes()) {
			if (param_node.is<VariableDeclarationNode>()) {
				const VariableDeclarationNode& var_decl = param_node.as<VariableDeclarationNode>();
				const DeclarationNode& param_decl = var_decl.declaration();
				gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
			} else if (param_node.is<DeclarationNode>()) {
				const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
				gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
			}
		}
		
		// Parse function body
		ParseResult body_result = parse_block();
		
		if (body_result.is_error()) {
			member_function_context_stack_.pop_back();
			// func_scope automatically exits scope on return
			return body_result;
		}
	
		// existing_func_ref is already defined earlier after validation
		if (body_result.node().has_value()) {
			// Generate mangled name before setting definition (Phase 6 mangling)
			compute_and_set_mangled_name(existing_func_ref);
			if (!existing_func_ref.set_definition(*body_result.node())) {
				FLASH_LOG(Parser, Error, "Function '", class_name.view(), "::", function_name_token.value(), 
						  "' already has a definition");
				member_function_context_stack_.pop_back();
				// func_scope automatically exits scope on return
				return ParseResult::error(ParserError::UnexpectedToken, function_name_token);
			}
			// Update parameter nodes to use definition's parameter names
			// C++ allows declaration and definition to have different parameter names
			existing_func_ref.update_parameter_nodes_from_definition(func_ref.parameter_nodes());
			// Deduce auto return types from function body
			deduce_and_update_auto_return_type(existing_func_ref);
		}

		member_function_context_stack_.pop_back();
		// func_scope automatically exits scope at end of block
	
		// Return success without a node - the existing declaration already has the definition attached
		// Don't return the node because it's already in the AST from the struct declaration
		return saved_position.success();
	}

	// First, try to parse as a function definition
	// Save position before attempting function parse so we can backtrack if it's actually a variable
	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: About to try parse_function_declaration. current_token={}, peek={}", 
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	SaveHandle before_function_parse = save_token_position();
	ParseResult function_definition_result = parse_function_declaration(decl_node, attr_info.calling_convention);
	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: parse_function_declaration returned. is_error={}, current_token={}, peek={}", 
		function_definition_result.is_error(),
		std::string(current_token_.value()),
		!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	if (!function_definition_result.is_error()) {
		// Successfully parsed as function - discard saved position
		discard_saved_token(before_function_parse);
		// It was successfully parsed as a function definition
		// Apply attribute linkage if present (calling convention already set in parse_function_declaration)
		if (auto func_node_ptr = function_definition_result.node()) {
			FunctionDeclarationNode& func_node = func_node_ptr->as<FunctionDeclarationNode>();
			if (attr_info.linkage == Linkage::DllImport || attr_info.linkage == Linkage::DllExport) {
				func_node.set_linkage(attr_info.linkage);
			}
			func_node.set_is_constexpr(is_constexpr);
			func_node.set_is_constinit(is_constinit);
			func_node.set_is_consteval(is_consteval);
		}
		
		// Continue with function-specific logic
		TypeSpecifierNode& type_specifier = decl_node.type_node().as<TypeSpecifierNode>();
		
		// Parse trailing specifiers using Phase 2 unified method (instead of just skipping them)
		// For free functions: noexcept is applied, const/volatile/&/&&/override/final are ignored
		FlashCpp::MemberQualifiers member_quals;
		FlashCpp::FunctionSpecifiers func_specs;
		FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: About to parse_function_trailing_specifiers. current_token={}, peek={}", 
			std::string(current_token_.value()),
			!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		auto specs_result = parse_function_trailing_specifiers(member_quals, func_specs);
		FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: parse_function_trailing_specifiers returned. is_error={}, current_token={}, peek={}", 
			specs_result.is_error(),
			std::string(current_token_.value()),
			!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		if (specs_result.is_error()) {
			return specs_result;
		}
		
		// Apply noexcept specifier to free functions
		if (func_specs.is_noexcept) {
			if (auto func_node_ptr = function_definition_result.node()) {
				FunctionDeclarationNode& func_node = func_node_ptr->as<FunctionDeclarationNode>();
				func_node.set_noexcept(true);
				if (func_specs.noexcept_expr.has_value()) {
					func_node.set_noexcept_expression(*func_specs.noexcept_expr);
				}
			}
		}
		
		if (type_specifier.type() == Type::Auto) {
			const bool is_trailing_return_type = (peek() == "->"_tok);
			if (is_trailing_return_type) {
				advance();

				ParseResult trailing_type_specifier = parse_type_specifier();
				if (trailing_type_specifier.is_error())
					return trailing_type_specifier;

				// Apply pointer and reference qualifiers (ptr-operator in C++20 grammar)
				if (trailing_type_specifier.node().has_value() && 
				    trailing_type_specifier.node()->is<TypeSpecifierNode>()) {
					TypeSpecifierNode& trailing_ts = trailing_type_specifier.node()->as<TypeSpecifierNode>();
					consume_pointer_ref_modifiers(trailing_ts);
				}

				type_specifier = as<TypeSpecifierNode>(trailing_type_specifier);
			}
		}

		const Token& identifier_token = decl_node.identifier_token();
		StringHandle func_name = identifier_token.handle();
		
		// C++20 Abbreviated Function Templates: Check if any parameter has auto type
		// If so, convert this function to an implicit function template
		if (auto func_node_ptr = function_definition_result.node()) {
			FunctionDeclarationNode& func_decl = func_node_ptr->as<FunctionDeclarationNode>();
			
			// Count auto parameters and collect their info including concept constraints
			struct AutoParamInfo {
				size_t index;
				Token token;
				std::string_view concept_name;  // Empty if unconstrained
			};
			std::vector<AutoParamInfo> auto_params;
			const auto& params = func_decl.parameter_nodes();
			for (size_t i = 0; i < params.size(); ++i) {
				if (params[i].is<DeclarationNode>()) {
					const DeclarationNode& param_decl = params[i].as<DeclarationNode>();
					const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					if (param_type.type() == Type::Auto) {
						std::string_view concept_constraint = param_type.has_concept_constraint() ? param_type.concept_constraint() : std::string_view{};
						auto_params.push_back({i, param_decl.identifier_token(), concept_constraint});
					}
				}
			}
			
			// If we have auto parameters, convert to abbreviated function template
			if (!auto_params.empty()) {
				// Create synthetic template parameters for each auto parameter
				// Each auto becomes a unique template type parameter: _T0, _T1, etc.
				std::vector<ASTNode> template_params;
				std::vector<StringHandle> template_param_names;
				
				for (size_t i = 0; i < auto_params.size(); ++i) {
					// Generate synthetic parameter name like "_T0", "_T1", etc.
					// Using underscore prefix to avoid conflicts with user-defined names
					// StringBuilder.commit() returns a persistent string_view
					StringHandle param_name = StringTable::getOrInternStringHandle(StringBuilder().append("_T"sv).append(static_cast<int64_t>(i)));
					
					// Use the auto parameter's token for position/error reporting
					Token param_token = auto_params[i].token;
					
					// Create a type template parameter node
					auto param_node = emplace_node<TemplateParameterNode>(param_name, param_token);
					
					// Set concept constraint if the auto parameter was constrained (e.g., IsInt auto x)
					if (!auto_params[i].concept_name.empty()) {
						param_node.as<TemplateParameterNode>().set_concept_constraint(auto_params[i].concept_name);
					}
					
					template_params.push_back(param_node);
					template_param_names.push_back(param_name);
				}
				
				// Create the TemplateFunctionDeclarationNode wrapping the function
				auto template_func_node = emplace_node<TemplateFunctionDeclarationNode>(
					std::move(template_params),
					*func_node_ptr,
					std::nullopt  // No requires clause for abbreviated templates
				);
				
				// Register the template in the template registry
				gTemplateRegistry.registerTemplate(func_name, template_func_node);
				
				// Also register the template parameter names for lookup
				gTemplateRegistry.registerTemplateParameters(func_name, template_param_names);
				
				// Add the template function to the symbol table
				gSymbolTable.insert(func_name, template_func_node);
				
				// Set template param names for parsing body (for template parameter recognition)
				current_template_param_names_ = template_param_names;
				
				// Check if this is just a declaration (no body)
				if (peek() == ";"_tok) {
					advance();  // consume ';'
					current_template_param_names_.clear();
					return saved_position.success(template_func_node);
				}
				
				// Has a body - save position at the '{' for delayed parsing during instantiation
				// Note: set_template_body_position is on FunctionDeclarationNode, which is the
				// underlying node inside TemplateFunctionDeclarationNode - this is consistent
				// with how regular template functions store their body position
				if (peek() == "{"_tok) {
					SaveHandle body_start = save_token_position();
					func_decl.set_template_body_position(body_start);
					skip_balanced_braces();
				}
				
				current_template_param_names_.clear();
				return saved_position.success(template_func_node);
			}
		}
		
		// Insert the FunctionDeclarationNode (which contains parameter info for overload resolution)
		// instead of just the DeclarationNode
		if (auto func_node = function_definition_result.node()) {
			if (!gSymbolTable.insert(func_name, *func_node)) {
				// Note: With overloading support, insert() now allows multiple functions with same name
				// It only returns false for non-function duplicate symbols
				return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, identifier_token);
			}
		}

		// Is only function declaration
		FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: Checking for ';' vs function body. current_token={}, peek={}", 
			std::string(current_token_.value()),
			!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		if (consume(";"_tok)) {
			// Return the function declaration node (needed for templates)
			if (auto func_node = function_definition_result.node()) {
				return saved_position.success(*func_node);
			}
			return saved_position.success();
		}

		// Add function parameters to the symbol table within a function scope (Phase 3: RAII)
		FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: About to parse function body. current_token={}, peek={}", 
			std::string(current_token_.value()),
			!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		FlashCpp::SymbolTableScope func_scope(ScopeType::Function);

		// Set current function pointer for __func__, __PRETTY_FUNCTION__
		// The FunctionDeclarationNode persists in the AST, so the pointer is safe
		if (auto funcNode = function_definition_result.node()) {
			const auto& func_decl = funcNode->as<FunctionDeclarationNode>();
			current_function_ = &func_decl;

			for (const auto& param : func_decl.parameter_nodes()) {
				if (param.is<DeclarationNode>()) {
					const auto& param_decl_node = param.as<DeclarationNode>();
					const Token& param_token = param_decl_node.identifier_token();
					gSymbolTable.insert(param_token.value(), param);
				}
			}

			// Note: trailing specifiers were already skipped after parse_function_declaration()

			// Parse function body
			FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration_or_function_definition: About to call parse_block. current_token={}, peek={}", 
				std::string(current_token_.value()),
				!peek().is_eof() ? std::string(peek_info().value()) : "N/A");
			auto block_result = parse_block();
			if (block_result.is_error()) {
				current_function_ = nullptr;
				// func_scope automatically exits scope
				return block_result;
			}

			current_function_ = nullptr;
			// func_scope automatically exits scope

			if (auto node = function_definition_result.node()) {
				if (auto block = block_result.node()) {
					FunctionDeclarationNode& final_func_decl = node->as<FunctionDeclarationNode>();
					// Generate mangled name before finalizing (Phase 6 mangling)
					compute_and_set_mangled_name(final_func_decl);
					final_func_decl.set_definition(*block);
					// Deduce auto return types from function body
					deduce_and_update_auto_return_type(final_func_decl);
					return saved_position.success(*node);
				}
			}
			// If we get here, something went wrong but we should still commit
			// because we successfully parsed a function
			return saved_position.success();
		}
	} else {
		// Function parsing failed - restore position to try variable declaration
		restore_token_position(before_function_parse);
		
		// If the error is a semantic error (not a syntax error about expecting '('),
		// return it directly instead of trying variable declaration parsing
		std::string error_msg = function_definition_result.error_message();
		if (error_msg.find("Variadic") != std::string::npos ||
		    error_msg.find("calling convention") != std::string::npos) {
			return function_definition_result;
		}
		
		// Otherwise, try parsing as a variable declaration
		// Attempt to parse a simple declaration (global variable or typedef)
		// Check for initialization
		std::optional<ASTNode> initializer;
		
		// Get the type specifier for brace initializer parsing and constexpr checks
		// This is always safe since decl_node is a DeclarationNode with a TypeSpecifierNode
		TypeSpecifierNode& type_specifier = decl_node.type_node().as<TypeSpecifierNode>();
		
		// Skip GCC __attribute__((...)) between declarator and initializer/semicolon
		// e.g.: int __ret __attribute__((__unused__)) = expr;
		skip_gcc_attributes();

		// Phase 3 Consolidation: Use shared copy initialization helper for = and = {} forms
		if (peek() == "="_tok) {
			auto init_result = parse_copy_initialization(decl_node, type_specifier);
			if (init_result.has_value()) {
				initializer = init_result;
			} else {
				return ParseResult::error("Failed to parse initializer expression", current_token_);
			}
		} else if (peek() == "{"_tok) {
			// Direct list initialization: Type var{args}
			ParseResult init_list_result = parse_brace_initializer(type_specifier);
			if (init_list_result.is_error()) {
				return init_list_result;
			}
			initializer = init_list_result.node();
		} else if (peek() == "("_tok) {
			// Direct initialization: Type var(args)
			// At global scope with struct types, use ConstructorCallNode for constexpr evaluation.
			// At block scope, use InitializerListNode consistent with parse_variable_declaration.
			bool is_global_scope = (gSymbolTable.get_current_scope_type() == ScopeType::Global);
			if (is_global_scope && type_specifier.type() == Type::Struct) {
				Token paren_token = peek_info();
				advance(); // consume '('
				ChunkedVector<ASTNode> arguments;
				while (!peek().is_eof() && peek() != ")"_tok) {
					auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (arg_result.is_error()) {
						return arg_result;
					}
					if (auto arg_node = arg_result.node()) {
						arguments.push_back(*arg_node);
					}
					if (peek() == ","_tok) {
						advance();
					} else {
						break;
					}
				}
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after constructor arguments", current_token_);
				}
				ASTNode type_node_copy = decl_node.type_node();
				initializer = ASTNode::emplace_node<ConstructorCallNode>(
					type_node_copy, std::move(arguments), paren_token);
			} else {
				auto init_result = parse_direct_initialization();
				if (init_result.has_value()) {
					initializer = init_result;
				} else {
					return ParseResult::error("Expected ')' after direct initialization arguments", current_token_);
				}
			}
		}

		// Create a variable declaration node for the first variable
		// Reuse the existing decl_node from type_and_name_result
		auto [global_var_node, global_decl_node] = emplace_node_ref<VariableDeclarationNode>(
			type_and_name_result.node().value(),  // Use the existing DeclarationNode
			initializer,
			specs.storage_class
		);
		global_decl_node.set_is_constexpr(is_constexpr);
		global_decl_node.set_is_constinit(is_constinit);

		// Get identifier token for error reporting
		const Token& identifier_token = decl_node.identifier_token();

		// Semantic checks for constexpr/constinit - only enforce for global/static variables
		// For local constexpr variables, they can fall back to runtime initialization (like const)
		bool is_global_scope = (gSymbolTable.get_current_scope_type() == ScopeType::Global);
		
		if ((is_constexpr || is_constinit) && is_global_scope) {
			const char* keyword_name = is_constexpr ? "constexpr" : "constinit";
			
			// Both constexpr and constinit require an initializer
			if (!initializer.has_value()) {
				return ParseResult::error(
					std::string(keyword_name) + " variable must have an initializer", 
					identifier_token
				);
			}
			
			// Skip constexpr evaluation for struct types with initializer lists
			// The constexpr evaluator doesn't currently support InitializerListNode
			// Also skip for expressions that contain casts or other unsupported operations
			// TODO: Implement full constexpr evaluation
			bool is_struct_init_list = (type_specifier.type() == Type::Struct && 
			                            initializer->is<InitializerListNode>());
			
			if (!is_struct_init_list) {
				// Evaluate the initializer to ensure it's a constant expression
				ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
				eval_ctx.storage_duration = ConstExpr::StorageDuration::Global;
				eval_ctx.is_constinit = is_constinit;
				
				auto eval_result = ConstExpr::Evaluator::evaluate(initializer.value(), eval_ctx);
				// C++ semantics distinction between constexpr and constinit:
				// - constexpr: variable CAN be used in constant expressions if initialized with a 
				//   constant expression, but it's not required at parse time. If evaluation fails,
				//   the variable is treated as a regular const variable.
				// - constinit: variable MUST be initialized with a constant expression (C++20).
				//   Failure to evaluate at compile time is always an error.
				if (!eval_result.success() && is_constinit) {
					return ParseResult::error(
						std::string(keyword_name) + " variable initializer must be a constant expression: " + eval_result.error_message,
						identifier_token
					);
				}
			}
			
			// Note: The evaluated value could be stored in the VariableDeclarationNode for later use
			// For now, we just validate that it can be evaluated
		}
		
		// For local constexpr variables, treat them like const - no validation, just runtime initialization
		// This follows C++ standard: constexpr means "can be used in constant expressions"
		// but doesn't require compile-time evaluation for local variables


		// Add to symbol table
		if (!gSymbolTable.insert(identifier_token.value(), global_var_node)) {
			return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, identifier_token);
		}

		// Handle comma-separated declarations (e.g., int x, y, z;)
		// When there are additional variables, collect them all in a BlockNode
		if (peek() == ","_tok) {
			// Create a block to hold all declarations
			auto [block_node, block_ref] = emplace_node_ref<BlockNode>();
			
			// Add the first declaration to the block
			block_ref.add_statement_node(global_var_node);
			
			while (peek() == ","_tok) {
				advance(); // consume ','

				// Parse the next variable name
				auto next_identifier_token = advance();
				if (!next_identifier_token.kind().is_identifier()) {
					return ParseResult::error("Expected identifier after comma in declaration list", current_token_);
				}

				// Create a new DeclarationNode with the same type
				DeclarationNode& next_decl = emplace_node<DeclarationNode>(
					emplace_node<TypeSpecifierNode>(type_specifier),
					next_identifier_token
				).as<DeclarationNode>();
				TypeSpecifierNode& next_type_spec = next_decl.type_node().as<TypeSpecifierNode>();

				// Phase 3 Consolidation: Use shared copy initialization helper
				std::optional<ASTNode> next_initializer;
				if (peek() == "="_tok) {
					auto init_result = parse_copy_initialization(next_decl, next_type_spec);
					if (init_result.has_value()) {
						next_initializer = init_result;
					} else {
						return ParseResult::error("Failed to parse initializer expression", current_token_);
					}
				} else if (peek() == "("_tok) {
					// Direct initialization for comma-separated declaration: Type var1, var2(args)
					auto init_result = parse_direct_initialization();
					if (init_result.has_value()) {
						next_initializer = init_result;
					} else {
						return ParseResult::error("Expected ')' after direct initialization arguments", current_token_);
					}
				} else if (peek() == "{"_tok) {
					// Direct list initialization for comma-separated declaration: Type var1, var2{args}
					ParseResult init_list_result = parse_brace_initializer(type_specifier);
					if (init_list_result.is_error()) {
						return init_list_result;
					}
					next_initializer = init_list_result.node();
				}

				// Create a variable declaration node for this additional variable
				auto [next_var_node, next_var_decl] = emplace_node_ref<VariableDeclarationNode>(
					emplace_node<DeclarationNode>(next_decl),
					next_initializer,
					specs.storage_class
				);
				next_var_decl.set_is_constexpr(is_constexpr);
				next_var_decl.set_is_constinit(is_constinit);

				// Add to symbol table
				if (!gSymbolTable.insert(next_identifier_token.value(), next_var_node)) {
					return ParseResult::error(ParserError::RedefinedSymbolWithDifferentValue, next_identifier_token);
				}
				
				// Add to block
				block_ref.add_statement_node(next_var_node);
			}

			// Expect semicolon after all declarations
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after declaration", current_token_);
			}

			return saved_position.success(block_node);
		}

		// Single declaration - expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after declaration", current_token_);
		}

		return saved_position.success(global_var_node);
	}

	// This should not be reached
	return ParseResult::error("Unexpected parsing state", current_token_);
}

// Parse out-of-line constructor or destructor definition
// Pattern: ClassName::ClassName(...) { ... } or ClassName::~ClassName() { ... }
ParseResult Parser::parse_out_of_line_constructor_or_destructor(std::string_view class_name, bool is_destructor, const FlashCpp::DeclarationSpecifiers& specs)
{
	ScopedTokenPosition saved_position(*this);
	
	FLASH_LOG_FORMAT(Parser, Debug, "parse_out_of_line_constructor_or_destructor: class={}, is_destructor={}", 
		std::string(class_name), is_destructor);
	
	// Consume ClassName::~?ClassName
	Token class_name_token = peek_info();
	advance();  // consume first class name
	
	if (!consume("::"_tok)) {
		return ParseResult::error("Expected '::' in out-of-line constructor/destructor definition", current_token_);
	}
	
	if (is_destructor) {
		// Check for ~ (might be operator type, not punctuator)
		if (peek() != "~"_tok) {
			return ParseResult::error("Expected '~' for destructor definition", current_token_);
		}
		advance();  // consume ~
	}
	
	// Consume the second class name (constructor/destructor name)
	Token func_name_token = peek_info();
	advance();
	
	// Find the struct in the type registry
	StringHandle class_name_handle = StringTable::getOrInternStringHandle(class_name);
	auto struct_iter = gTypesByName.find(class_name_handle);
	if (struct_iter == gTypesByName.end()) {
		FLASH_LOG(Parser, Error, "Unknown class '", class_name, "' in out-of-line constructor/destructor definition");
		return ParseResult::error("Unknown class in out-of-line constructor/destructor", class_name_token);
	}
	
	TypeInfo* type_info = struct_iter->second;
	StructTypeInfo* struct_info = type_info->getStructInfo();
	if (!struct_info) {
		FLASH_LOG(Parser, Error, "'", class_name, "' is not a struct/class type");
		return ParseResult::error("Not a struct/class type", class_name_token);
	}
	
	// Parse parameter list
	FlashCpp::ParsedParameterList params;
	auto param_result = parse_parameter_list(params, specs.calling_convention);
	if (param_result.is_error()) {
		return param_result;
	}
	
	// Skip optional qualifiers (noexcept, const, etc.) using existing helper
	FlashCpp::MemberQualifiers member_quals;
	skip_function_trailing_specifiers(member_quals);
	
	// Skip trailing requires clause for out-of-line constructor/destructor definitions
	skip_trailing_requires_clause();
	
	// Find the matching constructor/destructor declaration in the struct
	StructMemberFunction* existing_member = nullptr;
	size_t param_count = params.parameters.size();
	
	for (auto& member : struct_info->member_functions) {
		if (is_destructor && member.is_destructor) {
			// Destructors have no parameters to match
			if (member.function_decl.is<DestructorDeclarationNode>()) {
				const DestructorDeclarationNode& dtor = member.function_decl.as<DestructorDeclarationNode>();
				// Skip if already has definition
				if (dtor.get_definition().has_value()) {
					continue;
				}
			}
			existing_member = &member;
			break;
		} else if (!is_destructor && member.is_constructor) {
			// For constructors, match by parameter count and types
			if (member.function_decl.is<ConstructorDeclarationNode>()) {
				const ConstructorDeclarationNode& ctor = member.function_decl.as<ConstructorDeclarationNode>();
				
				// Skip if already has definition
				if (ctor.get_definition().has_value()) {
					continue;
				}
				
				// Check parameter count first
				if (ctor.parameter_nodes().size() != param_count) {
					continue;
				}
				
				// Match parameter types
				bool params_match = true;
				for (size_t i = 0; i < param_count && params_match; ++i) {
					const ASTNode& decl_param = ctor.parameter_nodes()[i];
					const ASTNode& def_param = params.parameters[i];
					
					// Get type info from both parameters
					const TypeSpecifierNode* decl_type = nullptr;
					const TypeSpecifierNode* def_type = nullptr;
					
					if (decl_param.is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var = decl_param.as<VariableDeclarationNode>();
						if (var.declaration().type_node().is<TypeSpecifierNode>()) {
							decl_type = &var.declaration().type_node().as<TypeSpecifierNode>();
						}
					} else if (decl_param.is<DeclarationNode>()) {
						const DeclarationNode& decl = decl_param.as<DeclarationNode>();
						if (decl.type_node().is<TypeSpecifierNode>()) {
							decl_type = &decl.type_node().as<TypeSpecifierNode>();
						}
					}
					
					if (def_param.is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var = def_param.as<VariableDeclarationNode>();
						if (var.declaration().type_node().is<TypeSpecifierNode>()) {
							def_type = &var.declaration().type_node().as<TypeSpecifierNode>();
						}
					} else if (def_param.is<DeclarationNode>()) {
						const DeclarationNode& decl = def_param.as<DeclarationNode>();
						if (decl.type_node().is<TypeSpecifierNode>()) {
							def_type = &decl.type_node().as<TypeSpecifierNode>();
						}
					}
					
					if (!decl_type || !def_type) {
						params_match = false;
						continue;
					}
					
					// Compare types
					if (decl_type->type() != def_type->type()) {
						params_match = false;
					} else if (decl_type->pointer_depth() != def_type->pointer_depth()) {
						params_match = false;
					} else if (decl_type->is_reference() != def_type->is_reference()) {
						params_match = false;
					} else if (decl_type->type_index() != def_type->type_index()) {
						// For user-defined types, check type_index
						params_match = false;
					}
				}
				
				if (params_match) {
					existing_member = &member;
					break;
				}
			}
		}
	}
	
	if (!existing_member) {
		FLASH_LOG(Parser, Error, "Out-of-line definition of '", class_name, is_destructor ? "::~" : "::", class_name, 
		          "' does not match any declaration in the class");
		return ParseResult::error("No matching declaration found", func_name_token);
	}
	
	// Get mutable reference to constructor for adding member initializers
	ConstructorDeclarationNode* ctor_ref = nullptr;
	if (!is_destructor && existing_member->function_decl.is<ConstructorDeclarationNode>()) {
		ctor_ref = &existing_member->function_decl.as<ConstructorDeclarationNode>();
	}
	
	// Enter function scope with RAII guard - need to do this before parsing initializer list
	// so that expressions in the initializer can reference parameters
	FlashCpp::SymbolTableScope func_scope(ScopeType::Function);
	
	// Push member function context so that member variables are resolved correctly
	member_function_context_stack_.push_back({
		class_name_handle,
		type_info->type_index_,
		nullptr,  // struct_node - we don't have access to it here
		nullptr   // local_struct_info - not needed here
	});
	
	// Add 'this' pointer to symbol table
	auto [this_type_node, this_type_ref] = emplace_node_ref<TypeSpecifierNode>(
		Type::Struct, type_info->type_index_, 
		static_cast<int>(struct_info->total_size * 8), Token()
	);
	this_type_ref.add_pointer_level();  // Make it a pointer
	
	Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
	auto [this_decl_node, this_decl_ref] = emplace_node_ref<DeclarationNode>(this_type_node, this_token);
	gSymbolTable.insert("this"sv, this_decl_node);
	
	// Add function parameters to symbol table - use the DEFINITION's parameters (params.parameters)
	// not the declaration's parameters, since they may have different names
	for (const ASTNode& param_node : params.parameters) {
		if (param_node.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = param_node.as<VariableDeclarationNode>();
			const DeclarationNode& param_decl = var_decl.declaration();
			if (!param_decl.identifier_token().value().empty()) {
				gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
			}
		} else if (param_node.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
			if (!param_decl.identifier_token().value().empty()) {
				gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
			}
		}
	}
	
	// For constructors, parse member initializer list
	if (!is_destructor && peek() == ":"_tok) {
		advance();  // consume ':'
		
		while (!peek().is_eof() &&
		       peek() != "{"_tok &&
		       peek() != ";"_tok) {
			auto init_name_token = advance();
			if (!init_name_token.kind().is_identifier()) {
				member_function_context_stack_.pop_back();
				return ParseResult::error("Expected member name in initializer list", init_name_token);
			}
			
			std::string_view init_name = init_name_token.value();
			
			// Check for template arguments: Base<T>(...) in base class initializer
			if (peek() == "<"_tok) {
				skip_template_arguments();
			}
			
			bool is_paren = peek() == "("_tok;
			bool is_brace = peek() == "{"_tok;
			
			if (!is_paren && !is_brace) {
				member_function_context_stack_.pop_back();
				return ParseResult::error("Expected '(' or '{' after initializer name", peek_info());
			}
			
			advance();  // consume '(' or '{'
			TokenKind close_kind = [is_paren]() { if (is_paren) return ")"_tok; return "}"_tok; }();
			
			std::vector<ASTNode> init_args;
			if (peek() != close_kind) {
				do {
					ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (arg_result.is_error()) {
						member_function_context_stack_.pop_back();
						return arg_result;
					}
					if (auto arg_node = arg_result.node()) {
						init_args.push_back(*arg_node);
					}
				} while (peek() == ","_tok && (advance(), true));
			}
			
			if (!consume(close_kind)) {
				member_function_context_stack_.pop_back();
				return ParseResult::error(is_paren ?
				    "Expected ')' after initializer arguments" :
				    "Expected '}' after initializer arguments", peek_info());
			}
			
			// Add member initializer to constructor
			if (ctor_ref && !init_args.empty()) {
				ctor_ref->add_member_initializer(init_name, init_args[0]);
			}
			
			if (!consume(","_tok)) {
				break;
			}
		}
	}
	
	// Parse function body
	if (peek() != "{"_tok) {
		member_function_context_stack_.pop_back();
		return ParseResult::error("Expected '{' in constructor/destructor definition", current_token_);
	}
	
	// Parse function body
	ParseResult body_result = parse_block();
	
	if (body_result.is_error()) {
		member_function_context_stack_.pop_back();
		return body_result;
	}
	
	// Set the definition on the existing declaration
	if (body_result.node().has_value()) {
		if (is_destructor && existing_member->function_decl.is<DestructorDeclarationNode>()) {
			DestructorDeclarationNode& dtor = existing_member->function_decl.as<DestructorDeclarationNode>();
			if (!dtor.set_definition(*body_result.node())) {
				FLASH_LOG(Parser, Error, "Destructor '", class_name, "::~", class_name, "' already has a definition");
				member_function_context_stack_.pop_back();
				return ParseResult::error("Destructor already has definition", func_name_token);
			}
			// Note: Destructors have no parameters, so no need to update parameter nodes
		} else if (ctor_ref) {
			if (!ctor_ref->set_definition(*body_result.node())) {
				FLASH_LOG(Parser, Error, "Constructor '", class_name, "::", class_name, "' already has a definition");
				member_function_context_stack_.pop_back();
				return ParseResult::error("Constructor already has definition", func_name_token);
			}
			// Update parameter nodes to use definition's parameter names
			// C++ allows declaration and definition to have different parameter names
			ctor_ref->update_parameter_nodes_from_definition(params.parameters);
		}
	}
	
	member_function_context_stack_.pop_back();
	
	FLASH_LOG_FORMAT(Parser, Debug, "parse_out_of_line_constructor_or_destructor: Successfully parsed {}::{}{}()", 
		std::string(class_name), is_destructor ? "~" : "", std::string(class_name));
	
	// Return success - the existing declaration already has the definition attached
	return saved_position.success();
}

// Helper function to parse and register a type alias (typedef or using) inside a struct/template
// Handles both "typedef Type Alias;" and "using Alias = Type;" syntax
// Also handles inline definitions: "typedef struct { ... } Alias;"
// Also handles using-declarations: "using namespace::name;" (member access import)
// Returns ParseResult with no node on success, error on failure
ParseResult Parser::finalize_static_member_init(StructStaticMember* static_member,
                                                 std::optional<ASTNode> init_expr,
                                                 DeclarationNode& decl_node,
                                                 const Token& name_token,
                                                 ScopedTokenPosition& saved_position) {
	ASTNode return_type_node = decl_node.type_node();
	auto [var_decl_node, var_decl_ref] = emplace_node_ref<DeclarationNode>(return_type_node, name_token);

	if (init_expr.has_value()) {
		static_member->initializer = *init_expr;
		auto [var_node, var_ref] = emplace_node_ref<VariableDeclarationNode>(var_decl_node, *init_expr);
		return saved_position.success(var_node);
	}

	// Empty brace init - create a zero literal matching the static member's type
	Type member_type = static_member->type;
	unsigned char member_size_bits = static_cast<unsigned char>(static_member->size * 8);
	if (member_size_bits == 0) {
		member_size_bits = 32;
	}
	NumericLiteralValue zero_value;
	std::string_view zero_str;
	if (member_type == Type::Float || member_type == Type::Double || member_type == Type::LongDouble) {
		zero_value = 0.0;
		zero_str = "0.0"sv;
	} else {
		zero_value = 0ULL;
		zero_str = "0"sv;
	}
	Token zero_token(Token::Type::Literal, zero_str, 0, 0, 0);
	auto literal = emplace_node<ExpressionNode>(NumericLiteralNode(zero_token, zero_value, member_type, TypeQualifier::None, member_size_bits));
	auto [var_node, var_ref] = emplace_node_ref<VariableDeclarationNode>(var_decl_node, literal);
	return saved_position.success(var_node);
}
