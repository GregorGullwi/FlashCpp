ParseResult Parser::parse_template_parameter_list(std::vector<ASTNode>& out_params) {
	// Save the current template parameter names so we can restore them later.
	// This allows nested template declarations to have their own parameter scope.
	std::vector<StringHandle> saved_template_param_names = current_template_param_names_;
	
	// Parse first parameter
	auto param_result = parse_template_parameter();
	if (param_result.is_error()) {
		current_template_param_names_ = std::move(saved_template_param_names);
		return param_result;
	}

	if (param_result.node().has_value()) {
		out_params.push_back(*param_result.node());
		// Add this parameter's name to current_template_param_names_ so that
		// subsequent parameters can reference it in their default values.
		// This enables patterns like: template<typename T, bool = is_arithmetic<T>::value>
		if (param_result.node()->is<TemplateParameterNode>()) {
			const auto& tparam = param_result.node()->as<TemplateParameterNode>();
			current_template_param_names_.push_back(tparam.nameHandle());
			FLASH_LOG(Templates, Debug, "Added template parameter '", tparam.name(), 
			          "' to current_template_param_names_ (now has ", current_template_param_names_.size(), " params)");
		}
	}

	// Parse additional parameters separated by commas
	while (peek() == ","_tok) {
		advance(); // consume ','

		param_result = parse_template_parameter();
		if (param_result.is_error()) {
			current_template_param_names_ = std::move(saved_template_param_names);
			return param_result;
		}

		if (param_result.node().has_value()) {
			out_params.push_back(*param_result.node());
			// Add this parameter's name too
			if (param_result.node()->is<TemplateParameterNode>()) {
				const auto& tparam = param_result.node()->as<TemplateParameterNode>();
				current_template_param_names_.push_back(tparam.nameHandle());
				FLASH_LOG(Templates, Debug, "Added template parameter '", tparam.name(), 
				          "' to current_template_param_names_ (now has ", current_template_param_names_.size(), " params)");
			}
		}
	}

	// Restore the original template parameter names.
	// The caller (parse_template_declaration) will set current_template_param_names_
	// to the full list of parameters for the body parsing phase.
	current_template_param_names_ = std::move(saved_template_param_names);

	return ParseResult::success();
}

// Parse a single template parameter: typename T, class T, int N, etc.
ParseResult Parser::parse_template_parameter() {
	ScopedTokenPosition saved_position(*this);

	// Check for template template parameter: template<template<typename> class Container>
	if (peek() == "template"_tok) {
		[[maybe_unused]] Token template_keyword = peek_info();
		advance(); // consume 'template'

		// Expect '<' to start nested template parameter list
		if (peek() != "<"_tok) {
			FLASH_LOG(Parser, Error, "Expected '<' after 'template', got: ",
				(!peek().is_eof() ? std::string("'") + std::string(peek_info().value()) + "'" : "<EOF>"));
			return ParseResult::error("Expected '<' after 'template' keyword in template template parameter", current_token_);
		}
		advance(); // consume '<'

		// Parse nested template parameter forms (just type specifiers, no names)
		std::vector<ASTNode> nested_params;
		auto param_list_result = parse_template_template_parameter_forms(nested_params);
		if (param_list_result.is_error()) {
			FLASH_LOG(Parser, Error, "parse_template_template_parameter_forms failed");
			return param_list_result;
		}

		// Expect '>' to close nested template parameter list
		if (peek() != ">"_tok) {
			FLASH_LOG(Parser, Error, "Expected '>' after nested template parameter list, got: ",
				(!peek().is_eof() ? std::string("'") + std::string(peek_info().value()) + "'" : "<EOF>"));
			return ParseResult::error("Expected '>' after nested template parameter list", current_token_);
		}
		advance(); // consume '>'

		// Expect 'class' or 'typename'
		if (!peek().is_keyword() ||
		    (peek() != "class"_tok && peek() != "typename"_tok)) {
			FLASH_LOG(Parser, Error, "Expected 'class' or 'typename' after template parameter list, got: ",
				(!peek().is_eof() ? std::string("'") + std::string(peek_info().value()) + "'" : "<EOF>"));
			return ParseResult::error("Expected 'class' or 'typename' after template parameter list in template template parameter", current_token_);
		}
		advance(); // consume 'class' or 'typename'

		// Parameter name is optional (unnamed template template parameters are valid C++)
		// e.g., template <class, class, template <class> class, template <class> class>
		std::string_view param_name;
		Token param_name_token;
		if (peek().is_identifier()) {
			param_name_token = peek_info();
			param_name = param_name_token.value();
			advance(); // consume parameter name
		} else {
			// Generate a unique synthetic name for unnamed template template parameter.
			// This avoids collisions when multiple unnamed template template parameters
			// appear in the same declaration (e.g., template<template<class> class, template<class> class>).
			// Without unique names, substitution maps would overwrite earlier bindings.
			static int anonymous_template_template_counter = 0;
			param_name = StringBuilder().append("__anon_ttp_"sv).append(static_cast<int64_t>(anonymous_template_template_counter++)).commit();
			param_name_token = current_token_;
		}

		// Create template template parameter node
		auto param_node = emplace_node<TemplateParameterNode>(StringTable::getOrInternStringHandle(param_name), std::move(nested_params), param_name_token);

		// TODO: Handle default arguments (e.g., template<typename> class Container = std::vector)

		return saved_position.success(param_node);
	}

	// Check for concept-constrained type parameter: Concept T, Concept<U> T, namespace::Concept T
	if (peek().is_identifier()) {
		auto concept_check_pos = save_token_position();
		
		// Build potential concept name (possibly namespace-qualified)
		StringBuilder potential_concept_sb;
		potential_concept_sb.append(peek_info().value());
		Token concept_token = peek_info();
		advance(); // consume first identifier
		
		// Check for namespace-qualified concept: ns::concept or ns::ns2::concept
		while (peek() == "::"_tok) {
			advance(); // consume '::'
			if (!peek().is_identifier()) {
				// Not a valid qualified name, restore and continue
				restore_token_position(concept_check_pos);
				potential_concept_sb.reset();
				break;
			}
			potential_concept_sb.append("::");
			potential_concept_sb.append(peek_info().value());
			concept_token = peek_info();
			advance(); // consume next identifier
		}
		
		// Intern the concept name string and get a stable string_view
		StringHandle concept_handle = StringTable::getOrInternStringHandle(potential_concept_sb);
		std::string_view potential_concept = StringTable::getStringView(concept_handle);
		
		// Check if this identifier is a registered concept
		FLASH_LOG_FORMAT(Parser, Debug, "parse_template_parameter: Checking if '{}' is a concept", potential_concept);
		if (gConceptRegistry.hasConcept(potential_concept)) {
			FLASH_LOG_FORMAT(Parser, Debug, "parse_template_parameter: '{}' IS a registered concept", potential_concept);
			// Check for template arguments: Concept<U>
			// For now, we'll skip template argument parsing for concepts
			// and just expect the parameter name
			if (peek() == "<"_tok) {
				// Skip template arguments for now
				// TODO: Parse and store concept template arguments
				int angle_depth = 0;
				do {
					update_angle_depth(peek(), angle_depth);
					advance();
				} while (angle_depth > 0 && !peek().is_eof());
			}
			
			// Check for ellipsis (parameter pack): Concept... Ts
			bool is_variadic = false;
			if (!peek().is_eof() && 
			    (peek().is_operator() || peek().is_punctuator()) &&
			    peek() == "..."_tok) {
				advance(); // consume '...'
				is_variadic = true;
			}
			
			// Expect identifier (parameter name)
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected identifier after concept constraint", current_token_);
			}
			
			Token param_name_token = peek_info();
			std::string_view param_name = param_name_token.value();
			advance(); // consume parameter name
			
			// Create type parameter node (concept-constrained)
			auto param_node = emplace_node<TemplateParameterNode>(StringTable::getOrInternStringHandle(param_name), param_name_token);
			
			// Store the concept constraint
			param_node.as<TemplateParameterNode>().set_concept_constraint(potential_concept);
			
			// Set variadic flag if this is a parameter pack
			if (is_variadic) {
				param_node.as<TemplateParameterNode>().set_variadic(true);
			}
			
			// Handle default arguments (e.g., Concept T = int)
			// Note: Parameter packs cannot have default arguments
			if (!is_variadic && peek() == "="_tok) {
				advance(); // consume '='
				
				// Parse the default type
				auto default_type_result = parse_type_specifier();
				if (default_type_result.is_error()) {
					return ParseResult::error("Expected type after '=' in template parameter default", current_token_);
				}
				
				if (default_type_result.node().has_value()) {
					TypeSpecifierNode& type_spec = default_type_result.node()->as<TypeSpecifierNode>();
					
					// Apply pointer/reference qualifiers (ptr-operator in C++20 grammar)
					consume_pointer_ref_modifiers(type_spec);
					param_node.as<TemplateParameterNode>().set_default_value(*default_type_result.node());
				}
			}
			
			return saved_position.success(param_node);
		} else {
			// Not a concept, restore position and let other parsing handle it
			restore_token_position(concept_check_pos);
		}
	}
	
	// Check for type parameter: typename or class
	if (peek().is_keyword()) {
		std::string_view keyword = peek_info().value();

		if (keyword == "typename" || keyword == "class") {
			[[maybe_unused]] Token keyword_token = peek_info();
			advance(); // consume 'typename' or 'class'

			// Check for ellipsis (parameter pack): typename... Args
			bool is_variadic = false;
			if (!peek().is_eof() && 
			    (peek().is_operator() || peek().is_punctuator()) &&
			    peek() == "..."_tok) {
				advance(); // consume '...'
				is_variadic = true;
			}

			// Check for identifier (parameter name) - it's optional for anonymous parameters
			std::string_view param_name;
			Token param_name_token;
			
			if (peek().is_identifier()) {
				// Named parameter
				param_name_token = peek_info();
				param_name = param_name_token.value();
				advance(); // consume parameter name
			} else {
				// Anonymous parameter - generate unique name
				// Check if next token is valid for end of parameter (comma, >, or =)
				if (!peek().is_eof() && 
				    ((peek().is_punctuator() && peek() == ","_tok) ||
				     (peek().is_operator() && (peek() == ">"_tok || peek() == "="_tok)))) {
					// Generate unique anonymous parameter name
					static int anonymous_type_counter = 0;
					param_name = StringBuilder().append("__anon_type_"sv).append(static_cast<int64_t>(anonymous_type_counter++)).commit();
					
					// Use the current token as the token reference
					param_name_token = current_token_;
				} else {
					return ParseResult::error("Expected identifier after 'typename' or 'class'", current_token_);
				}
			}

			// Create type parameter node
			auto param_node = emplace_node<TemplateParameterNode>(StringTable::getOrInternStringHandle(param_name), param_name_token);
			
			// Set variadic flag if this is a parameter pack
			if (is_variadic) {
				param_node.as<TemplateParameterNode>().set_variadic(true);
			}

			// Handle default arguments (e.g., typename T = int)
			// Note: Parameter packs cannot have default arguments
			if (!is_variadic && peek() == "="_tok) {
				advance(); // consume '='
				
				// Parse the default type
				auto default_type_result = parse_type_specifier();
				if (default_type_result.is_error()) {
					return ParseResult::error("Expected type after '=' in template parameter default", current_token_);
				}
				
				if (default_type_result.node().has_value()) {
					TypeSpecifierNode& type_spec = default_type_result.node()->as<TypeSpecifierNode>();
					
					// Apply pointer/reference qualifiers (ptr-operator in C++20 grammar)
					consume_pointer_ref_modifiers(type_spec);
					param_node.as<TemplateParameterNode>().set_default_value(*default_type_result.node());
				}
			}

			return saved_position.success(param_node);
		}
	}

	// Check for non-type parameter: int N, bool B, etc.
	// Parse type specifier
	auto type_result = parse_type_specifier();
	if (type_result.is_error()) {
		return type_result;
	}

	if (!type_result.node().has_value()) {
		return ParseResult::error("Expected type specifier for non-type template parameter", current_token_);
	}

	// Check for ellipsis (parameter pack): int... Ns
	bool is_variadic = false;
	if (!peek().is_eof() && 
	    (peek().is_operator() || peek().is_punctuator()) &&
	    peek() == "..."_tok) {
		advance(); // consume '...'
		is_variadic = true;
	}	
	// Check for identifier (parameter name) - it's optional for anonymous parameters
	std::string_view param_name;
	Token param_name_token;
	[[maybe_unused]] bool is_anonymous = false;
	
	if (peek().is_identifier()) {
		// Named parameter
		param_name_token = peek_info();
		param_name = param_name_token.value();
		advance(); // consume parameter name
	} else {
		// Anonymous parameter - generate unique name
		// Check if next token is valid for end of parameter (comma, >, or =)
		if (!peek().is_eof() && 
		    ((peek().is_punctuator() && peek() == ","_tok) ||
		     (peek().is_operator() && (peek() == ">"_tok || peek() == "="_tok)))) {
			// Generate unique anonymous parameter name
			static int anonymous_counter = 0;
			param_name = StringBuilder().append("__anon_param_"sv).append(static_cast<int64_t>(anonymous_counter++)).commit();
			
			// Store the anonymous name in a way that persists
			// We'll use the current token as the token reference
			param_name_token = current_token_;
			is_anonymous = true;
		} else {
			return ParseResult::error("Expected identifier for non-type template parameter", current_token_);
		}
	}

	// Create non-type parameter node
	auto param_node = emplace_node<TemplateParameterNode>(StringTable::getOrInternStringHandle(param_name), *type_result.node(), param_name_token);
	
	// Set variadic flag if this is a parameter pack
	if (is_variadic) {
		param_node.as<TemplateParameterNode>().set_variadic(true);
	}

	// Handle default arguments (e.g., int N = 10, size_t M = sizeof(T))
	// Note: Parameter packs cannot have default arguments
	if (!is_variadic && peek() == "="_tok) {
		advance(); // consume '='
		
		// Parse the default value expression in template argument context
		// This context tells parse_expression to stop at '>' and ',' which delimit template arguments
		auto default_value_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::TemplateArgument);
		if (default_value_result.is_error()) {
			return ParseResult::error("Expected expression after '=' in template parameter default", current_token_);
		}
		
		if (default_value_result.node().has_value()) {
			param_node.as<TemplateParameterNode>().set_default_value(*default_value_result.node());
		}
	}

	return saved_position.success(param_node);
}

// Parse template template parameter forms (just type specifiers without names)
// Used for template<template<typename> class Container> syntax
ParseResult Parser::parse_template_template_parameter_forms(std::vector<ASTNode>& out_params) {
	// Parse first parameter form
	auto param_result = parse_template_template_parameter_form();
	if (param_result.is_error()) {
		return param_result;
	}

	if (param_result.node().has_value()) {
		out_params.push_back(*param_result.node());
	}

	// Parse additional parameter forms separated by commas
	while (peek() == ","_tok) {
		advance(); // consume ','

		param_result = parse_template_template_parameter_form();
		if (param_result.is_error()) {
			return param_result;
		}

		if (param_result.node().has_value()) {
			out_params.push_back(*param_result.node());
		}
	}

	return ParseResult::success();
}

// Parse a single template template parameter form (just type specifier, no name)
// For template<template<typename> class Container>, this parses "typename"
// Also handles variadic packs: template<typename...> class Container
// Also handles nested template template parameters: template<template<typename> class> class TTT
ParseResult Parser::parse_template_template_parameter_form() {
	ScopedTokenPosition saved_position(*this);

	// Handle nested template template parameter: template<template<typename> class> class TTT
	if (peek().is_keyword() && peek() == "template"_tok) {
		return saved_position.propagate(parse_template_parameter());
	}

	// Only support typename and class for now (no non-type parameters in template template parameters)
	if (peek().is_keyword()) {
		std::string_view keyword = peek_info().value();

		if (keyword == "typename" || keyword == "class") {
			Token keyword_token = peek_info();
			advance(); // consume 'typename' or 'class'

			// Check for ellipsis (parameter pack): typename... 
			// This handles patterns like: template<typename...> class Op
			bool is_variadic = false;
			if (!peek().is_eof() && 
			    (peek().is_operator() || peek().is_punctuator()) &&
			    peek() == "..."_tok) {
				advance(); // consume '...'
				is_variadic = true;
			}

			// For template template parameters, we don't expect an identifier name
			// Just create a type parameter node with an empty name
			auto param_node = emplace_node<TemplateParameterNode>(StringHandle(), keyword_token);
			
			// Set variadic flag if this is a parameter pack
			if (is_variadic) {
				param_node.as<TemplateParameterNode>().set_variadic(true);
			}

			return saved_position.success(param_node);
		}
	}

	return ParseResult::error("Expected 'typename' or 'class' in template template parameter form", current_token_);
}

// Phase 6: Shared helper for template function declaration parsing
// This eliminates duplication between parse_template_declaration() and parse_member_function_template()
// Parses: type_and_name + function_declaration + body handling (semicolon or skip braces)
// Template parameters must already be registered in gTypesByName via TemplateParameterScope
std::optional<std::vector<TemplateTypeArg>> Parser::parse_explicit_template_arguments(std::vector<ASTNode>* out_type_nodes) {
	// Recursion depth guard to prevent stack overflow on deeply nested template arguments
	// Stack size increased to 8MB in FlashCppMSVC.vcxproj to handle deep recursion
	static thread_local int template_arg_recursion_depth = 0;
	constexpr int MAX_TEMPLATE_ARG_RECURSION_DEPTH = 20;
	
	struct RecursionGuard {
		int& depth;
		RecursionGuard(int& d) : depth(d) { ++depth; }
		~RecursionGuard() { --depth; }
	} guard(template_arg_recursion_depth);
	
	if (template_arg_recursion_depth > MAX_TEMPLATE_ARG_RECURSION_DEPTH) {
		FLASH_LOG_FORMAT(Templates, Error, "Hit MAX_TEMPLATE_ARG_RECURSION_DEPTH limit ({}) in parse_explicit_template_arguments", MAX_TEMPLATE_ARG_RECURSION_DEPTH);
		return std::nullopt;
	}
	
	FLASH_LOG_FORMAT(Templates, Debug, "parse_explicit_template_arguments called, in_sfinae_context={}", in_sfinae_context_);
	
	// Save position in case this isn't template arguments
	auto saved_pos = save_token_position();

	// Check for '<'
	if (peek() != "<"_tok) {
		return std::nullopt;
	}
	
	// Prevent infinite loop: don't retry template argument parsing at the same position
	if (saved_pos == last_failed_template_arg_parse_handle_) {
		return std::nullopt;
	}
	
	advance(); // consume '<'
	last_failed_template_arg_parse_handle_ = SIZE_MAX;  // Clear failure marker - we're making progress

	std::vector<TemplateTypeArg> template_args;

	// Check for empty template argument list (e.g., Container<>)
	// Also handle >> for nested templates: Container<__void_t<>>
	if (peek() == ">"_tok) {
		advance(); // consume '>'
		// Success - discard saved position
		discard_saved_token(saved_pos);
		return template_args;  // Return empty vector
	}
	
	// Handle >> token for empty template arguments in nested context (e.g., __void_t<>>)
	if (peek() == ">>"_tok) {
		FLASH_LOG(Parser, Debug, "Empty template argument list with >> token, splitting");
		split_right_shift_token();
		// Now peek() returns '>'
		if (peek() == ">"_tok) {
			advance(); // consume first '>'
			discard_saved_token(saved_pos);
			return template_args;  // Return empty vector
		}
	}

	// Parse template arguments
	while (true) {
		// Save position in case type parsing fails
		SaveHandle arg_saved_pos = save_token_position();

		// First, try to parse an expression (for non-type template parameters)
		// Use parse_expression with ExpressionContext::TemplateArgument to handle
		// member access expressions like is_int<T>::value and complex expressions
		// like T::value || my_or<Rest...>::value
		// Precedence 2 allows all binary operators except comma (precedence 1)
		// The TemplateArgument context ensures we stop at '>' and ',' delimiters
		auto expr_result = parse_expression(2, ExpressionContext::TemplateArgument);
		if (!expr_result.is_error() && expr_result.node().has_value()) {
			// Successfully parsed an expression - check if it's a boolean or numeric literal
			const ExpressionNode& expr = expr_result.node()->as<ExpressionNode>();
			
			// Handle boolean literals (true/false)
			if (std::holds_alternative<BoolLiteralNode>(expr)) {
				const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr);
				TemplateTypeArg bool_arg(lit.value() ? 1 : 0, Type::Bool);
				
				// Check for pack expansion (...)
				if (peek() == "..."_tok) {
					advance(); // consume '...'
					bool_arg.is_pack = true;
					FLASH_LOG(Templates, Debug, "Marked boolean literal as pack expansion");
				}
				
				template_args.push_back(bool_arg);
				if (out_type_nodes && expr_result.node().has_value()) {
					out_type_nodes->push_back(*expr_result.node());
				}
				discard_saved_token(arg_saved_pos);
				
				// Check for ',' or '>' after the boolean literal (or after pack expansion)
				if (peek().is_eof()) {
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}

				// Phase 5: Handle >> token splitting for nested templates
				if (peek() == ">>"_tok) {
					split_right_shift_token();
				}

				if (peek() == ">"_tok) {
					advance(); // consume '>'
					break;
				}

				if (peek() == ","_tok) {
					advance(); // consume ','
					continue;
				}

				// Unexpected token after boolean literal
				FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments unexpected token after boolean literal");
				restore_token_position(saved_pos);
				last_failed_template_arg_parse_handle_ = saved_pos;
				return std::nullopt;
			}
			
			// Handle numeric literals
			if (std::holds_alternative<NumericLiteralNode>(expr)) {
				const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
				const auto& val = lit.value();
				Type literal_type = lit.type();  // Get the type of the literal (bool, int, etc.)
				TemplateTypeArg num_arg;
				if (std::holds_alternative<unsigned long long>(val)) {
					num_arg = TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val)), literal_type);
					discard_saved_token(arg_saved_pos);
					// Successfully parsed a non-type template argument, continue to check for ',' or '>' or '...'
				} else if (std::holds_alternative<double>(val)) {
					num_arg = TemplateTypeArg(static_cast<int64_t>(std::get<double>(val)), literal_type);
					discard_saved_token(arg_saved_pos);
					// Successfully parsed a non-type template argument, continue to check for ',' or '>' or '...'
				} else {
					FLASH_LOG(Parser, Error, "Unsupported numeric literal type");
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}
				
				// Check for pack expansion (...)
				if (peek() == "..."_tok) {
					advance(); // consume '...'
					num_arg.is_pack = true;
					FLASH_LOG(Templates, Debug, "Marked numeric literal as pack expansion");
				}
				
				template_args.push_back(num_arg);
				if (out_type_nodes && expr_result.node().has_value()) {
					out_type_nodes->push_back(*expr_result.node());
				}
				
				// Check for ',' or '>' after the numeric literal (or after pack expansion)
				if (peek().is_eof()) {
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}

				// Phase 5: Handle >> token splitting for nested templates
				if (peek() == ">>"_tok) {
					split_right_shift_token();
				}

				if (peek() == ">"_tok) {
					advance(); // consume '>'
					break;
				}

				if (peek() == ","_tok) {
					advance(); // consume ','
					continue;
				}

				// Unexpected token after numeric literal
				FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments unexpected token after numeric literal: '", 
				          peek_info().value(), "' (might be comparison operator)");
				restore_token_position(saved_pos);
				last_failed_template_arg_parse_handle_ = saved_pos;
				return std::nullopt;
			}

			// Expression is not a numeric literal - try to evaluate it as a constant expression
			// This handles cases like is_int<T>::value where the expression needs evaluation
			// Evaluate constant expressions in two cases:
			// 1. During SFINAE context (template instantiation with concrete arguments)
			// 2. When NOT parsing a template body (e.g., global scope type alias like `using X = holder<1 ? 2 : 3>`)
			// Only skip evaluation during template DECLARATION when template parameters are not yet instantiated
			bool should_try_constant_eval = in_sfinae_context_ || !parsing_template_body_;
			if (should_try_constant_eval) {
				FLASH_LOG(Templates, Debug, "Trying to evaluate non-literal expression as constant (in_sfinae=", 
				          in_sfinae_context_, ", parsing_template_body=", parsing_template_body_, ")");
				auto const_value = try_evaluate_constant_expression(*expr_result.node());
				if (const_value.has_value()) {
					// Successfully evaluated as a constant expression
					TemplateTypeArg const_arg(const_value->value, const_value->type);
					
					// Check for pack expansion (...)
					if (peek() == "..."_tok) {
						advance(); // consume '...'
						const_arg.is_pack = true;
						FLASH_LOG(Templates, Debug, "Marked constant expression as pack expansion");
					}
					
					template_args.push_back(const_arg);
					discard_saved_token(arg_saved_pos);
					
					// Check for ',' or '>' after the expression (or after pack expansion)
					if (peek().is_eof()) {
						restore_token_position(saved_pos);
						last_failed_template_arg_parse_handle_ = saved_pos;
						return std::nullopt;
					}

					// Phase 5: Handle >> token splitting for nested templates
					if (peek() == ">>"_tok) {
						split_right_shift_token();
					}

					if (peek() == ">"_tok) {
						advance(); // consume '>'
						break;
					}

					if (peek() == ","_tok) {
						advance(); // consume ','
						continue;
					}

					// Unexpected token after expression
					FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments unexpected token after constant expression");
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}
				
				// Constant evaluation failed - check if this is a noexcept or similar expression
				// that should be accepted as a dependent template argument.
				// NoexceptExprNode, SizeofExprNode, AlignofExprNode, and TypeTraitExprNode are
				// compile-time expressions that may contain dependent expressions.
				// QualifiedIdentifierNode represents patterns like is_same<T, int>::value where
				// the expression is a static member access that depends on template parameters.
				// If the next token is a valid delimiter, accept the expression as dependent.
				bool is_compile_time_expr = std::holds_alternative<NoexceptExprNode>(expr) ||
				                            std::holds_alternative<SizeofExprNode>(expr) ||
				                            std::holds_alternative<AlignofExprNode>(expr) ||
				                            std::holds_alternative<TypeTraitExprNode>(expr) ||
				                            std::holds_alternative<QualifiedIdentifierNode>(expr);
				
				if (is_compile_time_expr && !peek().is_eof()) {
					// Handle >> token splitting for nested templates
					if (peek() == ">>"_tok) {
						split_right_shift_token();
					}
					
					// Before accepting as dependent, check if a QualifiedIdentifierNode is actually
					// a concrete type (e.g. std::ratio<1,2> which was already instantiated during
					// expression parsing). Concrete types should fall through to type parsing,
					// not be marked as dependent compile-time expressions.
					bool is_concrete_qualified_type = false;
					if (std::holds_alternative<QualifiedIdentifierNode>(expr) &&
					    (peek() == ">"_tok || peek() == ","_tok)) {
						const auto& qi = std::get<QualifiedIdentifierNode>(expr);
						std::string_view qname = buildQualifiedNameFromHandle(qi.namespace_handle(), qi.name());
						auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qname));
						if (type_it != gTypesByName.end() && type_it->second->struct_info_ != nullptr) {
							FLASH_LOG(Templates, Debug, "QualifiedIdentifierNode '", qname,
							          "' is a concrete type, falling through to type parsing");
							is_concrete_qualified_type = true;
							restore_token_position(arg_saved_pos);
						}
					}

					if (!is_concrete_qualified_type && (peek() == ">"_tok || peek() == ","_tok || peek() == "..."_tok)) {
						FLASH_LOG(Templates, Debug, "Accepting dependent compile-time expression as template argument");
						// Create a dependent template argument
						TemplateTypeArg dependent_arg;
						dependent_arg.base_type = Type::Bool;  // noexcept, sizeof, alignof return bool/size_t
						dependent_arg.type_index = 0;
						dependent_arg.is_value = true;  // This is a non-type (value) template argument
						dependent_arg.is_dependent = true;
						
						// Check for pack expansion (...)
						if (peek() == "..."_tok) {
							advance(); // consume '...'
							dependent_arg.is_pack = true;
							FLASH_LOG(Templates, Debug, "Marked compile-time expression as pack expansion");
						}
						
						template_args.push_back(dependent_arg);
						if (out_type_nodes && expr_result.node().has_value()) {
							out_type_nodes->push_back(*expr_result.node());
						}
						discard_saved_token(arg_saved_pos);
						
						// Handle >> token splitting again after pack expansion check
						if (peek() == ">>"_tok) {
							split_right_shift_token();
						}
						
						if (peek() == ">"_tok) {
							advance(); // consume '>'
							break;
						}
						
						if (peek() == ","_tok) {
							advance(); // consume ','
							continue;
						}
					}
				}
			} else {
				FLASH_LOG(Templates, Debug, "Skipping constant expression evaluation (in template body with dependent context)");
				
				// BUGFIX: Even in a template body, static constexpr members like __g and __d2
				// in a partial specialization have concrete values and should be evaluated.
				// Try constant evaluation for simple identifiers that refer to static members.
				bool evaluated_static_member = false;
				std::optional<ConstantValue> static_member_value;
				
				if (std::holds_alternative<IdentifierNode>(expr) && !struct_parsing_context_stack_.empty()) {
					const auto& id = std::get<IdentifierNode>(expr);
					StringHandle id_handle = StringTable::getOrInternStringHandle(id.name());
					const auto& ctx = struct_parsing_context_stack_.back();
					
					// Check local_struct_info for static constexpr members
					if (ctx.local_struct_info != nullptr) {
						for (const auto& static_member : ctx.local_struct_info->static_members) {
							if (static_member.getName() == id_handle && static_member.initializer.has_value()) {
								// Try to evaluate the static member's initializer
								static_member_value = try_evaluate_constant_expression(*static_member.initializer);
								if (static_member_value.has_value()) {
									FLASH_LOG(Templates, Debug, "Evaluated static constexpr member '", id.name(), 
									          "' to value ", static_member_value->value);
									evaluated_static_member = true;
								}
								break;
							}
						}
					}
					
					// Also check struct_node's static_members
					if (!evaluated_static_member && ctx.struct_node != nullptr) {
						for (const auto& static_member : ctx.struct_node->static_members()) {
							if (static_member.name == id_handle && static_member.initializer.has_value()) {
								static_member_value = try_evaluate_constant_expression(*static_member.initializer);
								if (static_member_value.has_value()) {
									FLASH_LOG(Templates, Debug, "Evaluated static constexpr member '", id.name(),
									          "' (from struct_node) to value ", static_member_value->value);
									evaluated_static_member = true;
								}
								break;
							}
						}
					}
				}
				
				if (evaluated_static_member && static_member_value.has_value()) {
					// Successfully evaluated static member - create template argument
					TemplateTypeArg const_arg(static_member_value->value, static_member_value->type);
					
					// Check for pack expansion (...)
					if (peek() == "..."_tok) {
						advance();
						const_arg.is_pack = true;
					}
					
					template_args.push_back(const_arg);
					discard_saved_token(arg_saved_pos);
					
					// Handle next token
					if (peek() == ">>"_tok) {
						split_right_shift_token();
					}
					if (peek() == ">"_tok) {
						advance();
						break;  // Break from outer while loop
					}
					if (peek() == ","_tok) {
						advance();
						continue;  // Continue to next template argument
					}
				}
				
				// During template declaration, expressions like is_int<T>::value are dependent
				// and cannot be evaluated yet. Check if we successfully parsed such an expression
				// by verifying that the next token is ',' or '>'
				FLASH_LOG_FORMAT(Templates, Debug, "After parsing expression, peek_token={}", 
				                 !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
				
				// Special case: If we parsed T[N] as an array subscript expression,
				// this is actually an array type declarator in a specialization pattern,
				// not an array access. Reparse as a type.
				bool is_array_subscript = std::holds_alternative<ArraySubscriptNode>(expr);
				if (is_array_subscript) {
					FLASH_LOG(Templates, Debug, "Detected array subscript in template arg - reparsing as array type");
					restore_token_position(arg_saved_pos);
					// Fall through to type parsing below
				} else {
				
				// Special case: If out_type_nodes is provided AND the expression is a simple identifier,
				// we should fall through to type parsing so identifiers get properly converted to TypeSpecifierNode.
				// This is needed for deduction guides where template parameters must be TypeSpecifierNode.
				// However, complex expressions like is_int<T>::value should still be accepted as dependent expressions.
				// 
				// ALSO: If we parsed a simple identifier followed by '<', we should fall through to type parsing
				// because this is likely a template type (e.g., enable_if_t<...>), not a value expression.
				// 
				// ALSO: If followed by '[', this is an array type declarator - must parse as type
				// 
				// IMPORTANT: If followed by '...', this is pack expansion, NOT a type - accept as dependent expression
				bool is_simple_identifier = std::holds_alternative<IdentifierNode>(expr) || 
				                            std::holds_alternative<TemplateParameterReferenceNode>(expr);
				[[maybe_unused]] bool is_function_call_expr = std::holds_alternative<FunctionCallNode>(expr);
				bool followed_by_template_args = peek() == "<"_tok;
				bool followed_by_array_declarator = peek() == "["_tok;
				bool followed_by_pack_expansion = peek() == "..."_tok;
				bool followed_by_reference = !peek().is_eof() && (peek() == "&"_tok || peek() == "&&"_tok);
				bool followed_by_pointer = peek() == "*"_tok;
				bool should_try_type_parsing = (out_type_nodes != nullptr && is_simple_identifier && !followed_by_pack_expansion) ||
				                               (is_simple_identifier && followed_by_template_args) ||
				                               (is_simple_identifier && followed_by_array_declarator) ||
				                               (is_simple_identifier && followed_by_reference) ||
				                               (is_simple_identifier && followed_by_pointer);
				
				if (!should_try_type_parsing && !peek().is_eof() && 
				    (peek() == ","_tok || peek() == ">"_tok || peek() == ">>"_tok || peek() == "..."_tok)) {
					// Check if this is actually a concrete type (not a template parameter)
					// If it's a concrete struct or type alias, we should fall through to type parsing instead
					bool is_concrete_type = false;
					if (std::holds_alternative<IdentifierNode>(expr)) {
						const auto& id = std::get<IdentifierNode>(expr);
						auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(id.name()));
						if (type_it != gTypesByName.end()) {
							const TypeInfo* type_info = type_it->second;
							// Check if it's a concrete struct (has struct_info_)
							// OR if it's a type alias that resolves to a concrete type
							// Type aliases have type_index pointing to the underlying type
							if (type_info->struct_info_ != nullptr) {
								is_concrete_type = true;
								FLASH_LOG(Templates, Debug, "Identifier '", id.name(), "' is a concrete struct type, falling through to type parsing");
							} else if (type_info->type_index_ < gTypeInfo.size()) {
								// Check if this is a type alias (type_index points to underlying type)
								// and the underlying type is concrete (not a template parameter)
								const TypeInfo& underlying = gTypeInfo[type_info->type_index_];
								// A type is concrete if:
								// 1. It has struct_info_ (it's a defined struct/class), OR
								// 2. It's not Type::UserDefined (i.e., it's a built-in type like int, bool, float)
								// Template parameters are stored as Type::UserDefined without struct_info_,
								// so this check correctly excludes them while accepting concrete types.
								if (underlying.struct_info_ != nullptr || 
								    underlying.type_ != Type::UserDefined) {
									// It's a type alias to a concrete type (struct or built-in)
									is_concrete_type = true;
									FLASH_LOG(Templates, Debug, "Identifier '", id.name(), "' is a type alias to concrete type, falling through to type parsing");
								}
							}
						}
					} else if (std::holds_alternative<FunctionCallNode>(expr)) {
						// FunctionCallNode represents a function call expression like test_func<T>()
						// This is NOT a type - it's a non-type template argument (the result of calling a function)
						// Previously this code incorrectly treated FunctionCallNode with template arguments as a type,
						// but that was wrong. A function call with template arguments (e.g., test_func<T>()) is still
						// a function call, not a type. The function returns a value, and that value is used as
						// the non-type template argument.
						// DO NOT set is_concrete_type = true here - let it be accepted as a dependent expression.
						FLASH_LOG(Templates, Debug, "FunctionCallNode - treating as function call expression, not a type");
					} else if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
						// QualifiedIdentifierNode can represent a namespace-qualified type like ns::Inner
						// or a template instantiation like ns::Inner<int> (when the template has already been
						// instantiated during expression parsing).
						const auto& qual_id = std::get<QualifiedIdentifierNode>(expr);
						// Build the qualified name and check if it exists in gTypesByName
						std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
						auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_name));
						if (type_it != gTypesByName.end()) {
							const TypeInfo* type_info = type_it->second;
							if (type_info->struct_info_ != nullptr) {
								is_concrete_type = true;
								FLASH_LOG(Templates, Debug, "QualifiedIdentifierNode '", qualified_name, "' is a concrete type, falling through to type parsing");
							}
						}
					}
					
					// If it's a concrete type, restore and let type parsing handle it
					if (is_concrete_type) {
						restore_token_position(arg_saved_pos);
						// Fall through to type parsing below
					} else {
						// Check if this is a template parameter that has a type substitution available
						// This enables variable templates inside function templates to work correctly:
						// e.g., __is_ratio_v<_R1> where _R1 should be substituted with ratio<1,2>
						bool substituted_type_param = false;
						bool finished_parsing = false;  // Track if we consumed '>' and should break
						std::string_view param_name_to_check;
						
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							const auto& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
							param_name_to_check = StringTable::getStringView(tparam_ref.param_name());
						} else if (std::holds_alternative<IdentifierNode>(expr)) {
							const auto& id = std::get<IdentifierNode>(expr);
							param_name_to_check = id.name();
						}
						
						if (!param_name_to_check.empty()) {
							// Check if we have a type substitution for this parameter
							for (const auto& subst : template_param_substitutions_) {
								if (subst.is_type_param && subst.param_name == param_name_to_check) {
									// Found a type substitution! Use it instead of creating a dependent arg
									FLASH_LOG(Templates, Debug, "Found type substitution for parameter '", 
									          param_name_to_check, "' -> ", subst.substituted_type.toString());
									
									TemplateTypeArg substituted_arg = subst.substituted_type;
									
									// Check for pack expansion (...)
									if (peek() == "..."_tok) {
										advance(); // consume '...'
										substituted_arg.is_pack = true;
										FLASH_LOG(Templates, Debug, "Marked substituted type as pack expansion");
									}
									
									template_args.push_back(substituted_arg);
									if (out_type_nodes && expr_result.node().has_value()) {
										out_type_nodes->push_back(*expr_result.node());
									}
									discard_saved_token(arg_saved_pos);
									substituted_type_param = true;
									
									// Handle next token
									if (peek() == ">>"_tok) {
										split_right_shift_token();
									}
									if (peek() == ">"_tok) {
										advance();
										finished_parsing = true;
									} else if (peek() == ","_tok) {
										advance();
									}
									break;  // Break from the for loop
								}
							}
						}
						
						if (substituted_type_param) {
							if (finished_parsing) {
								break;  // Break from the outer while loop - we're done
							}
							continue;  // Continue to next template argument
						}
						
						FLASH_LOG(Templates, Debug, "Accepting dependent expression as template argument");
						// Successfully parsed a dependent expression
						// Create a dependent template argument
						// IMPORTANT: For template parameter references (like T in is_same<T, T>),
						// this should be a TYPE argument, not a VALUE argument!
						// Try to get the type_index for the template parameter so pattern matching can detect reused parameters
						TemplateTypeArg dependent_arg;
						dependent_arg.base_type = Type::UserDefined;  // Template parameter is a user-defined type placeholder
						dependent_arg.type_index = 0;  // Default, will try to look up
						dependent_arg.is_value = false;  // This is a TYPE parameter, not a value
						dependent_arg.is_dependent = true;
						
						// Try to get the type_index for template parameter references
						// For TemplateParameterReferenceNode or IdentifierNode that refers to a template parameter
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
						const auto& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
						StringHandle param_name = tparam_ref.param_name();
						// Store the dependent name for placeholder type generation
						dependent_arg.dependent_name = param_name;
						// Look up the template parameter type in gTypesByName
						auto type_it = gTypesByName.find(param_name);
						if (type_it != gTypesByName.end()) {
							dependent_arg.type_index = type_it->second->type_index_;
							FLASH_LOG(Templates, Debug, "  Found type_index=", dependent_arg.type_index,
							          " for template parameter '", StringTable::getStringView(param_name), "'");
						}
					} else if (std::holds_alternative<IdentifierNode>(expr)) {
						const auto& id = std::get<IdentifierNode>(expr);
						// Store the dependent name for placeholder type generation
						dependent_arg.dependent_name = StringTable::getOrInternStringHandle(id.name());
						// Check if this identifier is a template parameter by looking it up
						auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(id.name()));
						if (type_it != gTypesByName.end()) {
							dependent_arg.type_index = type_it->second->type_index_;
							FLASH_LOG(Templates, Debug, "  Found type_index=", dependent_arg.type_index,
							          " for identifier '", id.name(), "'");
						} else {
							// Check if this identifier is a template alias (like void_t)
							// Template aliases may resolve to concrete types even when used with dependent arguments
							auto alias_opt = gTemplateRegistry.lookup_alias_template(id.name());
							if (alias_opt.has_value()) {
								const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
								Type target_type = alias_node.target_type_node().type();
								
								// If the alias always resolves to a concrete type (like void_t -> void),
								// use that concrete type instead of marking as dependent
								if (target_type != Type::UserDefined && target_type != Type::Struct) {
									FLASH_LOG(Templates, Debug, "Template alias '", id.name(), 
									          "' resolves to concrete type ", static_cast<int>(target_type));
									dependent_arg.base_type = target_type;
									dependent_arg.is_dependent = false;  // Not dependent - resolves to concrete type
								}
							}
						}
					}
					
						// Check for pack expansion (...)
						if (peek() == "..."_tok) {
							advance(); // consume '...'
							dependent_arg.is_pack = true;
							FLASH_LOG(Templates, Debug, "Marked dependent expression as pack expansion");
						}
						
						template_args.push_back(dependent_arg);
						
						// Store the expression node for deferred base class resolution
						// This is needed so that type trait expressions like __has_trivial_destructor(T)
						// can be properly substituted and evaluated during template instantiation
						if (out_type_nodes && expr_result.node().has_value()) {
							out_type_nodes->push_back(*expr_result.node());
						}
						
						discard_saved_token(arg_saved_pos);
						
						// Check for ',' or '>' after the expression (or after pack expansion)
						// Phase 5: Handle >> token splitting for nested templates
						if (peek() == ">>"_tok) {
							split_right_shift_token();
						}
						
						if (peek() == ">"_tok) {
							advance(); // consume '>'
							break;
						}
						
						if (peek() == ","_tok) {
							advance(); // consume ','
							continue;
						}
					}
				}
				}  // End of else block for !is_array_subscript
			}

			// Expression is not a numeric literal or evaluable constant - fall through to type parsing
		}

		// Expression parsing failed or wasn't a numeric literal - try parsing a type
		restore_token_position(arg_saved_pos);
		auto type_result = parse_type_specifier();
		if (type_result.is_error() || !type_result.node().has_value()) {
			// Neither type nor expression parsing worked
			FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments failed to parse type or expression (might be comparison operator)");
			restore_token_position(saved_pos);
			last_failed_template_arg_parse_handle_ = saved_pos;
			return std::nullopt;
		}

		// Successfully parsed a type
		TypeSpecifierNode& type_node = type_result.node()->as<TypeSpecifierNode>();
		
		MemberPointerKind member_pointer_kind = MemberPointerKind::None;

		// Detect pointer-to-member declarator: ClassType::*
		if (peek().is_identifier()) {
			SaveHandle member_saved_pos = save_token_position();
			advance(); // consume class/struct identifier
			if (peek() == "::"_tok) {
				advance(); // consume '::'
				if (peek() == "*"_tok) {
					advance(); // consume '*'
					member_pointer_kind = MemberPointerKind::Object;
					type_node.add_pointer_level(CVQualifier::None);
				} else {
					restore_token_position(member_saved_pos);
				}
			} else {
				restore_token_position(member_saved_pos);
			}
		}

		// Check for postfix cv-qualifiers: T const, T volatile, T const volatile
		// This is the C++ postfix const/volatile syntax used in standard library headers
		// (e.g., "template<typename T> struct is_const<T const>" from <type_traits>)
		while (!peek().is_eof()) {
			if (peek() == "const"_tok) {
				advance();
				type_node.add_cv_qualifier(CVQualifier::Const);
			} else if (peek() == "volatile"_tok) {
				advance();
				type_node.add_cv_qualifier(CVQualifier::Volatile);
			} else {
				break;
			}
		}

		// Check for pointer-to-array syntax: T(*)[] or T(*)[N]
		// AND function pointer/reference syntax: T(&)() or T(*)() or T(&&)()
		// This is the syntax used for pointer-to-array types and function types in template arguments
		// e.g., is_convertible<_FromElementType(*)[], _ToElementType(*)[]>
		// e.g., declval<_Xp(&)()>() - function reference type
		if (peek() == "("_tok) {
			SaveHandle paren_saved_pos = save_token_position();
			advance(); // consume '('

			// Skip optional calling convention before ptr-operator, consistent with
			// parse_declarator() and parse_type_and_name() which call parse_calling_convention()
			// at the same position. Handles patterns like: _Ret (__cdecl _Arg0::*)(_Types...)
			parse_calling_convention();

			// Detect what's inside: *, &, &&, or _Class::* (member pointer)
			bool is_ptr = false;
			bool is_lvalue_ref = false;
			bool is_rvalue_ref = false;
			bool is_member_ptr = false;

			if (!peek().is_eof()) {
				if (peek() == "*"_tok) {
					is_ptr = true;
					advance(); // consume '*'
				} else if (peek() == "&&"_tok) {
					is_rvalue_ref = true;
					advance(); // consume '&&'
				} else if (peek() == "&"_tok) {
					is_lvalue_ref = true;
					advance(); // consume '&'
				} else if (peek().is_identifier()) {
					// Check for member pointer syntax: _Class::*
					SaveHandle member_check_pos = save_token_position();
					advance(); // consume class name
					if (peek() == "::"_tok) {
						advance(); // consume '::'
						if (peek() == "*"_tok) {
							advance(); // consume '*'
							is_member_ptr = true;
							is_ptr = true;
							discard_saved_token(member_check_pos);
						} else {
							restore_token_position(member_check_pos);
						}
					} else {
						restore_token_position(member_check_pos);
					}
				}
			}
			
			if ((is_ptr || is_lvalue_ref || is_rvalue_ref) &&
			    peek() == ")"_tok) {
				advance(); // consume ')'
				
				// Check what follows: [] for array or () for function
				if (peek() == "["_tok) {
					// Pointer-to-array: T(*)[] or T(*)[N]
					if (is_ptr) {
						advance(); // consume '['
						
						// Optional array size
						std::optional<size_t> ptr_array_size;
						if (peek() != "]"_tok) {
							auto size_result = parse_expression(0, ExpressionContext::TemplateArgument);
							if (!size_result.is_error() && size_result.node().has_value()) {
								if (auto const_size = try_evaluate_constant_expression(*size_result.node())) {
									if (const_size->value >= 0) {
										ptr_array_size = static_cast<size_t>(const_size->value);
									}
								}
							}
						}
						
						if (consume("]"_tok)) {
							// Successfully parsed T(*)[] or T(*)[N]
							// This is a pointer to array - add pointer level and mark as array
							type_node.add_pointer_level(CVQualifier::None);
							type_node.set_array(true, ptr_array_size);
							discard_saved_token(paren_saved_pos);
							FLASH_LOG(Parser, Debug, "Parsed pointer-to-array type T(*)[]");
						} else {
							restore_token_position(paren_saved_pos);
						}
					} else {
						// References to arrays are less common, restore for now
						restore_token_position(paren_saved_pos);
					}
				} else if (peek() == "("_tok) {
					// Function pointer/reference/member: T(&)(...) or T(*)(...) or T(&&)(...) or T(Class::*)(...)
					advance(); // consume '('
					
					// Parse parameter list using shared helper
					std::vector<Type> param_types;
					bool param_parse_ok = parse_function_type_parameter_list(param_types);
					
					if (!param_parse_ok) {
						// Parsing failed - restore position
						restore_token_position(paren_saved_pos);
					}
					
					if (param_parse_ok && peek() == ")"_tok) {
						advance(); // consume ')'
						
						// Parse trailing cv-qualifiers, ref-qualifiers, and noexcept
						// For member function pointers: _Res (_Class::*)(_ArgTypes...) const & noexcept
						// For function pointers: _Res(*)(_ArgTypes...) noexcept(_NE)
						// For function references: _Res(&)(_ArgTypes...) noexcept
						bool sig_is_const = false;
						bool sig_is_volatile = false;
						while (!peek().is_eof()) {
							if ((is_member_ptr) && peek() == "const"_tok) {
								sig_is_const = true;
								advance();
							} else if ((is_member_ptr) && peek() == "volatile"_tok) {
								sig_is_volatile = true;
								advance();
							} else if (is_member_ptr && (peek() == "&"_tok || peek() == "&&"_tok)) {
								advance();
							} else if (peek() == "noexcept"_tok) {
								advance(); // consume 'noexcept'
								if (peek() == "("_tok) {
									skip_balanced_parens();
								}
							} else {
								break;
							}
						}
						
						// Successfully parsed function reference/pointer type!
						FunctionSignature func_sig;
						func_sig.return_type = type_node.type();
						func_sig.parameter_types = std::move(param_types);
						func_sig.is_const = sig_is_const;
						func_sig.is_volatile = sig_is_volatile;
						
						if (is_ptr) {
							type_node.add_pointer_level(CVQualifier::None);
						}
						type_node.set_function_signature(func_sig);
						
						if (is_member_ptr) {
							// Member function pointer - mark as member pointer
							type_node.set_member_class_name(StringHandle{});
						}
						
						if (is_lvalue_ref) {
							type_node.set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference
						} else if (is_rvalue_ref) {
							type_node.set_reference_qualifier(ReferenceQualifier::RValueReference);   // rvalue reference
						}
						
						discard_saved_token(paren_saved_pos);
						FLASH_LOG(Parser, Debug, "Parsed function ", 
						          is_member_ptr ? "member pointer" : (is_ptr ? "pointer" : (is_rvalue_ref ? "rvalue ref" : "lvalue ref")),
						          " type in template argument");
					} else {
						// Parsing failed - restore position
						restore_token_position(paren_saved_pos);
					}
				} else {
					// Just (*) or (&) or (&&) without [] or () - restore
					restore_token_position(paren_saved_pos);
				}
			} else {
				// Not (*, &, &&, or Class::*) - could be a bare function type: _Res(_ArgTypes...)
				// Try to parse the contents as a parameter list
				// Save position within the parens
				SaveHandle func_type_saved_pos = save_token_position();
				bool is_bare_func_type = false;
				std::vector<Type> func_param_types;
				
				// Try to parse as function parameter list using shared helper
				bool param_parse_ok = parse_function_type_parameter_list(func_param_types);
				
				if (param_parse_ok && peek() == ")"_tok) {
					advance(); // consume ')'
					is_bare_func_type = true;
					
					// Successfully parsed bare function type
					FunctionSignature func_sig;
					func_sig.return_type = type_node.type();
					func_sig.parameter_types = std::move(func_param_types);
					type_node.set_function_signature(func_sig);
					
					// Consume trailing noexcept or noexcept(expr) if present
					skip_noexcept_specifier();
					
					discard_saved_token(func_type_saved_pos);
					discard_saved_token(paren_saved_pos);
					FLASH_LOG(Parser, Debug, "Parsed bare function type in template argument");
				}
				
				if (!is_bare_func_type) {
					restore_token_position(func_type_saved_pos);
					restore_token_position(paren_saved_pos);
				}
			}
		}

		// Apply pointer/reference modifiers to the type
		consume_pointer_ref_modifiers(type_node);

		// Check for array declarators (e.g., T[], T[N])
		bool is_array_type = false;
		std::optional<size_t> parsed_array_size;
		while (peek() == "["_tok) {
			is_array_type = true;
			advance(); // consume '['

			// Optional size expression
			if (peek() != "]"_tok) {
				auto size_result = parse_expression(0, ExpressionContext::TemplateArgument);
				if (size_result.is_error() || !size_result.node().has_value()) {
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}

				if (auto const_size = try_evaluate_constant_expression(*size_result.node())) {
					if (const_size->value >= 0) {
						parsed_array_size = static_cast<size_t>(const_size->value);
					}
				} else {
					// Size expression present but not evaluable (e.g., template parameter N)
					// Use SIZE_MAX as a sentinel to indicate "sized array with unknown size"
					parsed_array_size = SIZE_MAX;
				}
			}

			if (!consume("]"_tok)) {
				restore_token_position(saved_pos);
				last_failed_template_arg_parse_handle_ = saved_pos;
				return std::nullopt;
			}
		}

		if (is_array_type) {
			type_node.set_array(true, parsed_array_size);
		}

		// Check for pack expansion (...)
		bool is_pack_expansion = false;
		if (peek() == "..."_tok) {
			advance(); // consume '...'
			is_pack_expansion = true;
		}

		// Create TemplateTypeArg from the fully parsed type
		TemplateTypeArg arg(type_node);
		arg.is_pack = is_pack_expansion;
		arg.member_pointer_kind = member_pointer_kind;
		
		// Check if this type is dependent (contains template parameters)
		// A type is dependent if:
		// 1. Its type name is in current_template_param_names_ (it IS a template parameter), AND
		//    we're NOT in SFINAE context (during SFINAE, template params are substituted)
		// 2. Its is_incomplete_instantiation_ flag is set (composite type with unresolved template parameters)
		// 3. It's a UserDefined type with type_index=0 (placeholder)
		FLASH_LOG_FORMAT(Templates, Debug, "Checking dependency for template argument: type={}, type_index={}, in_sfinae_context={}", 
		                 static_cast<int>(type_node.type()), type_node.type_index(), in_sfinae_context_);
		if (type_node.type() == Type::UserDefined) {
			// BUGFIX: Use the original token value instead of looking up via type_index
			// When template parameters are parsed, they may have type_index=0 (void),
			// which causes incorrect dependency checks. The token value is always correct.
			std::string_view type_name = type_node.token().value();
			FLASH_LOG_FORMAT(Templates, Debug, "UserDefined type, type_name from token: {}", type_name);
			
			// Also get the full type name from gTypeInfo for composite/qualified types
			// The token may only have the base name (e.g., "remove_reference")
			// but gTypeInfo has the full name (e.g., "remove_reference__Tp::type")
			std::string_view full_type_name;
			TypeIndex idx = type_node.type_index();
			if (idx < gTypeInfo.size()) {
				full_type_name = StringTable::getStringView(gTypeInfo[idx].name());
				FLASH_LOG_FORMAT(Templates, Debug, "Full type name from gTypeInfo: {}", full_type_name);
			}
			
			// Fallback to gTypeInfo lookup only if token is empty
			if (type_name.empty()) {
				type_name = full_type_name;
				FLASH_LOG(Templates, Debug, "Fallback: using full type name");
			}
			
			if (!type_name.empty()) {
				auto matches_identifier = [](std::string_view haystack, std::string_view needle) {
					size_t pos = haystack.find(needle);
					auto is_ident_char = [](char ch) {
						return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
					};
					while (pos != std::string_view::npos) {
						bool start_ok = (pos == 0) || !is_ident_char(haystack[pos - 1]);
						bool end_ok = (pos + needle.size() >= haystack.size()) || !is_ident_char(haystack[pos + needle.size()]);
						if (start_ok && end_ok) {
							return true;
						}
						pos = haystack.find(needle, pos + 1);
					}
					return false;
				};
				
				// Check if this is a template parameter name
				// During SFINAE context (re-parsing), template parameters are substituted with concrete types
				// so we should NOT mark them as dependent
				bool is_template_param = false;
				if (!in_sfinae_context_) {
					for (const auto& param_name : current_template_param_names_) {
						std::string_view param_sv = StringTable::getStringView(param_name);
						if (type_name == param_sv || matches_identifier(type_name, param_sv)) {
							is_template_param = true;
							break;
						}
					}
				}
				
				if (is_template_param || (idx < gTypeInfo.size() && gTypeInfo[idx].is_incomplete_instantiation_)) {
					arg.is_dependent = true;
					arg.dependent_name = StringTable::getOrInternStringHandle(type_name);
					FLASH_LOG_FORMAT(Templates, Debug, "Template argument is dependent (type name: {})", type_name);
				} else if (!in_sfinae_context_) {
					// Also check the full type name from gTypeInfo for composite/qualified types
					std::string_view check_name = !full_type_name.empty() ? full_type_name : type_name;
					
					// Check if this is a qualified identifier (contains ::) which might be a member access
					// If so, check if the base part contains any template parameter
					size_t scope_pos = check_name.find("::");
					if (scope_pos != std::string_view::npos) {
						// This is a qualified identifier - extract the base part (before ::)
						std::string_view base_part = check_name.substr(0, scope_pos);
						
						for (const auto& param_name : current_template_param_names_) {
							std::string_view param_sv = StringTable::getStringView(param_name);
							// Check both as standalone identifier AND as substring
							// BUT only check substring if the base_part contains underscores (mangled names)
							// This prevents false positives where common substrings match accidentally
							bool contains_param = matches_identifier(base_part, param_sv);
							if (!contains_param && base_part.find('_') != std::string_view::npos) {
								// For mangled names like "remove_reference__Tp", check substring
								contains_param = base_part.find(param_sv) != std::string_view::npos;
							}
							if (contains_param) {
								arg.is_dependent = true;
								arg.dependent_name = StringTable::getOrInternStringHandle(check_name);
								FLASH_LOG_FORMAT(Templates, Debug, "Template argument marked dependent due to qualified identifier with template param: {}", check_name);
								break;
							}
						}
					}
				}
			}
			
			// Also check for type_index=0 as a fallback indicator of dependent types
			if (!arg.is_dependent && type_node.type_index() == 0) {
				arg.is_dependent = true;
				FLASH_LOG(Templates, Debug, "Template argument is dependent (placeholder with type_index=0)");
			}
		}
		
		// Also check Struct types - if this is a template class that was parsed with dependent arguments,
		// the instantiation was skipped and we got back the primary template type
		// In a template body, if the struct is a registered template and we're using template params, it's dependent
		// BUT: If this is a template template argument (passing a template class as an argument), it's NOT dependent
		// even if we're in a template body. A template class like HasType used as a template argument is concrete.
		if (!arg.is_dependent && type_node.type() == Type::Struct && parsing_template_body_ && !in_sfinae_context_) {
			TypeIndex idx = type_node.type_index();
			if (idx < gTypeInfo.size()) {
				std::string_view type_name = StringTable::getStringView(gTypeInfo[idx].name());
				// Check if this is a template primary (not an instantiation which would have underscores)
				auto template_opt = gTemplateRegistry.lookupTemplate(type_name);
				if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
					// This struct type is a template primary
					// Check if type_name contains any current template parameters
					// If not, it's a concrete template class being used as a template template argument
					bool contains_template_param = false;
					for (const auto& param_name : current_template_param_names_) {
						if (type_name == param_name) {
							contains_template_param = true;
							break;
						}
					}
					
					// Only mark as dependent if the type name itself is a template parameter
					// A template class like HasType being used as an argument is NOT dependent
					if (contains_template_param) {
						FLASH_LOG_FORMAT(Templates, Debug, "Template argument {} is primary template matching template param - marking as dependent", type_name);
						arg.is_dependent = true;
						arg.dependent_name = StringTable::getOrInternStringHandle(type_name);
					} else {
						FLASH_LOG_FORMAT(Templates, Debug, "Template argument {} is a concrete template class (used as template template arg) - NOT dependent", type_name);
					}
				}
			}
		}
		
		template_args.push_back(arg);
		if (out_type_nodes) {
			out_type_nodes->push_back(*type_result.node());
		}

		// Check for ',' or '>'
		if (peek().is_eof()) {
			FLASH_LOG(Parser, Error, "parse_explicit_template_arguments unexpected end of tokens");
			restore_token_position(saved_pos);
			last_failed_template_arg_parse_handle_ = saved_pos;
			return std::nullopt;
		}

		FLASH_LOG_FORMAT(Parser, Debug, "After adding type argument, peek_token={}", std::string(peek_info().value()));
		
		// Phase 5: Handle >> token splitting for nested templates
		// C++20 maximal munch: Foo<Bar<int>> should parse as Foo<Bar<int> >
		if (peek() == ">>"_tok) {
			FLASH_LOG(Parser, Debug, "Encountered >> token, splitting for nested template");
			split_right_shift_token();
		}
		
		if (peek() == ">"_tok) {
			advance(); // consume '>'
			break;
		}

		if (peek() == ","_tok) {
			advance(); // consume ','
			continue;
		}

		// Unexpected token
		FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments unexpected token: '", peek_info().value(), "' (might be comparison operator)");
		restore_token_position(saved_pos);
		last_failed_template_arg_parse_handle_ = saved_pos;
		return std::nullopt;
	}

	// Success - discard saved position
	discard_saved_token(saved_pos);
	last_failed_template_arg_parse_handle_ = SIZE_MAX;  // Clear failure marker on success
	return template_args;
}

// Phase 1: C++20 Template Argument Disambiguation
// Check if '<' at current position could start template arguments without consuming tokens.
// This implements lookahead to disambiguate template argument lists from comparison operators.
// Returns true if parse_explicit_template_arguments() would succeed at this position.
bool Parser::could_be_template_arguments() {
	FLASH_LOG(Parser, Debug, "could_be_template_arguments: checking if '<' starts template arguments");
	
	// Quick check: must have '<' at current position
	if (peek() != "<"_tok) {
		return false;
	}
	
	// Save position BEFORE attempting to parse template arguments
	// This ensures we restore position even on success, making this truly non-consuming
	auto saved_pos = save_token_position();
	
	// Try to parse template arguments speculatively
	auto template_args = parse_explicit_template_arguments();
	
	// Always restore position - this makes the function non-consuming
	restore_token_position(saved_pos);
	
	// Return true if parsing would succeed
	return template_args.has_value();
}

// Phase 2: Unified Qualified Identifier Parser (Sprint 3-4)
// Consolidates all qualified identifier parsing into a single, consistent code path.
// This function parses patterns like: A::B::C or ns::Template<Args>::member
std::optional<QualifiedIdParseResult> Parser::parse_qualified_identifier_with_templates() {
	FLASH_LOG(Parser, Debug, "parse_qualified_identifier_with_templates: starting");
	
	// Must start with an identifier
	if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
		return std::nullopt;
	}
	
	std::vector<StringHandle> namespaces;
	Token final_identifier = current_token_;
	advance(); // consume first identifier
	
	// Check if followed by ::
	if (current_token_.kind().is_eof() || current_token_.value() != "::") {
		// Single identifier, no qualification - not a qualified identifier
		// Restore position for caller to handle
		return std::nullopt;
	}
	
	// Collect namespace parts
	while (current_token_.value() == "::") {
		// Current identifier becomes a namespace part - intern into string table
		namespaces.emplace_back(final_identifier.handle());
		advance(); // consume ::
		
		// Get next identifier
		if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
			// Error: expected identifier after ::
			return std::nullopt;
		}
		final_identifier = current_token_;
		advance(); // consume the identifier
	}
	
	// At this point: current_token_ is the token after final identifier
	// Check for template arguments: A::B::C<Args>
	if (current_token_.value() == "<") {
		FLASH_LOG_FORMAT(Parser, Debug, "parse_qualified_identifier_with_templates: parsing template args for '{}'", 
		                final_identifier.value());
		auto template_args = parse_explicit_template_arguments();
		if (template_args.has_value()) {
			FLASH_LOG_FORMAT(Parser, Debug, "parse_qualified_identifier_with_templates: parsed {} template args", 
			                template_args->size());
			return QualifiedIdParseResult(namespaces, final_identifier, *template_args);
		}
	}
	
	// No template arguments or parsing failed
	return QualifiedIdParseResult(namespaces, final_identifier);
}

// Try to instantiate a template with explicit template arguments
