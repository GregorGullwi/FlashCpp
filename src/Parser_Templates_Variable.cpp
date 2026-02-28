ParseResult Parser::parse_member_template_alias(StructDeclarationNode& struct_node, [[maybe_unused]] AccessSpecifier access) {
	ScopedTokenPosition saved_position(*this);

	// Consume 'template' keyword
	if (!consume("template"_tok)) {
		return ParseResult::error("Expected 'template' keyword", peek_info());
	}

	// Expect '<' to start template parameter list
	if (peek() != "<"_tok) {
		return ParseResult::error("Expected '<' after 'template' keyword", current_token_);
	}
	advance(); // consume '<'

	// Parse template parameter list
	std::vector<ASTNode> template_params;
	std::vector<StringHandle> template_param_names;

	auto param_list_result = parse_template_parameter_list(template_params);
	if (param_list_result.is_error()) {
		return param_list_result;
	}

	// Extract parameter names for later lookup
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			template_param_names.push_back(param.as<TemplateParameterNode>().nameHandle());
		}
	}

	// Expect '>' to close template parameter list
	if (peek() != ">"_tok) {
		return ParseResult::error("Expected '>' after template parameter list", current_token_);
	}
	advance(); // consume '>'

	// Temporarily add template parameters to type system using RAII scope guard
	FlashCpp::TemplateParameterScope template_scope;
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
			if (tparam.kind() == TemplateParameterKind::Type) {
				auto& type_info = add_user_type(tparam.nameHandle(), 0); // Do we need a correct size here?
				template_scope.addParameter(&type_info);
			}
		}
	}
	
	// Set template parameter context for parsing the requires clause
	auto saved_template_param_names = current_template_param_names_;
	current_template_param_names_ = template_param_names;
	bool saved_parsing_template_body = parsing_template_body_;
	parsing_template_body_ = true;

	// Handle optional requires clause
	// Pattern: template<typename T> requires Constraint using Alias = T;
	std::optional<ASTNode> requires_clause;
	if (peek() == "requires"_tok) {
		Token requires_token = peek_info();
		advance(); // consume 'requires'
		
		// Parse the constraint expression
		auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (constraint_result.is_error()) {
			// Clean up template parameter context before returning
			current_template_param_names_ = saved_template_param_names;
			parsing_template_body_ = saved_parsing_template_body;
			return constraint_result;
		}
		
		// Create RequiresClauseNode
		requires_clause = emplace_node<RequiresClauseNode>(
			*constraint_result.node(),
			requires_token
		);
		
		FLASH_LOG(Parser, Debug, "Parsed requires clause for member template alias");
	}

	// Expect 'using' keyword
	if (!consume("using"_tok)) {
		current_template_param_names_ = saved_template_param_names;
		parsing_template_body_ = saved_parsing_template_body;
		return ParseResult::error("Expected 'using' keyword in member template alias", peek_info());
	}

	// Parse alias name
	if (!peek().is_identifier()) {
		current_template_param_names_ = saved_template_param_names;
		parsing_template_body_ = saved_parsing_template_body;
		return ParseResult::error("Expected alias name after 'using' in member template alias", current_token_);
	}
	Token alias_name_token = peek_info();
	std::string_view alias_name = alias_name_token.value();
	advance();

	// Expect '='
	if (peek() != "="_tok) {
		current_template_param_names_ = saved_template_param_names;
		parsing_template_body_ = saved_parsing_template_body;
		return ParseResult::error("Expected '=' after alias name in member template alias", current_token_);
	}
	advance(); // consume '='

	// Parse the target type
	ParseResult type_result = parse_type_specifier();
	if (type_result.is_error()) {
		current_template_param_names_ = saved_template_param_names;
		parsing_template_body_ = saved_parsing_template_body;
		return type_result;
	}

	// Get the TypeSpecifierNode and check for pointer/reference modifiers
	TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
	consume_pointer_ref_modifiers(type_spec);

	// Expect semicolon
	if (!consume(";"_tok)) {
		current_template_param_names_ = saved_template_param_names;
		parsing_template_body_ = saved_parsing_template_body;
		return ParseResult::error("Expected ';' after member template alias declaration", current_token_);
	}

	// Create TemplateAliasNode
	auto alias_node = emplace_node<TemplateAliasNode>(
		std::move(template_params),
		std::move(template_param_names),
		StringTable::getOrInternStringHandle(alias_name),
		type_result.node().value()
	);

	// Register the alias template with qualified name (ClassName::AliasName)
	StringHandle qualified_name = StringTable::getOrInternStringHandle(
		StringBuilder().append(struct_node.name()).append("::").append(alias_name));
	gTemplateRegistry.register_alias_template(qualified_name, alias_node);

	FLASH_LOG_FORMAT(Parser, Info, "Registered member template alias: {}", StringTable::getStringView(qualified_name));

	// Restore template parameter context
	current_template_param_names_ = saved_template_param_names;
	parsing_template_body_ = saved_parsing_template_body;
	
	// template_scope automatically cleans up template parameters when it goes out of scope

	return saved_position.success();
}

// Parse member struct/class template: template<typename T> struct Name { ... };
ParseResult Parser::parse_member_variable_template(StructDeclarationNode& struct_node, [[maybe_unused]] AccessSpecifier access) {
	ScopedTokenPosition saved_position(*this);
	
	// Consume 'template' keyword
	if (!consume("template"_tok)) {
		return ParseResult::error("Expected 'template' keyword", peek_info());
	}
	
	// Parse template parameter list
	if (peek() != "<"_tok) {
		return ParseResult::error("Expected '<' after 'template' keyword", current_token_);
	}
	advance(); // consume '<'
	
	std::vector<ASTNode> template_params;
	std::vector<std::string_view> template_param_names;
	
	auto param_list_result = parse_template_parameter_list(template_params);
	if (param_list_result.is_error()) {
		return param_list_result;
	}
	
	// Extract parameter names
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			template_param_names.push_back(param.as<TemplateParameterNode>().name());
		}
	}
	
	// Expect '>'
	if (peek() != ">"_tok) {
		return ParseResult::error("Expected '>' after template parameter list", current_token_);
	}
	advance(); // consume '>'
	
	// Temporarily add template parameters to type system using RAII scope guard
	FlashCpp::TemplateParameterScope template_scope;
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
			if (tparam.kind() == TemplateParameterKind::Type) {
				auto& type_info = add_user_type(tparam.nameHandle(), 0); // Do we need a correct size here?
				template_scope.addParameter(&type_info);
			}
		}
	}
	
	// Parse storage class specifiers (static, constexpr, inline, etc.)
	bool is_constexpr = false;
	StorageClass storage_class = StorageClass::None;
	
	while (peek().is_keyword()) {
		auto kw = peek();
		if (kw == "constexpr"_tok) {
			is_constexpr = true;
			advance();
		} else if (kw == "inline"_tok) {
			advance(); // consume but don't store for now
		} else if (kw == "static"_tok) {
			storage_class = StorageClass::Static;
			advance();
		} else {
			break; // Not a storage class specifier
		}
	}
	
	// Parse the type
	auto type_result = parse_type_specifier();
	if (type_result.is_error()) {
		return type_result;
	}
	
	// Parse variable name
	if (!peek().is_identifier()) {
		return ParseResult::error("Expected variable name in member variable template", current_token_);
	}
	Token var_name_token = peek_info();
	std::string_view var_name = var_name_token.value();
	advance();
	
	// Handle variable template partial specialization: name<args> = expr;
	[[maybe_unused]] bool is_partial_specialization = false;
	if (peek() == "<"_tok) {
		is_partial_specialization = true;
		// Skip the template specialization arguments
		skip_template_arguments();
	}
	
	// Create DeclarationNode
	auto decl_node = emplace_node<DeclarationNode>(
		type_result.node().value(),
		var_name_token
	);
	
	// Parse initializer (required for member variable templates)
	std::optional<ASTNode> init_expr;
	if (peek() == "="_tok) {
		advance(); // consume '='
		
		// Parse the initializer expression
		auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (init_result.is_error()) {
			return init_result;
		}
		init_expr = init_result.node();
	}
	
	// Expect semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after member variable template declaration", current_token_);
	}
	
	// Create VariableDeclarationNode
	auto var_decl_node = emplace_node<VariableDeclarationNode>(
		decl_node,
		init_expr,
		storage_class
	);
	
	// Set constexpr flag if present
	var_decl_node.as<VariableDeclarationNode>().set_is_constexpr(is_constexpr);
	
	// Create TemplateVariableDeclarationNode
	auto template_var_node = emplace_node<TemplateVariableDeclarationNode>(
		std::move(template_params),
		var_decl_node
	);
	
	// Build qualified name for registration
	StringHandle qualified_name = StringTable::getOrInternStringHandle(
		StringBuilder().append(StringTable::getStringView(struct_node.name()))
		               .append("::"sv).append(var_name));
	
	// Register in template registry
	gTemplateRegistry.registerVariableTemplate(var_name_token.handle(), template_var_node);
	gTemplateRegistry.registerVariableTemplate(qualified_name, template_var_node);
	
	FLASH_LOG_FORMAT(Parser, Info, "Registered member variable template: {}", StringTable::getStringView(qualified_name));
	
	return saved_position.success();
}

// Helper: Parse member template keyword - performs lookahead to detect whether 'template' introduces
// a member template alias or member function template, then dispatches to the appropriate parser.
// This eliminates code duplication across regular struct, full specialization, and partial specialization parsing.
