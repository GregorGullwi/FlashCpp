ParseResult Parser::parse_template_function_declaration_body(
	std::vector<ASTNode>& template_params,
	std::optional<ASTNode> requires_clause,
	ASTNode& out_template_node
) {
	// Save position for template declaration re-parsing (needed for SFINAE)
	// This position is at the start of the return type, before parse_type_and_name()
	SaveHandle declaration_start = save_token_position();
	
	// Parse storage class specifiers (constexpr, inline, static, etc.)
	// This must be done BEFORE parse_type_and_name() to capture constexpr for template functions
	auto specs = parse_declaration_specifiers();
	bool is_constexpr = specs.is_constexpr();
	bool is_consteval = specs.is_consteval();
	bool is_constinit = specs.is_constinit();
	
	// Parse the function declaration (type and name)
	auto type_and_name_result = parse_type_and_name();
	if (type_and_name_result.is_error()) {
		return type_and_name_result;
	}

	// Check if parse_type_and_name already returned a FunctionDeclarationNode
	// This happens for complex declarators like: char (*func(params))[N]
	FunctionDeclarationNode* func_decl_ptr = nullptr;
	std::optional<ASTNode> func_result_node;
	
	if (type_and_name_result.node().has_value() && type_and_name_result.node()->is<FunctionDeclarationNode>()) {
		// Already have a complete function declaration
		func_result_node = type_and_name_result.node();
		func_decl_ptr = &func_result_node->as<FunctionDeclarationNode>();
	} else if (!type_and_name_result.node().has_value() || !type_and_name_result.node()->is<DeclarationNode>()) {
		return ParseResult::error("Expected declaration node for template function", peek_info());
	} else {
		// Need to parse function declaration from DeclarationNode
		DeclarationNode& decl_node = type_and_name_result.node()->as<DeclarationNode>();

		// Parse function declaration with parameters
		auto func_result = parse_function_declaration(decl_node);
		if (func_result.is_error()) {
			return func_result;
		}

		if (!func_result.node().has_value()) {
			return ParseResult::error("Failed to create function declaration node", peek_info());
		}

		func_result_node = func_result.node();
		func_decl_ptr = &func_result_node->as<FunctionDeclarationNode>();
	}

	FunctionDeclarationNode& func_decl = *func_decl_ptr;
	
	// Apply storage class specifiers to the function declaration
	func_decl.set_is_constexpr(is_constexpr);
	func_decl.set_is_consteval(is_consteval);
	func_decl.set_is_constinit(is_constinit);

	// In C++, the order after parameters is: cv-qualifiers -> ref-qualifier -> noexcept -> trailing-return-type
	// We need to skip cv-qualifiers, ref-qualifier, and noexcept BEFORE checking for trailing return type
	// Example: template<typename T> auto func(T x) const noexcept -> decltype(x + 1)
	FlashCpp::MemberQualifiers member_quals;
	skip_function_trailing_specifiers(member_quals);

	// Note: trailing requires clause is parsed below (line ~5030) and stored
	// on the TemplateFunctionDeclarationNode for constraint checking during instantiation.

	// Handle trailing return type for auto return type
	// This must be done AFTER skipping cv-qualifiers/noexcept but BEFORE semicolon/body
	// Example: template<typename T> auto func(T x) -> decltype(x + 1)
	DeclarationNode& decl_node = func_decl.decl_node();
	TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
	FLASH_LOG(Templates, Debug, "Template instantiation: pre-trailing return type: type=", static_cast<int>(return_type.type()),
	          ", index=", return_type.type_index(), ", token='", return_type.token().value(), "'");
	if (!peek().is_eof()) {
		FLASH_LOG(Templates, Debug, "Template instantiation: next token after params='", peek_info().value(), "'");
	} else {
		FLASH_LOG(Templates, Debug, "Template instantiation: no token after params");
	}
	if (return_type.type() == Type::Auto && peek() == "->"_tok) {
		// Save position of '->' for SFINAE re-parsing of trailing return type
		SaveHandle trailing_pos = save_token_position();
		func_decl.set_trailing_return_type_position(trailing_pos);
		advance();  // consume '->'
		
		// Enter a temporary scope for trailing return type parsing
		// This allows parameter names to be visible in decltype expressions
		gSymbolTable.enter_scope(ScopeType::Function);
		
		// Register function parameters so they're visible in trailing return type expressions
		// Example: auto func(T __t, U __u) -> decltype(__t + __u)
		const auto& params = func_decl.parameter_nodes();
		register_parameters_in_scope(params);
		
		ParseResult trailing_type_specifier = parse_type_specifier();
		
		// Exit the temporary scope
		gSymbolTable.exit_scope();
		
		if (trailing_type_specifier.is_error()) {
			return trailing_type_specifier;
		}
		
		// Verify we got a TypeSpecifierNode
		if (!trailing_type_specifier.node().has_value() || !trailing_type_specifier.node()->is<TypeSpecifierNode>()) {
			return ParseResult::error("Expected type specifier for trailing return type", current_token_);
		}
		
		// Apply pointer and reference qualifiers to the trailing return type (e.g., T*, T&, T&&)
		TypeSpecifierNode& trailing_ts = trailing_type_specifier.node()->as<TypeSpecifierNode>();
		consume_pointer_ref_modifiers(trailing_ts);
		
		FLASH_LOG(Templates, Debug, "Template instantiation: parsed trailing return type: type=", static_cast<int>(trailing_ts.type()),
		          ", index=", trailing_ts.type_index(), ", token='", trailing_ts.token().value(), "'");
		if (trailing_ts.type_index() < gTypeInfo.size()) {
			FLASH_LOG(Templates, Debug, "Template instantiation: trailing return gTypeInfo name='",
			          StringTable::getStringView(gTypeInfo[trailing_ts.type_index()].name()), 
			          "', underlying_type=", static_cast<int>(gTypeInfo[trailing_ts.type_index()].type_));
		}
		
		// Replace the auto type with the trailing return type
		return_type = trailing_type_specifier.node()->as<TypeSpecifierNode>();
		FLASH_LOG(Templates, Debug, "Template instantiation: updated return type from trailing clause: type=", static_cast<int>(return_type.type()),
		          ", index=", return_type.type_index());
	}

	// Check for trailing requires clause: template<typename T> T func(T x) requires constraint
	std::optional<ASTNode> trailing_requires_clause;
	if (peek() == "requires"_tok) {
		Token requires_token = peek_info();
		advance(); // consume 'requires'
		
		// Enter a temporary scope for trailing requires clause parsing
		// This allows parameter names to be visible in requires expressions
		// Example: func(T __t, U __u) requires requires { __t + __u; }
		gSymbolTable.enter_scope(ScopeType::Function);
		
		// Register function parameters so they're visible in the constraint expression
		const auto& params = func_decl.parameter_nodes();
		register_parameters_in_scope(params);
		
		// Parse the constraint expression
		auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		
		// Exit the temporary scope
		gSymbolTable.exit_scope();
		
		if (constraint_result.is_error()) {
			return constraint_result;
		}
		
		// Create RequiresClauseNode for trailing requires
		trailing_requires_clause = emplace_node<RequiresClauseNode>(
			*constraint_result.node(),
			requires_token
		);
	}
	
	// Use trailing requires clause if present, otherwise use the leading one
	std::optional<ASTNode> final_requires_clause = trailing_requires_clause.has_value() ? trailing_requires_clause : requires_clause;

	// Create a template function declaration node
	auto template_func_node = emplace_node<TemplateFunctionDeclarationNode>(
		std::move(template_params),
		*func_result_node,
		final_requires_clause
	);

	// Handle function body: semicolon (declaration only), = delete, = default, or braces (definition)
	if (peek() == ";"_tok) {
		// Just a declaration, consume the semicolon
		advance();
	} else if (peek() == "="_tok) {
		// Handle = delete or = default
		advance(); // consume '='
		if (!peek().is_eof()) {
			if (peek() == "delete"_tok) {
				advance(); // consume 'delete'
				// Mark the function as deleted so calling it produces an error
				func_decl.set_is_deleted(true);
			} else if (peek() == "default"_tok) {
				advance(); // consume 'default'
				// For defaulted template functions, the compiler generates the implementation
			} else {
				return ParseResult::error("Expected 'delete' or 'default' after '=' in function declaration", peek_info());
			}
		}
		// Expect semicolon after = delete or = default
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after '= delete' or '= default'", current_token_);
		}
	} else if (peek() == "{"_tok) {
		// Has a body - save positions for re-parsing during instantiation
		SaveHandle body_start = save_token_position();
		
		// Store both declaration and body positions for SFINAE support
		// Declaration position: for re-parsing return type with template parameters
		// Body position: for re-parsing function body with template parameters
		func_decl.set_template_declaration_position(declaration_start);
		func_decl.set_template_body_position(body_start);
		
		// Skip over the body (skip_balanced_braces consumes the '{' and everything up to the matching '}')
		skip_balanced_braces();
	}

	out_template_node = template_func_node;
	return ParseResult::success(template_func_node);
}

// Parse member function template inside a class
// Pattern: template<typename U> ReturnType functionName(U param) { ... }
ParseResult Parser::parse_member_function_template(StructDeclarationNode& struct_node, AccessSpecifier access) {
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

	auto param_list_result = parse_template_parameter_list(template_params);
	if (param_list_result.is_error()) {
		return param_list_result;
	}

	// Expect '>' to close template parameter list
	if (peek() != ">"_tok) {
		return ParseResult::error("Expected '>' after template parameter list", current_token_);
	}
	advance(); // consume '>'

	// Temporarily add template parameters to type system using RAII scope guard (Phase 3)
	FlashCpp::TemplateParameterScope template_scope;
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
			if (tparam.kind() == TemplateParameterKind::Type) {
				auto& type_info = add_user_type(tparam.nameHandle(), 0); // Do we need a correct size here?
				gTypesByName.emplace(type_info.name(), &type_info);
				template_scope.addParameter(&type_info);
			}
		}
	}

	// Set up template parameter names for the body parsing phase
	// This is needed for decltype expressions and other template-dependent constructs
	// Save current template param names and restore after body parsing
	std::vector<StringHandle> saved_template_param_names = std::move(current_template_param_names_);
	current_template_param_names_.clear();
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
			current_template_param_names_.push_back(tparam.nameHandle());
		}
	}

	// Check for requires clause after template parameters
	// Pattern: template<typename T> requires Constraint<T> ReturnType func();
	std::optional<ASTNode> requires_clause;
	if (peek() == "requires"_tok) {
		advance(); // consume 'requires'
		
		// Parse the constraint expression
		auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (constraint_result.is_error()) {
			current_template_param_names_ = std::move(saved_template_param_names);
			return constraint_result;
		}
		
		requires_clause = emplace_node<RequiresClauseNode>(
			*constraint_result.node(),
			Token(Token::Type::Keyword, "requires"sv, 0, 0, 0));
	}

	// Check for template constructor: template<typename U> StructName(params)
	// Skip any storage specifiers (constexpr, explicit, inline) and check if
	// the first non-specifier identifier matches the struct name followed by '('
	{
		SaveHandle lookahead_pos = save_token_position();
		bool found_constructor = false;
		
		// Skip declaration specifiers and 'explicit' in any order
		// Both orderings are valid: 'explicit constexpr' and 'constexpr explicit'
		parse_declaration_specifiers();
		
		// Also skip 'explicit' which is constructor-specific and not in parse_declaration_specifiers
		// C++20 explicit(condition) - also skip the condition expression
		while (peek() == "explicit"_tok) {
			advance();
			if (peek() == "("_tok) {
				skip_balanced_parens();
			}
		}
		
		// Skip any remaining declaration specifiers after 'explicit'
		// Handles 'explicit constexpr' where constexpr comes after explicit
		// (Results intentionally discarded - this is a lookahead, actual values captured below)
		parse_declaration_specifiers();
		
		// Check if next identifier is the struct name
		// Also check the base template name for template specializations
		// E.g., in template<> struct allocator<void>, the struct name is "allocator_void"
		// but the constructor is still named "allocator"
		bool is_base_template_ctor = false;
		if (!peek().is_eof() && peek().is_identifier() &&
		    peek_info().value() != struct_node.name()) {
			auto type_it = gTypesByName.find(struct_node.name());
			if (type_it != gTypesByName.end() && type_it->second->isTemplateInstantiation()) {
				std::string_view base_name = StringTable::getStringView(type_it->second->baseTemplateName());
				if (peek_info().value() == base_name) {
					is_base_template_ctor = true;
				}
			}
		}
		if (!peek().is_eof() && peek().is_identifier() &&
		    (peek_info().value() == struct_node.name() || is_base_template_ctor)) {
			[[maybe_unused]] Token name_token = peek_info();
			advance();
			
			// Check if followed by '('
			if (peek() == "("_tok) {
				found_constructor = true;
				
				// Restore to parse constructor properly
				restore_token_position(lookahead_pos);
				
				// Parse declaration specifiers again to get to constructor name
				auto specs = parse_declaration_specifiers();
				
				// Track 'explicit' separately (constructor-specific, not in DeclarationSpecifiers)
				// C++20 explicit(condition) - also skip the condition expression
				bool is_explicit = false;
				while (peek() == "explicit"_tok) {
					is_explicit = true;
					advance();
					if (peek() == "("_tok) {
						skip_balanced_parens();
					}
				}
				
				// Parse any remaining declaration specifiers after 'explicit'
				// Handles 'explicit constexpr' where constexpr comes after explicit
				{
					auto more_specs = parse_declaration_specifiers();
					if (more_specs.constexpr_spec != FlashCpp::ConstexprSpecifier::None)
						specs.constexpr_spec = more_specs.constexpr_spec;
					if (more_specs.is_inline) specs.is_inline = true;
				}
				
				// Now at the constructor name - consume it
				Token ctor_name_token = peek_info();
				advance();
				
				// Cache struct name handle for use throughout this scope
				StringHandle struct_name_handle = struct_node.name();
				
				FLASH_LOG_FORMAT(Parser, Debug, "parse_member_function_template: Detected template constructor {}()", 
				                 StringTable::getStringView(struct_name_handle));
				
				// Create constructor declaration
				auto [ctor_node, ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
					struct_name_handle, ctor_name_token.handle());
				
				// Apply specifiers to constructor
				ctor_ref.set_explicit(is_explicit);
				ctor_ref.set_constexpr(specs.is_constexpr());
				
				// Parse parameters
				FlashCpp::ParsedParameterList params;
				auto param_result = parse_parameter_list(params);
				if (param_result.is_error()) {
					current_template_param_names_ = std::move(saved_template_param_names);
					return param_result;
				}
				
				// Apply parsed parameters to the constructor
				for (const auto& param : params.parameters) {
					ctor_ref.add_parameter_node(param);
				}
				
				// Enter scope for initializer list parsing
				FlashCpp::SymbolTableScope ctor_scope(ScopeType::Function);
				
				// Add parameters to symbol table
				for (const auto& param : ctor_ref.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& param_decl_node = param.as<DeclarationNode>();
						const Token& param_token = param_decl_node.identifier_token();
						gSymbolTable.insert(param_token.value(), param);
					}
				}
				
				// Parse noexcept specifier if present
				if (parse_constructor_exception_specifier()) {
					ctor_ref.set_noexcept(true);
				}
				
				// Parse trailing requires clause if present and store on constructor
				if (auto req = parse_trailing_requires_clause()) {
					ctor_ref.set_requires_clause(*req);
				}
				
				// Skip GCC __attribute__ between specifiers and initializer list
				skip_gcc_attributes();
				
				// Parse member initializer list if present
				if (peek() == ":"_tok) {
					advance(); // consume ':'
					
					// Parse each initializer
					do {
						if (!peek().is_identifier()) {
							current_template_param_names_ = std::move(saved_template_param_names);
							return ParseResult::error("Expected member name in initializer list", peek_info());
						}
						
						advance();
						
						// Check for template arguments: Base<T>(...)
						if (peek() == "<"_tok) {
							skip_template_arguments();
						}
						
						// Expect '(' or '{'
						bool is_paren = peek() == "("_tok;
						bool is_brace = peek() == "{"_tok;
						if (!is_paren && !is_brace) {
							current_template_param_names_ = std::move(saved_template_param_names);
							return ParseResult::error("Expected '(' or '{' after initializer name", peek_info());
						}
						
						// Skip balanced delimiters - we don't need to parse the expressions for template patterns
						if (is_paren) {
							skip_balanced_parens();
						} else {
							skip_balanced_braces();
						}
						
					} while (consume(","_tok));
				}
				
				// Handle = default, = delete, body, or semicolon
				if (peek() == "="_tok) {
					advance(); // consume '='
					if (peek() == "default"_tok) {
						advance();
						ctor_ref.set_is_implicit(true);
						auto [block_node, block_ref] = create_node_ref(BlockNode());
						ctor_ref.set_definition(block_node);
					} else if (peek() == "delete"_tok) {
						advance();
						// Don't add deleted constructors
						if (!consume(";"_tok)) {
							current_template_param_names_ = std::move(saved_template_param_names);
							return ParseResult::error("Expected ';' after '= delete'", peek_info());
						}
						current_template_param_names_ = std::move(saved_template_param_names);
						return saved_position.success();
					}
					if (!consume(";"_tok)) {
						current_template_param_names_ = std::move(saved_template_param_names);
						return ParseResult::error("Expected ';' after '= default' or '= delete'", peek_info());
					}
				} else if (peek() == "{"_tok) {
					// DELAYED PARSING: Save the current position (start of '{')
					// This allows member variables declared later in the class to be visible
					SaveHandle body_start = save_token_position();
					
					// Look up the struct type
					auto type_it = gTypesByName.find(struct_name_handle);
					size_t struct_type_index = 0;
					if (type_it != gTypesByName.end()) {
						struct_type_index = type_it->second->type_index_;
					}
					
					// Skip over the constructor body by counting braces
					skip_balanced_braces();
					
					// Extract template parameter names for use during delayed body parsing
					std::vector<StringHandle> template_param_name_handles;
					for (const auto& param : template_params) {
						if (param.is<TemplateParameterNode>()) {
							template_param_name_handles.push_back(param.as<TemplateParameterNode>().nameHandle());
						}
					}
					
					FLASH_LOG_FORMAT(Parser, Debug, "Deferring template constructor body parsing for struct='{}', param_count={}", 
						StringTable::getStringView(struct_name_handle), template_param_name_handles.size());
					
					// Record this for delayed parsing (with template parameters)
					delayed_function_bodies_.push_back({
						nullptr,  // func_node (not used for constructors)
						body_start,
						SaveHandle{},  // No initializer list position saved (already parsed)
						struct_name_handle,
						struct_type_index,
						&struct_node,
						false,     // has_initializer_list - already handled above  
						true,  // is_constructor
						false,  // is_destructor
						&ctor_ref,  // ctor_node
						nullptr,   // dtor_node
						template_param_name_handles,  // template_param_names for template constructors
						true   // is_member_function_template
					});
				} else if (!consume(";"_tok)) {
					current_template_param_names_ = std::move(saved_template_param_names);
					return ParseResult::error("Expected '{', ';', '= default', or '= delete' after constructor declaration", peek_info());
				}
				
				// Add constructor to struct
				struct_node.add_constructor(ctor_node, access);
				
				// Restore template param names
				current_template_param_names_ = std::move(saved_template_param_names);
				
				return saved_position.success();
			}
		}
		
		// Not a constructor, restore and continue with function parsing
		if (!found_constructor) {
			restore_token_position(lookahead_pos);
		}
	}

	// Check for template conversion operator: template<typename T> operator T() const noexcept
	// Conversion operators don't have a return type, so parse_type_and_name() fails.
	// We need to detect and handle them before calling parse_template_function_declaration_body().
	{
		SaveHandle conv_lookahead = save_token_position();
		bool found_conversion_op = false;

		// Skip declaration specifiers (constexpr, explicit, inline, etc.)
		parse_declaration_specifiers();
		// Also skip 'explicit' / 'explicit(condition)'
		while (peek() == "explicit"_tok) {
			advance();
			if (peek() == "("_tok) {
				skip_balanced_parens();
			}
		}

		if (peek() == "operator"_tok) {
			// Check if this is a conversion operator (not operator() or operator<< etc.)
			SaveHandle op_saved = save_token_position();
			Token operator_keyword_token = peek_info();
			advance(); // consume 'operator'

			// If next token is not '(' and not an operator symbol, it's likely a conversion operator
			if (peek() != "("_tok &&
			    !peek().is_operator() &&
			    peek() != "["_tok && peek() != "new"_tok && peek() != "delete"_tok) {
				auto type_result = parse_type_specifier();
				if (!type_result.is_error() && type_result.node().has_value()) {
					// Apply pointer/reference qualifiers on conversion target type (ptr-operator in C++20 grammar)
					TypeSpecifierNode& conv_target_type = type_result.node()->as<TypeSpecifierNode>();
					consume_pointer_ref_modifiers(conv_target_type);
					if (peek() == "("_tok) {
						found_conversion_op = true;

						const TypeSpecifierNode& target_type = type_result.node()->as<TypeSpecifierNode>();
						StringBuilder op_name_builder;
						op_name_builder.append("operator ");
						op_name_builder.append(target_type.getReadableString());
						std::string_view operator_name = op_name_builder.commit();

						Token identifier_token = Token(Token::Type::Identifier, operator_name,
						                              operator_keyword_token.line(), operator_keyword_token.column(),
						                              operator_keyword_token.file_index());

						// Create a declaration node with the return type being the target type
						ASTNode decl_node = emplace_node<DeclarationNode>(
							type_result.node().value(), identifier_token);

						discard_saved_token(op_saved);
						discard_saved_token(conv_lookahead);

						// Parse parameter list (should be empty for conversion operators)
						FlashCpp::ParsedParameterList params;
						auto param_result = parse_parameter_list(params);
						if (param_result.is_error()) {
							current_template_param_names_ = std::move(saved_template_param_names);
							return param_result;
						}

						// Create a function declaration for the conversion operator
						auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(
							decl_node.as<DeclarationNode>(), identifier_token.value());
						for (const auto& param : params.parameters) {
							func_ref.add_parameter_node(param);
						}

						// Skip trailing specifiers (const, noexcept, etc.)
						FlashCpp::MemberQualifiers member_quals;
						skip_function_trailing_specifiers(member_quals);
						skip_trailing_requires_clause();

						// Create template function declaration node
						auto template_func_node = emplace_node<TemplateFunctionDeclarationNode>(
							std::move(template_params),
							func_node,
							requires_clause
						);

						// Handle body: = default, = delete, { body }, or ;
						if (peek() == "{"_tok) {
							SaveHandle body_start = save_token_position();
							func_ref.set_template_body_position(body_start);
							skip_balanced_braces();
						} else if (peek() == "="_tok) {
							advance(); // consume '='
							if (peek() == "delete"_tok) {
								advance(); // consume 'delete'
								// Deleted template conversion operators are registered but
								// will be rejected if instantiation is attempted
							} else if (peek() == "default"_tok) {
								advance(); // consume 'default'
								// Defaulted template conversion operators get compiler-generated impl
								func_ref.set_is_implicit(true);
								auto [block_node, block_ref] = create_node_ref(BlockNode());
								func_ref.set_definition(block_node);
							}
							consume(";"_tok);
						} else {
							consume(";"_tok);
						}

						// Register as a member function template on the struct
						struct_node.add_member_function(template_func_node, access,
						                                false, false, false, false,
						                                member_quals.is_const(), member_quals.is_volatile());

						auto qualified_name = StringTable::getOrInternStringHandle(
							StringBuilder().append(struct_node.name()).append("::"sv).append(operator_name));
						gTemplateRegistry.registerTemplate(qualified_name, template_func_node);
						gTemplateRegistry.registerTemplate(StringTable::getOrInternStringHandle(operator_name), template_func_node);

						current_template_param_names_ = std::move(saved_template_param_names);
						return saved_position.success();
					}
				}
			}
			if (!found_conversion_op) {
				restore_token_position(op_saved);
			}
		}

		if (!found_conversion_op) {
			restore_token_position(conv_lookahead);
		}
	}

	// Use shared helper to parse function declaration body (Phase 6)
	ASTNode template_func_node;
	auto body_result = parse_template_function_declaration_body(template_params, requires_clause, template_func_node);
	
	// Restore template param names
	current_template_param_names_ = std::move(saved_template_param_names);
	
	if (body_result.is_error()) {
		return body_result;  // template_scope automatically cleans up
	}

	// Get the function name for registration
	const TemplateFunctionDeclarationNode& template_decl = template_func_node.as<TemplateFunctionDeclarationNode>();
	const FunctionDeclarationNode& func_decl = template_decl.function_declaration().as<FunctionDeclarationNode>();
	const DeclarationNode& decl_node = func_decl.decl_node();

	// Add to struct as a member function template
	// First, add to the struct's member functions list so it can be found for inheritance lookup
	struct_node.add_member_function(template_func_node, access);
	
	// Register the template in the global registry with qualified name (ClassName::functionName)
	auto qualified_name = StringTable::getOrInternStringHandle(StringBuilder().append(struct_node.name()).append("::"sv).append(decl_node.identifier_token().value()));
	gTemplateRegistry.registerTemplate(qualified_name, template_func_node);
	
	// Also register with simple name for unqualified lookups (needed for inherited member template function calls)
	gTemplateRegistry.registerTemplate(decl_node.identifier_token().handle(), template_func_node);

	// template_scope automatically cleans up template parameters when it goes out of scope

	return saved_position.success();
}

// Parse member template alias: template<typename T, typename U> using type = T;
ParseResult Parser::parse_member_template_or_function(StructDeclarationNode& struct_node, AccessSpecifier access) {
	// Look ahead to determine if this is a template alias, struct/class template, friend, or function template
	SaveHandle lookahead_pos = save_token_position();
	
	advance(); // consume 'template'
	
	// Skip template parameter list to find what comes after
	bool is_template_alias = false;
	bool is_struct_or_class_template = false;
	bool is_template_friend = false;
	bool is_variable_template = false;
	if (peek() == "<"_tok) {
		advance(); // consume '<'
		
		// Skip template parameters by counting angle brackets
		// Handle >> token for nested templates (C++20 maximal munch)
		int angle_bracket_depth = 1;
		while (angle_bracket_depth > 0 && !peek().is_eof()) {
			if (peek() == "<"_tok) {
				angle_bracket_depth++;
			} else if (peek() == ">"_tok) {
				angle_bracket_depth--;
			} else if (peek() == ">>"_tok) {
				// >> is two > tokens for nested templates
				angle_bracket_depth -= 2;
			}
			advance();
		}
		
		// Now check what comes after the template parameters
		// Handle requires clause: template<typename T> requires Constraint using Alias = T;
		if (peek() == "requires"_tok) {
			advance(); // consume 'requires'
			
			// Skip the constraint expression by counting balanced brackets/parens
			// The constraint expression ends before 'using', 'struct', 'class', 'friend', or a type specifier
			int paren_depth = 0;
			int angle_depth = 0;
			int brace_depth = 0;
			while (!peek().is_eof()) {
				auto tk = peek();
				
				// Track nested brackets
				if (tk == "("_tok) paren_depth++;
				else if (tk == ")"_tok) paren_depth--;
				else if (tk == "{"_tok) brace_depth++;
				else if (tk == "}"_tok) brace_depth--;
				else update_angle_depth(tk, angle_depth);
				
				// At top level, check for the actual declaration keyword
				if (paren_depth == 0 && angle_depth == 0 && brace_depth == 0) {
					if (peek().is_keyword()) {
						if (tk == "using"_tok || tk == "struct"_tok || tk == "class"_tok || tk == "friend"_tok) {
							break;
						}
						// Common function specifiers that indicate we've reached the declaration
						if (tk == "constexpr"_tok || tk == "static"_tok || tk == "inline"_tok || 
						    tk == "virtual"_tok || tk == "explicit"_tok || tk == "const"_tok || tk == "volatile"_tok) {
							break;
						}
					}
					// Type specifiers (identifiers not in constraint) indicate end of requires clause
					// BUT only if the identifier is NOT followed by '<' (which would indicate a template)
					// or '::' (which would indicate a qualified name like __detail::A<_Iter>)
					else if (peek().is_identifier()) {
						// Peek ahead to see if this is a template instantiation (part of constraint)
						// or a qualified name (namespace::concept)
						// Save position, check next token, then restore
						SaveHandle id_check_pos = save_token_position();
						advance(); // consume the identifier
						bool is_constraint_part = !peek().is_eof() && 
						                          (peek() == "<"_tok || peek() == "::"_tok);
						restore_token_position(id_check_pos);
						
						if (!is_constraint_part) {
							// This identifier is followed by something other than '<' or '::'
							// It's likely the start of the declaration (a type), not part of the constraint
							break;
						}
						// Otherwise, it's a template like is_reference_v<T> or qualified name - continue skipping
					}
				}
				
				advance();
			}
		}
		
		FLASH_LOG_FORMAT(Parser, Debug, "parse_member_template_or_function: After skipping template params, peek={}", 
		    !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
		
		if (peek().is_keyword()) {
			auto next_kw = peek();
			FLASH_LOG_FORMAT(Parser, Debug, "parse_member_template_or_function: Detected keyword '{}'", peek_info().value());
			if (next_kw == "using"_tok) {
				is_template_alias = true;
			} else if (next_kw == "struct"_tok || next_kw == "class"_tok || next_kw == "union"_tok) {
				is_struct_or_class_template = true;
			} else if (next_kw == "friend"_tok) {
				is_template_friend = true;
				FLASH_LOG(Parser, Debug, "parse_member_template_or_function: is_template_friend = true");
			} else if (next_kw == "static"_tok || next_kw == "constexpr"_tok || next_kw == "inline"_tok) {
				// Could be a member variable template: template<...> static constexpr bool name = ...;
				// Need to look ahead further to see if it has '=' before '(' 
				// Skip specifiers and type, find if name is followed by '=' (variable) or '(' (function)
				// NOTE: Must not confuse operator= with variable initialization
				SaveHandle var_check_pos = save_token_position();
				int angle_depth_inner = 0;
				bool found_equals = false;
				bool found_paren = false;
				bool found_operator_keyword = false;
				
				// Skip up to 20 tokens looking for '=' or '(' at depth 0
				for (int i = 0; i < 20 && !peek().is_eof() && !found_equals && !found_paren; ++i) {
					auto tok = peek();
					
					// Check for 'operator' keyword - next '=' would be part of operator name, not initializer
					if (tok == "operator"_tok) {
						found_operator_keyword = true;
						// Skip past operator and the operator symbol
						advance(); // consume 'operator'
						// The next token(s) are the operator name (=, ==, +=, etc.)
						// For operator=, we'll see '=' next but it's not an initializer
						if (!peek().is_eof()) {
							advance(); // consume operator symbol
							// If it was '==', '<<=', etc., we consumed two parts already
							// Now continue looking for the opening paren
							continue;
						}
					}

					update_angle_depth(tok, angle_depth_inner);
					
					if (angle_depth_inner == 0) {
						if (tok == "="_tok && !found_operator_keyword) {
							// Only treat as variable initializer if we haven't seen 'operator'
							found_equals = true;
						} else if (tok == "("_tok) {
							found_paren = true;
						} else if (tok == ";"_tok) {
							// End of declaration without finding either - could be forward decl
							break;
						}
					}
					advance();
				}
				
				restore_token_position(var_check_pos);
				
				if (found_equals && !found_paren && !found_operator_keyword) {
					is_variable_template = true;
					FLASH_LOG(Parser, Debug, "parse_member_template_or_function: Detected member variable template");
				}
			}
		}
	}
	
	// Restore position before calling the appropriate parser
	restore_token_position(lookahead_pos);
	
	if (is_template_alias) {
		// This is a member template alias
		return parse_member_template_alias(struct_node, access);
	} else if (is_struct_or_class_template) {
		// This is a member struct/class template
		return parse_member_struct_template(struct_node, access);
	} else if (is_template_friend) {
		// This is a template friend declaration
		return parse_template_friend_declaration(struct_node);
	} else if (is_variable_template) {
		// This is a member variable template: template<...> static constexpr Type var = ...;
		return parse_member_variable_template(struct_node, access);
	} else {
		// This is a member function template
		return parse_member_function_template(struct_node, access);
	}
}

// Evaluate constant expressions for template arguments
// Handles cases like is_int<T>::value where T is substituted
// Returns pair of (value, type) if successful, nullopt otherwise
std::optional<Parser::ConstantValue> Parser::try_evaluate_constant_expression(const ASTNode& expr_node) {
	if (!expr_node.is<ExpressionNode>()) {
		FLASH_LOG(Templates, Debug, "Not an ExpressionNode");
		return std::nullopt;
	}
	
	const ExpressionNode& expr = expr_node.as<ExpressionNode>();
	
	// Log what variant we have
	FLASH_LOG_FORMAT(Templates, Debug, "Expression variant index: {}", expr.index());
	
	// Handle boolean literals directly
	if (std::holds_alternative<BoolLiteralNode>(expr)) {
		const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr);
		return ConstantValue{lit.value() ? 1 : 0, Type::Bool};
	}
	
	// Handle numeric literals directly
	if (std::holds_alternative<NumericLiteralNode>(expr)) {
		const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
		const auto& val = lit.value();
		if (std::holds_alternative<unsigned long long>(val)) {
			return ConstantValue{static_cast<int64_t>(std::get<unsigned long long>(val)), lit.type()};
		} else if (std::holds_alternative<double>(val)) {
			return ConstantValue{static_cast<int64_t>(std::get<double>(val)), lit.type()};
		}
	}
	
	// Handle qualified identifier expressions (e.g., is_int<double>::value)
	// This is the most common case for template member access in C++
	if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
		const QualifiedIdentifierNode& qualified_id = std::get<QualifiedIdentifierNode>(expr);
		
		// The qualified identifier represents something like "is_int<double>::value"
		// We need to extract: type_name = "is_int<double>" and member_name = "value"
		// The full_name() gives us the complete qualified name
		std::string full_qualified_name = qualified_id.full_name();
		
		// Find the last :: to split type name from member name
		size_t last_scope_pos = full_qualified_name.rfind("::");
		if (last_scope_pos == std::string::npos) {
			FLASH_LOG_FORMAT(Templates, Debug, "Qualified identifier '{}' has no scope separator", full_qualified_name);
			return std::nullopt;
		}
		
		std::string_view type_name(full_qualified_name.data(), last_scope_pos);
		std::string_view member_name(full_qualified_name.data() + last_scope_pos + 2, 
		                              full_qualified_name.size() - last_scope_pos - 2);
		
		FLASH_LOG_FORMAT(Templates, Debug, "Evaluating constant expression: {}::{}", type_name, member_name);
		
		// Look up the type - it should be an instantiated template class
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(type_name));
		if (type_it == gTypesByName.end()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Type {} not found in type system, attempting to instantiate as template", type_name);
			
			// Try to parse the type name as a template instantiation (e.g., "Num<int>")
			// Extract template name and arguments
			size_t template_start = type_name.find('<');
			if (template_start != std::string_view::npos && type_name.back() == '>') {
				std::string_view template_name = type_name.substr(0, template_start);
				// For now, we'll try to instantiate with the args as a string
				// This is a simplified approach - proper parsing would be better
				// but since we're in constant evaluation, the template should have been
				// instantiated already if it's used correctly
				
				// Check if this is a known template
				auto template_entry = gTemplateRegistry.lookupTemplate(template_name);
				if (template_entry.has_value()) {
					FLASH_LOG_FORMAT(Templates, Debug, "Found template '{}', but instantiation failed or incomplete", template_name);
				}
			}
			
			FLASH_LOG_FORMAT(Templates, Debug, "Type {} not found even after instantiation attempt", type_name);
			return std::nullopt;
		}
		
		const TypeInfo* type_info = type_it->second;
		if (!type_info->isStruct()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Type {} is not a struct", type_name);
			return std::nullopt;
		}
		
		const StructTypeInfo* struct_info = type_info->getStructInfo();
		if (!struct_info) {
			FLASH_LOG(Templates, Debug, "Could not get struct info");
			return std::nullopt;
		}
		
		// Trigger lazy static member instantiation if needed
		StringHandle type_name_handle = StringTable::getOrInternStringHandle(type_name);
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
		instantiateLazyStaticMember(type_name_handle, member_name_handle);
		
		// Look for the static member with the given name (may have just been lazily instantiated)
		// Use findStaticMemberRecursive to also search base classes
		auto [static_member, owner_struct] = struct_info->findStaticMemberRecursive(member_name_handle);
		if (!static_member) {
			FLASH_LOG_FORMAT(Templates, Debug, "Static member {} not found in {}", member_name, type_name);
			return std::nullopt;
		}
		
		// If the static member was found in a base class, trigger lazy instantiation for that base class too
		if (owner_struct != struct_info) {
			FLASH_LOG(Templates, Debug, "Static member '", member_name, "' found in base class '", 
			          StringTable::getStringView(owner_struct->name), "', triggering lazy instantiation");
			instantiateLazyStaticMember(owner_struct->name, member_name_handle);
			// Re-fetch the static member after lazy instantiation
			auto [updated_static_member, updated_owner] = owner_struct->findStaticMemberRecursive(member_name_handle);
			static_member = updated_static_member;
			if (!static_member) {
				FLASH_LOG_FORMAT(Templates, Debug, "Static member {} not found after lazy instantiation", member_name);
				return std::nullopt;
			}
		}
		
		// Check if it has an initializer
		if (!static_member->initializer.has_value()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Static member {}::{} has no initializer", type_name, member_name);
			return std::nullopt;
		}
		
		// Evaluate the initializer - it should be a constant expression
		// For type traits, this is typically a bool literal (true/false)
		const ASTNode& init_node = *static_member->initializer;
		
		// Recursively evaluate the initializer
		return try_evaluate_constant_expression(init_node);
	}
	
	// Handle member access expressions (e.g., obj.member or obj->member)
	// Less common for template constant expressions but included for completeness
	if (std::holds_alternative<MemberAccessNode>(expr)) {
		const MemberAccessNode& member_access = std::get<MemberAccessNode>(expr);
		std::string_view member_name = member_access.member_name();
		
		// The object should be an identifier representing the template instance
		// For example, in "is_int<double>::value", the object is is_int<double>
		const ASTNode& object = member_access.object();
		if (!object.is<ExpressionNode>()) {
			return std::nullopt;
		}
		
		const ExpressionNode& obj_expr = object.as<ExpressionNode>();
		if (!std::holds_alternative<IdentifierNode>(obj_expr)) {
			return std::nullopt;
		}
		
		const IdentifierNode& id_node = std::get<IdentifierNode>(obj_expr);
		std::string_view type_name = id_node.name();
		
		FLASH_LOG_FORMAT(Templates, Debug, "Evaluating constant expression: {}::{}", type_name, member_name);
		
		// Look up the type - it should be an instantiated template class
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(type_name));
		if (type_it == gTypesByName.end()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Type {} not found in type system", type_name);
			return std::nullopt;
		}
		
		const TypeInfo* type_info = type_it->second;
		if (!type_info->isStruct()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Type {} is not a struct", type_name);
			return std::nullopt;
		}
		
		const StructTypeInfo* struct_info = type_info->getStructInfo();
		if (!struct_info) {
			FLASH_LOG(Templates, Debug, "Could not get struct info");
			return std::nullopt;
		}
		
		// Trigger lazy static member instantiation if needed
		StringHandle type_name_handle2 = StringTable::getOrInternStringHandle(type_name);
		StringHandle member_name_handle2 = StringTable::getOrInternStringHandle(member_name);
		instantiateLazyStaticMember(type_name_handle2, member_name_handle2);
		
		// Look for the static member with the given name (may have just been lazily instantiated)
		const StructStaticMember* static_member = struct_info->findStaticMember(member_name_handle2);
		if (!static_member) {
			FLASH_LOG_FORMAT(Templates, Debug, "Static member {} not found in {}", member_name, type_name);
			return std::nullopt;
		}
		
		// Check if it has an initializer
		if (!static_member->initializer.has_value()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Static member {}::{} has no initializer", type_name, member_name);
			return std::nullopt;
		}
		
		// Evaluate the initializer - it should be a constant expression
		// For type traits, this is typically a bool literal (true/false)
		const ASTNode& init_node = *static_member->initializer;
		
		// Recursively evaluate the initializer
		return try_evaluate_constant_expression(init_node);
	}
	
	// Handle type trait expressions (e.g., __has_trivial_destructor(T), __is_class(T))
	// These are compile-time boolean expressions used in template metaprogramming
	// Uses shared evaluateTypeTrait() from TypeTraitEvaluator.h for consistency with CodeGen.h
	if (std::holds_alternative<TypeTraitExprNode>(expr)) {
		const TypeTraitExprNode& trait_expr = std::get<TypeTraitExprNode>(expr);
		
		// Get the type(s) this trait is being applied to
		if (!trait_expr.has_type()) {
			// No-argument traits like __is_constant_evaluated
			if (trait_expr.kind() == TypeTraitKind::IsConstantEvaluated) {
				// We're evaluating in a constant context, so return true
				return ConstantValue{1, Type::Bool};
			}
			return std::nullopt;
		}
		
		const TypeSpecifierNode& type_spec = trait_expr.type_node().as<TypeSpecifierNode>();
		TypeIndex type_idx = type_spec.type_index();
		
		FLASH_LOG_FORMAT(Templates, Debug, "Evaluating type trait {} on type index {} (base_type={})", 
			static_cast<int>(trait_expr.kind()), type_idx, static_cast<int>(type_spec.type()));
		
		// Get TypeInfo and StructTypeInfo for the type
		const TypeInfo* type_info = (type_idx < gTypeInfo.size()) ? &gTypeInfo[type_idx] : nullptr;
		const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
		
		// Use shared evaluation function from TypeTraitEvaluator.h (overload that takes TypeSpecifierNode)
		TypeTraitResult eval_result = evaluateTypeTrait(trait_expr.kind(), type_spec, type_info, struct_info);
		
		if (!eval_result.success) {
			// Trait requires special handling (binary trait, etc.) or is not supported
			FLASH_LOG_FORMAT(Templates, Debug, "Type trait {} requires special handling or is not supported", 
				static_cast<int>(trait_expr.kind()));
			return std::nullopt;
		}
		
		FLASH_LOG_FORMAT(Templates, Debug, "Type trait evaluation result: {}", eval_result.value);
		return ConstantValue{eval_result.value ? 1 : 0, Type::Bool};
	}
	
	// Handle ternary operator expressions (e.g., (5 < 0) ? -1 : 1)
	// Use the ConstExprEvaluator for complex expression evaluation
	if (std::holds_alternative<TernaryOperatorNode>(expr)) {
		FLASH_LOG(Templates, Debug, "Evaluating ternary operator expression");
		ConstExpr::EvaluationContext ctx(gSymbolTable);
		// Set struct context for static member lookup
		if (!struct_parsing_context_stack_.empty()) {
			const auto& struct_ctx = struct_parsing_context_stack_.back();
			ctx.struct_node = struct_ctx.struct_node;
			ctx.struct_info = struct_ctx.local_struct_info;
		}
		// Enable on-demand template instantiation for expressions like (_Pn < 0) ? -1 : 1
		ctx.parser = this;
		auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);
		if (eval_result.success()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Ternary evaluated to: {}", eval_result.as_int());
			return ConstantValue{eval_result.as_int(), Type::Int};
		}
		FLASH_LOG(Templates, Debug, "Failed to evaluate ternary operator");
		return std::nullopt;
	}
	
	// Handle binary operator expressions (e.g., 5 < 0, 1 + 2)
	if (std::holds_alternative<BinaryOperatorNode>(expr)) {
		FLASH_LOG(Templates, Debug, "Evaluating binary operator expression");
		ConstExpr::EvaluationContext ctx(gSymbolTable);
		// Set struct context for static member lookup (fixes __d2 = 10 / __g where __g is a static member)
		if (!struct_parsing_context_stack_.empty()) {
			const auto& struct_ctx = struct_parsing_context_stack_.back();
			ctx.struct_node = struct_ctx.struct_node;
			ctx.struct_info = struct_ctx.local_struct_info;
		}
		// Enable on-demand template instantiation for expressions like _R1::num == _R2::num
		ctx.parser = this;
		auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);
		if (eval_result.success()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Binary op evaluated to: {}", eval_result.as_int());
			return ConstantValue{eval_result.as_int(), Type::Int};
		}
		FLASH_LOG(Templates, Debug, "Failed to evaluate binary operator");
		return std::nullopt;
	}
	
	// Handle unary operator expressions (e.g., -5, ~0, !true)
	if (std::holds_alternative<UnaryOperatorNode>(expr)) {
		FLASH_LOG(Templates, Debug, "Evaluating unary operator expression");
		ConstExpr::EvaluationContext ctx(gSymbolTable);
		// Set struct context for static member lookup
		if (!struct_parsing_context_stack_.empty()) {
			const auto& struct_ctx = struct_parsing_context_stack_.back();
			ctx.struct_node = struct_ctx.struct_node;
			ctx.struct_info = struct_ctx.local_struct_info;
		}
		// Enable on-demand template instantiation for expressions like -Num<T>::num
		ctx.parser = this;
		auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);
		if (eval_result.success()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Unary op evaluated to: {}", eval_result.as_int());
			return ConstantValue{eval_result.as_int(), Type::Int};
		}
		FLASH_LOG(Templates, Debug, "Failed to evaluate unary operator");
		return std::nullopt;
	}
	
	return std::nullopt;
}

// Parse explicit template arguments: <int, float, ...>
// Returns a vector of types if successful, nullopt otherwise
