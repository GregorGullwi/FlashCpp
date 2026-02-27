std::optional<bool> Parser::try_parse_out_of_line_template_member(
	const std::vector<ASTNode>& template_params,
	const std::vector<StringHandle>& template_param_names,
	const std::vector<ASTNode>& inner_template_params,
	const std::vector<StringHandle>& inner_template_param_names) {

	// Save position in case this isn't an out-of-line definition
	SaveHandle saved_pos = save_token_position();

	// Check for out-of-line constructor/destructor pattern first:
	// ClassName<Args>::ClassName(...)  (constructor)
	// ClassName<Args>::~ClassName()    (destructor)
	// ns::ClassName<Args>::ClassName(...)  (namespace-qualified constructor)
	// parse_type_specifier would consume the full qualified name as a type, so detect this early
	if (peek().is_identifier()) {
		SaveHandle ctor_check = save_token_position();
		Token potential_class = peek_info();
		advance(); // consume first name (could be namespace or class name)

		// Skip namespace qualifiers: ns1::ns2::ClassName<Args>::ClassName(...)
		// Keep advancing past identifier::identifier until we find identifier< or identifier::~
		while (peek() == "::"_tok && !peek().is_eof()) {
			// Look ahead to see if this is namespace::name or class::ctor pattern
			SaveHandle ns_check = save_token_position();
			advance(); // consume '::'
			bool is_dtor_check = false;
			if (peek_info().value() == "~") {
				is_dtor_check = true;
			}
			if (!is_dtor_check && peek().is_identifier()) {
				Token next_name = peek_info();
				advance(); // consume name
				if (peek() == "<"_tok || peek() == "::"_tok) {
					// This name is either a class (followed by <Args>) or another namespace (followed by ::)
					// Update potential_class and continue
					potential_class = next_name;
					if (peek() == "<"_tok) {
						skip_template_arguments();
					}
					discard_saved_token(ns_check);
					continue;
				} else if (peek() == "("_tok && next_name.value() == potential_class.value()) {
					// Found ClassName::ClassName( pattern without template args
					restore_token_position(ns_check);
					break;
				}
				// Unexpected pattern - restore and break
				restore_token_position(ns_check);
				break;
			}
			// Found :: followed by ~ or non-identifier - restore and let main logic handle it
			restore_token_position(ns_check);
			break;
		}

		// Handle both ClassName<Args>::ClassName(...) and ClassName::ClassName(...)
		if (peek() == "<"_tok) {
			skip_template_arguments();
		}
		if (peek() == "::"_tok) {
			advance(); // consume '::'
			bool is_dtor = false;
			if (peek_info().value() == "~") {
				advance(); // consume '~'
				is_dtor = true;
			}
			// Handle nested class member function: ClassName<Args>::NestedClass::ctor/dtor/func(...)
			// E.g., basic_ostream<_CharT, _Traits>::sentry::sentry(...)
			//        basic_ostream<_CharT, _Traits>::sentry::~sentry()
			if (!is_dtor && peek().is_identifier() && peek_info().value() != potential_class.value()) {
				SaveHandle nested_check = save_token_position();
				Token nested_class_token = peek_info();
				advance(); // consume nested class name
				if (peek() == "::"_tok) {
					advance(); // consume '::'
					bool is_nested_dtor = false;
					if (peek_info().value() == "~") {
						advance(); // consume '~'
						is_nested_dtor = true;
					}
					if (peek().is_identifier()) {
						advance(); // consume function name
						if (peek() == "("_tok) {
							// Out-of-line nested class member function definition
							// Skip the entire definition (params, body, etc.)
							discard_saved_token(nested_check);
							discard_saved_token(ctor_check);
							FLASH_LOG_FORMAT(Templates, Debug,
								"Skipping out-of-line nested class member function: {}::{}::{}",
								potential_class.value(), nested_class_token.value(),
								(is_nested_dtor ? "~dtor" : "ctor/func"));
							skip_balanced_parens();
							FlashCpp::MemberQualifiers nested_quals;
							skip_function_trailing_specifiers(nested_quals);
							// Skip member initializer list
							if (peek() == ":"_tok) {
								advance();
								while (!peek().is_eof() && peek() != ";"_tok) {
									if (peek() == "("_tok) skip_balanced_parens();
									else if (peek() == "{"_tok) {
										skip_balanced_braces();
										if (peek() != ","_tok) break;
									} else advance();
								}
							}
							if (peek() == "{"_tok) skip_balanced_braces();
							else if (peek() == "="_tok) {
								advance(); // consume '='
								if (peek() == "default"_tok || peek() == "delete"_tok) advance();
								if (peek() == ";"_tok) advance();
							}
							else if (peek() == ";"_tok) advance();
							discard_saved_token(saved_pos);
							return true;
						}
					}
				}
				// Not a nested class member - restore to after the first '::' 
				restore_token_position(nested_check);
			}
			if (peek().is_identifier() && peek_info().value() == potential_class.value()) {
					Token ctor_name_token = peek_info();
					advance(); // consume constructor/destructor name
					if (peek() == "("_tok) {
						// This IS a constructor/destructor definition!
						discard_saved_token(ctor_check);
						std::string_view ctor_class_name = potential_class.value();

						// Create a void return type for constructors/destructors
						auto void_type = emplace_node<TypeSpecifierNode>(Type::Void, TypeQualifier::None, 0, ctor_name_token);
						auto [ctor_decl_node, ctor_decl_ref] = emplace_node_ref<DeclarationNode>(void_type, ctor_name_token);
						auto [ctor_func_node, ctor_func_ref] = emplace_node_ref<FunctionDeclarationNode>(ctor_decl_ref, ctor_name_token.value());

						// Parse parameter list
						FlashCpp::ParsedParameterList ctor_params;
						auto ctor_param_result = parse_parameter_list(ctor_params);
						if (ctor_param_result.is_error()) {
							discard_saved_token(saved_pos);
							return true; // consumed tokens, can't backtrack
						}
						for (const auto& param : ctor_params.parameters) {
							ctor_func_ref.add_parameter_node(param);
						}
						ctor_func_ref.set_is_variadic(ctor_params.is_variadic);

						// Skip trailing specifiers (const, noexcept, etc.)
						FlashCpp::MemberQualifiers ctor_quals;
						skip_function_trailing_specifiers(ctor_quals);
						// Skip requires clause if present
						if (peek() == "requires"_tok) {
							advance();
							if (peek() == "("_tok) {
								skip_balanced_parens();
							} else {
								while (!peek().is_eof() && peek() != "{"_tok && peek() != ";"_tok && peek() != ":"_tok) {
									advance();
								}
							}
						}
						// Skip member initializer list (for constructors)
						if (peek() == ":"_tok) {
							advance(); // consume ':'
							// Skip entries: name(args), name{args}, name<T>(args), ...
							// Brace-init in the list must be skipped as balanced
							// pairs so we don't confuse them with the function body '{'.
							while (!peek().is_eof() && peek() != ";"_tok) {
								if (peek() == "("_tok) {
									skip_balanced_parens();
								} else if (peek() == "{"_tok) {
									// Could be brace-init (member{val}) or function body.
									// After balanced braces, if next is ',' there are more initializers;
									// otherwise we've found the function body start.
									skip_balanced_braces();
									if (peek() != ","_tok) break;
								} else {
									advance();
								}
							}
						}

						// Save body position and handle body / = default / = delete
						bool ctor_is_defaulted = false;
						bool ctor_is_deleted = false;
						SaveHandle ctor_body_start = save_token_position();
						if (peek() == "{"_tok) {
							skip_balanced_braces();
						} else if (peek() == "="_tok) {
							// Handle = default; and = delete;
							advance(); // consume '='
							if (peek() == "default"_tok) {
								ctor_is_defaulted = true;
								advance(); // consume 'default'
							} else if (peek() == "delete"_tok) {
								ctor_is_deleted = true;
								advance(); // consume 'delete'
							}
							if (peek() == ";"_tok) {
								advance(); // consume ';'
							}
						} else if (peek() == ";"_tok) {
							advance();
						}

						// Register as out-of-line member function
						OutOfLineMemberFunction out_of_line_ctor;
						out_of_line_ctor.template_params = template_params;
						out_of_line_ctor.function_node = ctor_func_node;
						out_of_line_ctor.body_start = ctor_body_start;
						out_of_line_ctor.template_param_names = template_param_names;
						out_of_line_ctor.is_defaulted = ctor_is_defaulted;
						out_of_line_ctor.is_deleted = ctor_is_deleted;

						gTemplateRegistry.registerOutOfLineMember(ctor_class_name, std::move(out_of_line_ctor));

						FLASH_LOG(Templates, Debug, "Registered out-of-line template ",
						          (is_dtor ? "destructor" : "constructor"), ": ",
						          ctor_class_name);
						discard_saved_token(saved_pos);
						return true;
					}
				}
			}
		restore_token_position(ctor_check);
	}

	// Try to parse return type
	auto return_type_result = parse_type_specifier();
	if (return_type_result.is_error() || !return_type_result.node().has_value()) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	ASTNode return_type_node = *return_type_result.node();

	// Skip pointer/reference modifiers after the return type
	// Pattern: Type*, Type&, Type&&, Type* const, Type const*, etc.
	// This handles cases where the return type and class name are on separate lines:
	//   template<typename T>
	//   const typename Class<T>::nested_type*
	//   Class<T>::method(...) { ... }
	while (!peek().is_eof()) {
		auto token_val = peek_info().value();
		if (token_val == "*" || token_val == "&") {
			advance();
			// Also skip CV-qualifiers after pointer/reference
			parse_cv_qualifiers();
		} else {
			break;
		}
	}

	// Check for class name (identifier) or constructor pattern
	// For constructors: ClassName<Args>::ClassName(...)
	// parse_type_specifier already consumed "ClassName" as a type, so next is '<'
	Token class_name_token;
	std::string_view class_name;

	if (peek().is_identifier()) {
		// Normal case: return_type ClassName<Args>::FunctionName(...)
		class_name_token = peek_info();
		class_name = class_name_token.value();
		advance();
	} else if (peek() == "<"_tok && return_type_node.is<TypeSpecifierNode>()) {
		// Constructor pattern: ClassName<Args>::ClassName(...)
		// parse_type_specifier consumed "ClassName" as return type, but it's really the class name
		class_name_token = return_type_node.as<TypeSpecifierNode>().token();
		class_name = class_name_token.value();
	} else if (peek() == "::"_tok && return_type_node.is<TypeSpecifierNode>()) {
		// Namespace-qualified constructor pattern: ns::ClassName<Args>::ClassName(...)
		// parse_type_specifier consumed the full "ns::ClassName<Args>" as a type
		// The :: that follows leads to the member function/constructor name
		class_name_token = return_type_node.as<TypeSpecifierNode>().token();
		class_name = class_name_token.value();
	} else {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	// Check for template arguments after class name: ClassName<T>, etc.
	// This is optional - only present for template classes
	// Uses skip_template_arguments() which correctly handles '>>' tokens
	// for nested templates like hash<vector<bool, _Alloc>>
	if (peek() == "<"_tok) {
		skip_template_arguments();
	}

	// Check for '::'
	if (peek() != "::"_tok) {
		restore_token_position(saved_pos);
		return std::nullopt;
	}
	advance();  // consume '::'

	// This IS an out-of-line template member function definition!
	// Discard the saved position - we're committed to parsing this
	discard_saved_token(saved_pos);

	// Parse function name (or constructor/destructor/operator name)
	if (!peek().is_identifier()) {
		// Handle 'operator' keyword for operator member functions
		// (e.g., ClassName<T>::operator()(...))
		if (peek() == "operator"_tok) {
			Token op_token = peek_info();
			advance(); // consume 'operator'
			StringBuilder op_builder;
			op_builder.append("operator"sv);

			// Special handling for operator() - '(' followed by ')' is the call operator name
			if (peek() == "("_tok) {
				auto next_saved = save_token_position();
				advance(); // consume '('
				if (peek() == ")"_tok) {
					advance(); // consume ')'
					discard_saved_token(next_saved);
					op_builder.append("()"sv);
				} else {
					// Not operator() — the '(' starts the parameter list
					restore_token_position(next_saved);
				}
			}
			// Special handling for operator[] - '[' followed by ']'
			else if (peek() == "["_tok) {
				auto bracket_saved = save_token_position();
				advance(); // consume '['
				if (peek() == "]"_tok) {
					advance(); // consume ']'
					discard_saved_token(bracket_saved);
					op_builder.append("[]"sv);
				} else {
					restore_token_position(bracket_saved);
				}
			}
			else {
				// Other operators: consume tokens until we hit '(' (parameter list start)
				while (!peek().is_eof() && peek() != "("_tok) {
					if (peek() == "{"_tok || peek() == ";"_tok) break;
					op_builder.append(peek_info().value());
					advance();
				}
			}

			std::string_view op_name = op_builder.commit();
			Token function_name_token_op = Token(Token::Type::Identifier, op_name,
				op_token.line(), op_token.column(), op_token.file_index());

			// Skip to parameter list parsing with operator name
			// Create function declaration and skip the body
			auto [func_decl_node_op, func_decl_ref_op] = emplace_node_ref<DeclarationNode>(return_type_node, function_name_token_op);
			auto [func_node_op, func_ref_op] = emplace_node_ref<FunctionDeclarationNode>(func_decl_ref_op, function_name_token_op.value());

			if (peek() == "("_tok) {
				skip_balanced_parens();
			}
			FlashCpp::MemberQualifiers op_quals;
			skip_function_trailing_specifiers(op_quals);
			// Handle trailing return type: auto Class<T>::operator()(params) -> RetType
			if (peek() == "->"_tok) {
				advance(); // consume '->'
				auto trailing_type = parse_type_specifier();
				if (trailing_type.node().has_value() && trailing_type.node()->is<TypeSpecifierNode>()) {
					TypeSpecifierNode& trailing_ts = trailing_type.node()->as<TypeSpecifierNode>();
					consume_pointer_ref_modifiers(trailing_ts);
				}
			}
			if (peek() == "{"_tok) {
				skip_balanced_braces();
			} else if (peek() == ";"_tok) {
				advance();
			}

			FLASH_LOG(Templates, Debug, "Skipped out-of-line template operator: ",
			          class_name, "::", op_name);
			return true;
		}

		// Check for destructor: ~ClassName
		if (peek_info().value() == "~") {
			advance(); // consume '~'
			if (peek().is_identifier()) {
				// Destructor - skip the name and body
				advance(); // consume destructor name
				// Skip the parameter list and body
				if (peek() == "("_tok) {
					skip_balanced_parens();
				}
				FlashCpp::MemberQualifiers dtor_quals;
				skip_function_trailing_specifiers(dtor_quals);
				if (peek() == "{"_tok) {
					skip_balanced_braces();
				} else if (peek() == ";"_tok) {
					advance();
				}
				return true;
			}
		}
		return std::nullopt;  // Error - expected function name
	}

	Token function_name_token = peek_info();
	advance();

	// Check for template arguments after function name: handle<SmallStruct>
	// We need to parse these to register the specialization correctly
	std::vector<TemplateTypeArg> function_template_args;
	if (peek() == "<"_tok) {
		auto template_args_opt = parse_explicit_template_arguments();
		if (template_args_opt.has_value()) {
			function_template_args = *template_args_opt;
		} else {
			// If we can't parse template arguments, just skip them
			advance();  // consume '<'
			int angle_bracket_depth = 1;
			while (angle_bracket_depth > 0 && !peek().is_eof()) {
				if (peek() == "<"_tok) {
					angle_bracket_depth++;
				} else if (peek() == ">"_tok) {
					angle_bracket_depth--;
				}
				advance();
			}
		}
	}

	// Handle nested class template member: ClassName::NestedTemplate<Args>::FunctionName
	// When we have ClassName::NestType<Args>:: followed by more identifiers,
	// the actual function name is further down. Keep consuming qualified parts.
	// Note: saved_pos was already discarded above - we are committed to this parsing path,
	// so we must not return std::nullopt from within this loop. Instead, break out and let
	// the downstream code handle any unexpected tokens.
	while (peek() == "::"_tok) {
		advance(); // consume '::'

		// The previous function_name_token was actually a nested class name,
		// not a function name. Update class_name to track the innermost class.
		class_name = function_name_token.value();

		// Handle 'template' keyword disambiguator (e.g., ::template member<Args>)
		if (peek() == "template"_tok) {
			advance(); // consume 'template'
		}

		// Handle 'operator' keyword for operator member functions
		// (e.g., ClassName::operator==, ClassName::operator(), ClassName::operator[])
		if (peek() == "operator"_tok) {
			function_name_token = peek_info();
			advance(); // consume 'operator'
			// Build the operator name (operator==, operator(), operator[], etc.)
			StringBuilder op_builder;
			op_builder.append("operator"sv);

			// Special handling for operator() - '(' followed by ')' is the call operator name
			if (peek() == "("_tok) {
				auto next_saved = save_token_position();
				advance(); // consume '('
				if (peek() == ")"_tok) {
					advance(); // consume ')'
					discard_saved_token(next_saved);
					op_builder.append("()"sv);
				} else {
					// Not operator() — the '(' starts the parameter list
					restore_token_position(next_saved);
				}
			}
			// Special handling for operator[] - '[' followed by ']'
			else if (peek() == "["_tok) {
				auto bracket_saved = save_token_position();
				advance(); // consume '['
				if (peek() == "]"_tok) {
					advance(); // consume ']'
					discard_saved_token(bracket_saved);
					op_builder.append("[]"sv);
				} else {
					restore_token_position(bracket_saved);
				}
			}
			else {
				// Other operators: consume tokens until we hit '(' (parameter list start)
				while (!peek().is_eof() && peek() != "("_tok) {
					if (peek() == "{"_tok || peek() == ";"_tok) break;
					op_builder.append(peek_info().value());
					advance();
				}
			}

			std::string_view op_name = op_builder.commit();
			function_name_token = Token(Token::Type::Identifier, op_name,
				function_name_token.line(), function_name_token.column(),
				function_name_token.file_index());
			function_template_args.clear();
			break; // operator name consumed; next token should be '('
		}

		// Handle destructor: ~ClassName
		bool is_dtor = false;
		if (peek() == "~"_tok) {
			advance(); // consume '~'
			is_dtor = true;
		}

		// If we can't find an identifier here, break out of the loop
		// and let the downstream code handle the unexpected token
		if (!peek().is_identifier()) {
			break;
		}

		if (is_dtor) {
			// Build "~ClassName" token
			Token ident = peek_info();
			std::string_view dtor_name = StringBuilder().append("~"sv).append(ident.value()).commit();
			function_name_token = Token(Token::Type::Identifier, dtor_name,
				ident.line(), ident.column(), ident.file_index());
		} else {
			function_name_token = peek_info();
		}
		advance();
		// Reset function template args - they belonged to the nested class, not the function
		function_template_args.clear();
		// Check for template arguments on this new name
		if (peek() == "<"_tok) {
			auto template_args_opt = parse_explicit_template_arguments();
			if (template_args_opt.has_value()) {
				function_template_args = *template_args_opt;
			} else {
				advance();  // consume '<'
				int angle_bracket_depth = 1;
				while (angle_bracket_depth > 0 && !peek().is_eof()) {
					if (peek() == "<"_tok) angle_bracket_depth++;
					else if (peek() == ">"_tok) angle_bracket_depth--;
					advance();
				}
			}
		}
	}

	// Check if this is a static member variable definition (=) or a member function (()
	if (peek() == "="_tok) {
		// This is a static member variable definition: template<typename T> Type ClassName<T>::member = value;
		advance();  // consume '='
		
		// Parse initializer expression
		auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (init_result.is_error() || !init_result.node().has_value()) {
			FLASH_LOG(Parser, Error, "Failed to parse initializer for static member variable");
			return std::nullopt;
		}
		
		// Expect semicolon
		if (!consume(";"_tok)) {
			FLASH_LOG(Parser, Error, "Expected ';' after static member variable definition");
			return std::nullopt;
		}
		
		// Register the static member variable definition
		OutOfLineMemberVariable out_of_line_var;
		out_of_line_var.template_params = template_params;
		out_of_line_var.member_name = function_name_token.handle();  // Actually the variable name
		out_of_line_var.type_node = return_type_node;               // Actually the variable type
		out_of_line_var.initializer = *init_result.node();
		out_of_line_var.template_param_names = template_param_names;
		
		gTemplateRegistry.registerOutOfLineMemberVariable(class_name, std::move(out_of_line_var));
		
		FLASH_LOG(Templates, Debug, "Registered out-of-class static member variable definition: ", 
		          class_name, "::", function_name_token.value());
		
		return true;  // Successfully parsed out-of-line static member variable definition
	}
	
	// Check if this is a static member variable definition without initializer (;)
	// Pattern: template<typename T> Type ClassName<T>::member;
	if (peek() == ";"_tok) {
		advance();  // consume ';'
		
		// Register the static member variable definition without initializer
		// This is used for providing storage for static constexpr members declared in the class
		OutOfLineMemberVariable out_of_line_var;
		out_of_line_var.template_params = template_params;
		out_of_line_var.member_name = function_name_token.handle();  // Actually the variable name
		out_of_line_var.type_node = return_type_node;               // Actually the variable type
		// No initializer for this case
		out_of_line_var.template_param_names = template_param_names;
		
		gTemplateRegistry.registerOutOfLineMemberVariable(class_name, std::move(out_of_line_var));
		
		FLASH_LOG(Templates, Debug, "Registered out-of-class static member variable definition (no initializer): ", 
		          class_name, "::", function_name_token.value());
		
		return true;  // Successfully parsed out-of-line static member variable definition
	}
	
	// Parse parameter list for member function
	if (peek() != "("_tok) {
		return std::nullopt;  // Error - expected '(' for function definition
	}

	// Create a function declaration node
	auto [func_decl_node, func_decl_ref] = emplace_node_ref<DeclarationNode>(return_type_node, function_name_token);
	auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(func_decl_ref, function_name_token.value());

	// Parse parameters using unified parameter list parsing (Phase 1)
	FlashCpp::ParsedParameterList params;
	auto param_result = parse_parameter_list(params);
	if (param_result.is_error()) {
		return std::nullopt;
	}

	// Apply parsed parameters to the function
	for (const auto& param : params.parameters) {
		func_ref.add_parameter_node(param);
	}
	func_ref.set_is_variadic(params.is_variadic);

	// Phase 7: Validate signature against the template class declaration (if it exists)
	// Look up the template class to find the member function declaration
	auto template_class_opt = gTemplateRegistry.lookupTemplate(class_name);
	if (template_class_opt.has_value() && template_class_opt->is<TemplateClassDeclarationNode>()) {
		const TemplateClassDeclarationNode& template_class = template_class_opt->as<TemplateClassDeclarationNode>();
		const StructDeclarationNode& struct_decl = template_class.class_declaration().as<StructDeclarationNode>();
		
		// Find the member function with matching name
		for (const auto& member : struct_decl.member_functions()) {
			// Skip constructors, destructors, and non-FunctionDeclarationNode entries
			// (they use ConstructorDeclarationNode/DestructorDeclarationNode types)
			if (member.is_constructor || member.is_destructor || !member.function_declaration.is<FunctionDeclarationNode>()) {
				continue;
			}
			const FunctionDeclarationNode& member_func = member.function_declaration.as<FunctionDeclarationNode>();
			if (member_func.decl_node().identifier_token().value() == function_name_token.value()) {
				// Use validate_signature_match for validation
				auto validation_result = validate_signature_match(member_func, func_ref);
				if (!validation_result.is_match()) {
					FLASH_LOG(Parser, Warning, validation_result.error_message, " in out-of-line template member '",
					          class_name, "::", function_name_token.value(), "'");
					// Don't fail - templates may have dependent types that can't be fully resolved yet
				}
				break;
			}
		}
	}

	// Skip function trailing specifiers (const, volatile, noexcept, etc.)
	FlashCpp::MemberQualifiers member_quals;
	skip_function_trailing_specifiers(member_quals);

	// Handle trailing return type: auto Class<T>::method(params) -> RetType { ... }
	if (peek() == "->"_tok) {
		advance(); // consume '->'
		// Parse and discard the trailing return type
		auto trailing_type = parse_type_specifier();
		if (trailing_type.node().has_value() && trailing_type.node()->is<TypeSpecifierNode>()) {
			TypeSpecifierNode& trailing_ts = trailing_type.node()->as<TypeSpecifierNode>();
			consume_pointer_ref_modifiers(trailing_ts);
		}
	}

	// Skip requires clause if present
	if (peek() == "requires"_tok) {
		advance(); // consume 'requires'
		// Skip the requires expression - could be complex
		if (peek() == "("_tok) {
			skip_balanced_parens();
		} else {
			// Simple requires clause - skip until { or ;
			while (!peek().is_eof() && peek() != "{"_tok && peek() != ";"_tok && peek() != ":"_tok) {
				advance();
			}
		}
	}

	// Skip member initializer list (for constructors): ": member(args), ..."
	if (peek() == ":"_tok) {
		advance(); // consume ':'
		// Skip entries: name(args), name{args}, name<T>(args), ...
		// Brace-init in the list (e.g., member{value}) must be skipped as balanced
		// pairs so we don't confuse them with the function body '{'.
		while (!peek().is_eof() && peek() != ";"_tok) {
			if (peek() == "("_tok) {
				skip_balanced_parens();
			} else if (peek() == "{"_tok) {
				// Could be brace-init (member{val}) or function body.
				// After balanced braces, if next is ',' there are more initializers;
				// otherwise we've found the function body start.
				skip_balanced_braces();
				if (peek() != ","_tok) break;
			} else {
				advance();
			}
		}
	}

	// Save the position of the function body for delayed parsing
	// body_start must be right before '{' - trailing specifiers and initializer lists
	// are already consumed above
	bool member_is_defaulted = false;
	bool member_is_deleted = false;
	SaveHandle body_start = save_token_position();

	// Skip the function body for now (we'll re-parse it during instantiation or first use)
	if (peek() == "{"_tok) {
		skip_balanced_braces();
	} else if (peek() == "="_tok) {
		// Handle = default; and = delete; - store on function node and out-of-line record
		advance(); // consume '='
		if (peek() == "default"_tok) {
			member_is_defaulted = true;
			advance(); // consume 'default'
		} else if (peek() == "delete"_tok) {
			member_is_deleted = true;
			func_ref.set_is_deleted(true);
			advance(); // consume 'delete'
		}
		if (peek() == ";"_tok) {
			advance(); // consume ';'
		}
	} else if (peek() == ";"_tok) {
		advance(); // consume ';' (declaration without body)
	}

	// Check if this is a template member function specialization
	bool is_specialization = !function_template_args.empty();
	
	if (is_specialization) {
		// Register as a template specialization
		std::string_view qualified_name = StringBuilder()
			.append(class_name)
			.append("::")
			.append(function_name_token.value())
			.commit();
		
		// Save the body position for delayed parsing
		func_ref.set_template_body_position(body_start);
		
		gTemplateRegistry.registerSpecialization(qualified_name, function_template_args, func_node);
		
		FLASH_LOG(Templates, Debug, "Registered template member function specialization: ", 
		          qualified_name, " with ", function_template_args.size(), " template args");
	} else {
		// Regular out-of-line member function for a template class
		OutOfLineMemberFunction out_of_line_member;
		out_of_line_member.template_params = template_params;
		out_of_line_member.function_node = func_node;
		out_of_line_member.body_start = body_start;
		out_of_line_member.template_param_names = template_param_names;
		out_of_line_member.inner_template_params = inner_template_params;
		out_of_line_member.inner_template_param_names = inner_template_param_names;
		out_of_line_member.is_defaulted = member_is_defaulted;
		out_of_line_member.is_deleted = member_is_deleted;

		gTemplateRegistry.registerOutOfLineMember(class_name, std::move(out_of_line_member));

		if (!inner_template_params.empty()) {
			FLASH_LOG(Templates, Debug, "Registered nested template out-of-line member: ",
			          class_name, "::", function_name_token.value(),
			          " (outer params: ", template_params.size(),
			          ", inner params: ", inner_template_params.size(), ")");
		}
	}

	return true;  // Successfully parsed out-of-line definition
}

// Parse a template function body with concrete type bindings
// This is called during code generation to instantiate member function templates
std::optional<ASTNode> Parser::parseTemplateBody(
	SaveHandle body_pos,
	const std::vector<std::string_view>& template_param_names,
	const std::vector<Type>& concrete_types,
	StringHandle struct_name,
	TypeIndex struct_type_index
) {
	// Save current parser state using save_token_position so we can restore properly
	SaveHandle saved_cursor = save_token_position();

	// Bind template parameters to concrete types using RAII scope guard (Phase 6)
	FlashCpp::TemplateParameterScope template_scope;
	for (size_t i = 0; i < template_param_names.size() && i < concrete_types.size(); ++i) {
		Type concrete_type = concrete_types[i];
		auto param_name = StringTable::getOrInternStringHandle(template_param_names[i]);

		// Add a TypeInfo for this concrete type with the template parameter name
		auto& type_info = gTypeInfo.emplace_back(
			param_name,
			concrete_type,
			gTypeInfo.size(),
			0 // Placeholder size
		);

		// Register in global type lookup
		gTypesByName[param_name] = &type_info;
		template_scope.addParameter(&type_info);  // RAII cleanup on all return paths
	}

	// If this is a member function, set up member function context
	bool setup_member_context = struct_name.isValid() && struct_type_index != 0;
	ASTNode this_decl_node;  // Need to keep this alive for the duration of parsing
	if (setup_member_context) {
		// Find the struct in the type system
		auto struct_type_it = gTypesByName.find(struct_name);
		if (struct_type_it != gTypesByName.end()) {
			[[maybe_unused]] const TypeInfo* type_info = struct_type_it->second;
			
			// Add 'this' pointer to global symbol table
			// Create a token for 'this'
			Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
			
			// Create type node for 'this' (pointer to struct)
			auto this_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
				Type::UserDefined,
				struct_type_index,
				64,  // Pointer size
				this_token
			);
			this_type_node.as<TypeSpecifierNode>().add_pointer_level(CVQualifier::None);
			
			// Create declaration for 'this'
			this_decl_node = ASTNode::emplace_node<DeclarationNode>(this_type_node, this_token);
			
			// Add to global symbol table
			gSymbolTable.insert("this"sv, this_decl_node);
			
			// Also push member function context for good measure
			// Try to find the StructDeclarationNode in the symbol table
			auto struct_symbol_opt = lookup_symbol(struct_name);
			StructDeclarationNode* struct_node_ptr = nullptr;
			if (struct_symbol_opt.has_value() && struct_symbol_opt->is<StructDeclarationNode>()) {
				struct_node_ptr = &struct_symbol_opt->as<StructDeclarationNode>();
			}
			
			MemberFunctionContext ctx;
			ctx.struct_name = struct_name;
			ctx.struct_type_index = struct_type_index;
			ctx.struct_node = struct_node_ptr;
			ctx.local_struct_info = nullptr;  // Not needed for template member function instantiation
			member_function_context_stack_.push_back(ctx);
		}
	}

	// Restore to template body position (this sets current_token_ to the saved token)
	restore_lexer_position_only(body_pos);

	// The current token should now be '{' (the token that was saved)
	// parse_block() will consume it, so don't consume it here

	// Parse the block body
	auto block_result = parse_block();

	// Clean up member function context if we set it up
	if (setup_member_context && !member_function_context_stack_.empty()) {
		member_function_context_stack_.pop_back();
		// Remove 'this' from global symbol table
		// Note: gSymbolTable doesn't have a remove method, but since we're about to restore
		// the parser state anyway, the symbol table will revert to its previous state
	}

	// template_scope RAII guard automatically cleans up temporary type bindings

	// Restore original parser state
	restore_lexer_position_only(saved_cursor);

	if (block_result.is_error() || !block_result.node().has_value()) {
		return std::nullopt;
	}

	return block_result.node();
}

// Substitute template parameters in an AST node with concrete types/values
// This recursively traverses the AST and replaces TemplateParameterReferenceNode instances
