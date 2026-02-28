ParseResult Parser::parse_bitfield_width(std::optional<size_t>& out_width, std::optional<ASTNode>* out_expr) {
	if (peek() != ":"_tok) {
		return ParseResult::success();
	}

	advance(); // consume ':'
	auto width_result = parse_expression(4, ExpressionContext::Normal); // Stop before assignment operators.
	if (width_result.is_error()) {
		return width_result;
	}
	if (width_result.node().has_value()) {
		ConstExpr::EvaluationContext ctx(gSymbolTable);
		auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
		if (!eval_result.success() || eval_result.as_int() < 0) {
			// If caller wants deferred evaluation and the expression is not a plain literal,
			// defer it (e.g., template non-type parameter).
			if (out_expr != nullptr) {
				*out_expr = *width_result.node();
				return ParseResult::success();
			}
			return ParseResult::error("Bitfield width must be a non-negative integral constant expression", peek_info());
		}
		out_width = static_cast<size_t>(eval_result.as_int());
	}
	return ParseResult::success();
}

// Parse template declaration: template<typename T> ...
// Also handles explicit template instantiation: template void Func<int>(); or template class Container<int>;
ParseResult Parser::parse_template_declaration() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'template' keyword
	if (!consume("template"_tok)) {
		return ParseResult::error("Expected 'template' keyword", peek_info());
	}

	// Check if this is an explicit template instantiation (no '<' after 'template')
	// Syntax: template class Container<int>;           // Explicit instantiation definition
	//         extern template class Container<int>;    // Explicit instantiation declaration
	//         template void Container<int>::set(int);  // Explicit member function instantiation
	if (peek() != "<"_tok) {
		// Check if this is an extern declaration (suppresses implicit instantiation)
		bool is_extern = false;
		if (peek() == "extern"_tok) {
			is_extern = true;
			advance(); // consume 'extern'
			
			// Re-check that we still have 'template'
			if (peek() != "template"_tok) {
				return ParseResult::error("Expected 'template' after 'extern'", current_token_);
			}
			advance(); // consume second 'template'
		}
		
		// Now peek at what type of explicit instantiation this is
		if (peek().is_eof()) {
			return ParseResult::error("Unexpected end after 'template' keyword", current_token_);
		}
		
		std::string_view next_token = peek_info().value();
		
		// Handle: template class/struct Name<Args>;
		if (next_token == "class" || next_token == "struct") {
			advance(); // consume 'class' or 'struct'
			
			// Parse the template name and arguments
			if (peek().is_eof()) {
				return ParseResult::error("Expected template name after 'template class'", current_token_);
			}
			
			Token name_token = peek_info();
			advance(); // consume template name
			
			// Handle namespace-qualified names (e.g., __cxx11::numpunct)
			while (peek() == "::"_tok) {
				advance(); // consume '::'
				if (peek().is_eof()) {
					return ParseResult::error("Expected identifier after '::'", current_token_);
				}
				name_token = peek_info();
				advance(); // consume next identifier
			}
			
			// Parse template arguments: Name<Args>
			std::optional<std::vector<TemplateTypeArg>> template_args;
			if (peek() == "<"_tok) {
				template_args = parse_explicit_template_arguments();
				if (!template_args.has_value()) {
					return ParseResult::error("Failed to parse template arguments in explicit instantiation", current_token_);
				}
			}
			
			// Expect ';'
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after explicit template instantiation", current_token_);
			}
			
			// For explicit instantiation DEFINITION (not extern), force instantiation even in lazy mode
			if (!is_extern && template_args.has_value()) {
				FLASH_LOG(Templates, Debug, "Explicit template instantiation: ", name_token.value());
				
				// Try to instantiate the class template with force_eager=true
				auto instantiated = try_instantiate_class_template(name_token.value(), *template_args, true);
				if (instantiated.has_value()) {
					// Success - the template is now explicitly instantiated
					// Add the instantiated struct to the AST so its member functions get code-generated
					ast_nodes_.push_back(*instantiated);
					FLASH_LOG(Templates, Debug, "Successfully explicitly instantiated: ", name_token.value());
				} else {
					// Template not found or instantiation failed
					FLASH_LOG(Templates, Warning, "Could not explicitly instantiate template: ", name_token.value());
				}
			} else if (is_extern) {
				// extern template - suppresses implicit instantiation
				// For now, we just note it (could be used to optimize away redundant instantiations)
				FLASH_LOG(Templates, Debug, "Extern template declaration (suppresses implicit instantiation): ", name_token.value());
			}
			
			return saved_position.success();
		}
		
		// Handle other explicit instantiations (functions, etc.)
		// For now, just consume until ';'
		FLASH_LOG(Templates, Debug, "Explicit template instantiation (other): skipping");
		while (peek() != ";"_tok) {
			advance();
		}
		if (peek() == ";"_tok) {
			advance(); // consume ';'
		}
		return saved_position.success();
	}

	// Expect '<' to start template parameter list
	// Note: '<' is an operator, not a punctuator
	advance(); // consume '<'

	// Check if this is a template specialization (template<>)
	bool is_specialization = false;
	if (peek() == ">"_tok) {
		is_specialization = true;
		advance(); // consume '>'
	}

	// Parse template parameter list (unless it's a specialization)
	std::vector<ASTNode> template_params;
	if (!is_specialization) {
		auto param_list_result = parse_template_parameter_list(template_params);
		if (param_list_result.is_error()) {
			return param_list_result;
		}

		// Expect '>' to end template parameter list
		// Note: '>' is an operator, not a punctuator
		if (peek() != ">"_tok) {
			return ParseResult::error("Expected '>' after template parameter list", current_token_);
		}
		advance(); // consume '>'
	}

	// Check if this is a nested template specialization (for template member functions of template classes)
	// Pattern: template<> template<> ReturnType ClassName<Args>::FunctionName<Args>(...)
	if (is_specialization && peek() == "template"_tok) {
		
		// Recursively parse the inner template<>
		// This handles: template<> template<> int Processor<int>::process<SmallStruct>(...)
		auto inner_result = parse_template_declaration();
		if (inner_result.is_error()) {
			return inner_result;
		}
		
		// The inner parse_template_declaration handles the rest, so we're done
		return saved_position.success();
	}

	// Now parse what comes after the template parameter list
	// We support function templates and class templates

	// Add template parameters to the type system temporarily using RAII scope guard (Phase 6)
	// This allows them to be used in the function body or class members
	FlashCpp::TemplateParameterScope template_scope;
	std::vector<StringHandle> template_param_names;
	bool has_packs = false;  // Track if any parameter is a pack
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
			// Add ALL template parameters to the name list (Type, NonType, and Template)
			// This allows them to be recognized when referenced in the template body
			template_param_names.push_back(tparam.nameHandle());  // string_view from Token
			
			// Check if this is a parameter pack
			has_packs |= tparam.is_variadic();
			
			// Type parameters and Template template parameters need TypeInfo registration
			// This allows them to be recognized during type parsing (e.g., Container<T>)
			if (tparam.kind() == TemplateParameterKind::Type || tparam.kind() == TemplateParameterKind::Template) {
				// Register the template parameter as a user-defined type temporarily
				// Create a TypeInfo entry for the template parameter
				auto& type_info = gTypeInfo.emplace_back(tparam.nameHandle(), tparam.kind() == TemplateParameterKind::Template ? Type::Template : Type::UserDefined, gTypeInfo.size(), 0); // Do we need a correct size here?
				gTypesByName.emplace(type_info.name(), &type_info);
				template_scope.addParameter(&type_info);  // RAII cleanup on all return paths
			}
		}
	}
	
	// Set the flag to enable fold expression parsing if we have parameter packs
	bool saved_has_packs = has_parameter_packs_;
	has_parameter_packs_ = has_packs;
	
	// Set template parameter context EARLY, before any code that might call parse_type_specifier()
	// This includes variable template detection below which needs to recognize template params
	// like _Int in return types: typename tuple_element<_Int, pair<_Tp1, _Tp2>>::type&
	current_template_param_names_ = template_param_names;
	parsing_template_body_ = true;

	// Check if this is a nested template (member function template of a class template)
	// Pattern: template<typename T> template<typename U> ReturnType Class<T>::method(U u) { ... }
	// At this point, outer template params are registered, so the inner parse can see them.
	if (peek() == "template"_tok) {
		// Helper to clean up template state before early returns from this block.
		// parsing_template_body_, current_template_param_names_, and has_parameter_packs_
		// were set above and would normally be cleaned up at end-of-function (~line 3805).
		auto cleanup_template_state = [this, saved_has_packs]() {
			current_template_param_names_.clear();
			parsing_template_body_ = false;
			has_parameter_packs_ = saved_has_packs;
		};

		auto inner_saved = save_token_position();
		advance(); // consume inner 'template'
		if (peek() == "<"_tok) {
			advance(); // consume '<'

			// Parse inner template parameters
			std::vector<ASTNode> inner_template_params;
			auto inner_param_result = parse_template_parameter_list(inner_template_params);
			if (inner_param_result.is_error()) {
				// Fallback: skip the rest (for standard headers that use unsupported features)
				restore_token_position(inner_saved);
				advance(); // re-consume 'template'
				skip_template_arguments();
				while (!peek().is_eof()) {
					if (peek() == "{"_tok) { skip_balanced_braces(); cleanup_template_state(); return saved_position.success(); }
					else if (peek() == ";"_tok) { advance(); cleanup_template_state(); return saved_position.success(); }
					else if (peek() == "("_tok) { skip_balanced_parens(); }
					else { advance(); }
				}
				cleanup_template_state();
				return saved_position.success();
			}

			if (peek() != ">"_tok) {
				// Failed to parse inner template params - restore and fall through to skip
				restore_token_position(inner_saved);
				advance(); // re-consume 'template'
				skip_template_arguments();
				while (!peek().is_eof()) {
					if (peek() == "{"_tok) { skip_balanced_braces(); cleanup_template_state(); return saved_position.success(); }
					else if (peek() == ";"_tok) { advance(); cleanup_template_state(); return saved_position.success(); }
					else if (peek() == "("_tok) { skip_balanced_parens(); }
					else { advance(); }
				}
				cleanup_template_state();
				return saved_position.success();
			}
			advance(); // consume '>'

			// Extract inner template parameter names
			std::vector<StringHandle> inner_template_param_names;
			for (const auto& param : inner_template_params) {
				if (param.is<TemplateParameterNode>()) {
					inner_template_param_names.push_back(param.as<TemplateParameterNode>().nameHandle());
				}
			}

			discard_saved_token(inner_saved);

			// Manually parse the nested template out-of-line definition.
			// We skip to find: ReturnType ClassName<Args>::FunctionName(params) { body }
			// and extract the class name, function name, and body position.
			// We DON'T call try_parse_out_of_line_template_member because its save/restore
			// logic conflicts with the nested template parameter scope.
			std::string_view nested_class_name;
			Token nested_func_name_token;
			bool found_nested_def = false;

			// Skip return type and everything up to ClassName<...>::FunctionName(
			// Strategy: scan tokens looking for the pattern: identifier < ... > :: identifier
			// We take the LAST such match before '(' to avoid misidentifying qualified
			// return types (e.g. typename Container<T>::value_type) as the class::function pattern.
			{
				Token last_ident;
				while (!peek().is_eof()) {
					if (peek().is_identifier()) {
						last_ident = peek_info();
						advance();
						if (peek() == "<"_tok) {
							// This might be ClassName<T>
							Token class_token = last_ident;
							skip_template_arguments();
							if (peek() == "::"_tok) {
								advance(); // consume '::'
								if (peek().is_identifier()) {
									// Tentatively record this match
									nested_class_name = class_token.value();
									nested_func_name_token = peek_info();
									advance(); // consume function name
									// Handle nested :: for deeper nesting
									while (peek() == "::"_tok) {
										advance();
										if (peek().is_identifier()) {
											nested_class_name = nested_func_name_token.value();
											nested_func_name_token = peek_info();
											advance();
										} else break;
									}
									found_nested_def = true;
									// If '(' follows, this is the actual definition - stop
									if (peek() == "("_tok) {
										break;
									}
									// Otherwise, this was a qualified return type - keep scanning
								} else if (peek_info().value() == "operator") {
									// Handle operator overloads: Class<T>::operator()(...)
									nested_class_name = class_token.value();
									Token operator_keyword = peek_info();
									advance(); // consume 'operator'
									// Consume the operator symbol(s) and build the full name
									std::string_view full_op_name;
									if (peek() == "("_tok) {
										advance(); // consume '('
										if (peek() == ")"_tok) {
											advance(); // consume ')' -> operator()
										}
										static const std::string op_call = "operator()";
										full_op_name = op_call;
									} else if (peek() == "["_tok) {
										advance(); // consume '['
										if (peek() == "]"_tok) {
											advance(); // consume ']' -> operator[]
										}
										static const std::string op_subscript = "operator[]";
										full_op_name = op_subscript;
									} else if (peek().is_operator() || peek().is_punctuator()) {
										// Build "operator+" etc.
										static std::unordered_map<std::string_view, std::string> op_names;
										auto sym = peek_info().value();
										auto it = op_names.find(sym);
										if (it == op_names.end()) {
											it = op_names.emplace(sym, "operator" + std::string(sym)).first;
										}
										full_op_name = it->second;
										advance(); // consume single-char operator
									} else {
										static const std::string op_default = "operator";
										full_op_name = op_default;
									}
									// Create a token with the full operator name
									nested_func_name_token = Token(Token::Type::Identifier, full_op_name,
										operator_keyword.line(), operator_keyword.column(),
										operator_keyword.file_index());
									found_nested_def = true;
									if (peek() == "("_tok) {
										break;
									}
								}
							}
						}
					} else if (peek() == "("_tok || peek() == "{"_tok || peek() == ";"_tok) {
						break;
					} else {
						advance();
					}
				}
			}

			if (found_nested_def && peek() == "("_tok) {
				// Create a stub function declaration for registration
				auto void_type = emplace_node<TypeSpecifierNode>(Type::Void, TypeQualifier::None, 0, nested_func_name_token);
				auto [func_decl_node, func_decl_ref] = emplace_node_ref<DeclarationNode>(void_type, nested_func_name_token);
				auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(func_decl_ref, nested_func_name_token.value());

				// Skip parameter list
				skip_balanced_parens();
				// Skip trailing specifiers
				FlashCpp::MemberQualifiers quals;
				skip_function_trailing_specifiers(quals);

				// Handle trailing return type: auto Class<T>::method(params) -> RetType
				if (peek() == "->"_tok) {
					advance(); // consume '->'
					auto trailing_type = parse_type_specifier();
					if (trailing_type.node().has_value() && trailing_type.node()->is<TypeSpecifierNode>()) {
						TypeSpecifierNode& trailing_ts = trailing_type.node()->as<TypeSpecifierNode>();
						consume_pointer_ref_modifiers(trailing_ts);
					}
				}

				// Skip trailing requires clause if present
				skip_trailing_requires_clause();

				// Save body position (includes member initializer list for constructors)
				SaveHandle body_start = save_token_position();

				// Handle constructor member initializer list: ClassName<T>::ClassName(...) : init1(x), init2(y) { }
				if (peek() == ":"_tok) {
					advance(); // consume ':'
					// Skip member initializer list entries: name(expr), name(expr), ...
					while (!peek().is_eof()) {
						// Skip initializer name (possibly qualified: typename X<T>::type() or Base<T>(...))
						if (peek() == "typename"_tok) {
							advance(); // consume 'typename'
						}
						// Skip tokens until we find '(' or '{' of the initializer
						while (!peek().is_eof() && peek() != "("_tok && peek() != "{"_tok && peek() != ";"_tok) {
							if (peek() == "<"_tok) {
								skip_template_arguments();
							} else if (peek() == "::"_tok) {
								advance();
							} else {
								advance();
							}
						}
						// Skip the initializer arguments
						if (peek() == "("_tok) {
							skip_balanced_parens();
						} else if (peek() == "{"_tok) {
							// Could be brace-init for a member, or the start of the function body
							// If followed by a comma or another initializer, it's brace-init
							auto check_save = save_token_position();
							skip_balanced_braces();
							if (peek() == ","_tok) {
								// Brace-init member, continue
								discard_saved_token(check_save);
							} else {
								// This was the function body (or end) - restore and break
								restore_token_position(check_save);
								break;
							}
						} else {
							break;
						}
						// Check for more initializers
						if (peek() == ","_tok) {
							advance(); // consume ','
						} else {
							break;
						}
					}
				}

				if (peek() == "{"_tok) {
					skip_balanced_braces();
				} else if (peek() == ";"_tok) {
					advance();
				}

				// Register as out-of-line member with inner template params
				OutOfLineMemberFunction out_of_line_member;
				out_of_line_member.template_params = template_params;
				out_of_line_member.function_node = func_node;
				out_of_line_member.body_start = body_start;
				out_of_line_member.template_param_names = template_param_names;
				out_of_line_member.inner_template_params = inner_template_params;
				out_of_line_member.inner_template_param_names = inner_template_param_names;

				gTemplateRegistry.registerOutOfLineMember(nested_class_name, std::move(out_of_line_member));

				FLASH_LOG(Templates, Debug, "Registered nested template out-of-line member: ",
				          nested_class_name, "::", nested_func_name_token.value(),
				          " (outer params: ", template_params.size(),
				          ", inner params: ", inner_template_params.size(), ")");

				cleanup_template_state();
				return saved_position.success();
			}

			// Fallback: skip remaining tokens
			while (!peek().is_eof()) {
				if (peek() == "{"_tok) { skip_balanced_braces(); cleanup_template_state(); return saved_position.success(); }
				else if (peek() == ";"_tok) { advance(); cleanup_template_state(); return saved_position.success(); }
				else if (peek() == "("_tok) { skip_balanced_parens(); }
				else { advance(); }
			}
			cleanup_template_state();
			return saved_position.success();
		}
		restore_token_position(inner_saved);
	}

	// Check if it's a concept template: template<typename T> concept Name = ...;
	bool is_concept_template = peek() == "concept"_tok;

	// Check if it's an alias template: template<typename T> using Ptr = T*;
	bool is_alias_template = peek() == "using"_tok;

	// Check if it's a class/struct/union template
	bool is_class_template = !peek().is_eof() &&
	                         peek().is_keyword() &&
	                         (peek() == "class"_tok || peek() == "struct"_tok || peek() == "union"_tok);

	// Check if it's a variable template (constexpr, inline, etc. + type + identifier)
	bool is_variable_template = false;
	if (!is_alias_template && !is_class_template && !peek().is_eof()) {
		// Variable templates usually start with constexpr, inline, or a type directly
		// Save position to check
		auto var_check_pos = save_token_position();
		
		// Skip storage class specifiers (constexpr, inline, static, etc.)
		while (peek().is_keyword()) {
			auto kw = peek();
			if (kw == "constexpr"_tok || kw == "inline"_tok || kw == "static"_tok || 
			    kw == "const"_tok || kw == "volatile"_tok || kw == "extern"_tok) {
				advance();
			} else {
				break;
			}
		}
		
		// Try to parse type specifier
		auto var_type_result = parse_type_specifier();
		if (!var_type_result.is_error()) {
			// After type, expect identifier (variable name)
			if (peek().is_identifier()) {
				advance();
				
				// After identifier, check what comes next:
				// - '=' : variable template primary definition
				// - '{' : variable template with brace initialization (C++11)
				// - '<' followed by '...>' and then '=' or '{' : variable template partial specialization
				// - '<' followed by '...>' and then '::' : NOT a variable template (static member definition)
				// - '(' : function, not variable template
				if (!peek().is_eof()) {
					if (peek() == "="_tok || peek() == "{"_tok) {
						is_variable_template = true;
					} else if (peek() == "<"_tok) {
						// Could be partial spec or static member definition
						// Need to skip the template args and check what follows
						advance(); // consume '<'
						int angle_depth = 1;
						while (angle_depth > 0 && !peek().is_eof()) {
							if (peek() == "<"_tok) {
								angle_depth++;
							} else if (peek() == ">"_tok) {
								angle_depth--;
							} else if (peek() == ">>"_tok) {
								angle_depth -= 2;
							}
							advance();
						}
						// Now check what follows the closing >
						// If it's '=' or '{', it's a variable template partial spec
						// If it's '::', it's a static member definition (NOT variable template)
						if (!peek().is_eof() && 
						    (peek() == "="_tok || peek() == "{"_tok)) {
							is_variable_template = true;
						}
						// If it's '::', fall through (is_variable_template stays false)
					}
				}
			}
		}
		
		// Restore position for actual parsing
		restore_token_position(var_check_pos);
	}

	// Note: current_template_param_names_ and parsing_template_body_ were set earlier
	// (after template_param_names was populated) so that variable template detection
	// can recognize template parameters in type specifiers.

	// Check for requires clause after template parameters
	// Syntax: template<typename T> requires Concept<T> ...
	std::optional<ASTNode> requires_clause;
	if (peek() == "requires"_tok) {
		Token requires_token = peek_info();
		advance(); // consume 'requires'
		
		// Parse the constraint expression
		auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (constraint_result.is_error()) {
			// Clean up template parameter context before returning
			current_template_param_names_.clear();
			parsing_template_body_ = false;
			return constraint_result;
		}
		
		// Create RequiresClauseNode
		requires_clause = emplace_node<RequiresClauseNode>(
			*constraint_result.node(),
			requires_token
		);
		
		// After parsing requires clause, re-check if this is a class/struct/union template
		// The original check (before requires clause) would have seen 'requires' keyword
		// and set is_class_template to false, but now we can see the actual keyword
		if (!is_class_template && !peek().is_eof() &&
		    peek().is_keyword() &&
		    (peek() == "class"_tok || peek() == "struct"_tok || peek() == "union"_tok)) {
			is_class_template = true;
			FLASH_LOG(Parser, Debug, "Re-detected class template after requires clause");
		}
		
		// Also re-check for alias template after requires clause
		// Pattern: template<typename T> requires Constraint using Alias = T;
		if (!is_alias_template && peek() == "using"_tok) {
			is_alias_template = true;
			FLASH_LOG(Parser, Debug, "Re-detected alias template after requires clause");
		}
		
		// Also re-check for variable template after requires clause
		// Pattern: template<T> requires Constraint inline constexpr bool var<T> = value;
		if (!is_class_template && !is_variable_template && !peek().is_eof()) {
			auto var_recheck_pos = save_token_position();
			
			// Try to parse type specifier (it handles skipping storage class specifiers internally)
			auto var_type_result = parse_type_specifier();
			if (!var_type_result.is_error()) {
				// After type, expect identifier
				if (peek().is_identifier()) {
					advance();
					
					// Check for '=', '{', or '<' followed by pattern and '=' or '{'
					if (!peek().is_eof()) {
						if (peek() == "="_tok || peek() == "{"_tok) {
							is_variable_template = true;
							FLASH_LOG(Parser, Debug, "Re-detected variable template after requires clause");
						} else if (peek() == "<"_tok) {
							// Skip template args and check for '=' or '{'
							advance();
							int angle_depth = 1;
							while (angle_depth > 0 && !peek().is_eof()) {
								update_angle_depth(peek(), angle_depth);
								advance();
							}
							if (!peek().is_eof() && 
							    (peek() == "="_tok || peek() == "{"_tok)) {
								is_variable_template = true;
								FLASH_LOG(Parser, Debug, "Re-detected variable template partial spec after requires clause");
							}
						}
					}
				}
			}
			
			restore_token_position(var_recheck_pos);
		}
	}

	ParseResult decl_result;
	if (is_concept_template) {
		// Parse concept template: template<typename T> concept Name = constraint;
		// Consume 'concept' keyword
		Token concept_token = peek_info();
		advance();
		
		// Parse the concept name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected concept name after 'concept' in template", current_token_);
		}
		Token concept_name_token = peek_info();
		advance();
		
		// Expect '=' before the constraint expression
		if (peek() != "="_tok) {
			return ParseResult::error("Expected '=' after concept name", current_token_);
		}
		advance(); // consume '='
		
		// Parse the constraint expression
		auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (constraint_result.is_error()) {
			return constraint_result;
		}
		
		// Expect ';' at the end
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after concept definition", current_token_);
		}
		
		// Convert template_params (ASTNode vector) to TemplateParameterNode vector
		std::vector<TemplateParameterNode> template_param_nodes;
		for (const auto& param : template_params) {
			if (param.is<TemplateParameterNode>()) {
				template_param_nodes.push_back(param.as<TemplateParameterNode>());
			}
		}
		
		// Create the ConceptDeclarationNode with template parameters
		auto concept_node = emplace_node<ConceptDeclarationNode>(
			concept_name_token,
			std::move(template_param_nodes),
			*constraint_result.node(),
			concept_token
		);
		
		// Register the concept in the global concept registry
		gConceptRegistry.registerConcept(concept_name_token.value(), concept_node);
		
		// Also register with namespace-qualified name if we're in a namespace
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		if (!current_handle.isGlobal()) {
			StringHandle concept_handle = concept_name_token.handle();
			StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, concept_handle);
			gConceptRegistry.registerConcept(StringTable::getStringView(qualified_handle), concept_node);
		}
		
		// Clean up template parameter context before returning
		// Note: only clear current_template_param_names_, keep parsing_template_body_ as-is
		current_template_param_names_.clear();
		
		return saved_position.success(concept_node);
	} else if (is_alias_template) {
		// Consume 'using' keyword
		advance();
		
		// Parse alias name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected alias name after 'using' in template", current_token_);
		}
		Token alias_name_token = peek_info();
		std::string_view alias_name = alias_name_token.value();
		advance();
		
		// Expect '='
		if (peek() != "="_tok) {
			return ParseResult::error("Expected '=' after alias name in template", current_token_);
		}
		advance(); // consume '='
		
		// Save position before parsing target type - we may need to reparse
		auto target_type_start_pos = save_token_position();
		
		// Parse the target type
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}
		
		// Get the TypeSpecifierNode and check for pointer/reference modifiers
		TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
		
		// Check if the target type is a template instantiation with unresolved parameters
		// This happens when parsing things like: template<bool B> using bool_constant = integral_constant<bool, B>
		// The integral_constant<bool, B> gets instantiated with "?" placeholder in the name
		bool has_unresolved_params = false;
		StringHandle target_template_name;
		std::vector<ASTNode> target_template_arg_nodes;

		if ((type_spec.type() == Type::Struct || type_spec.type() == Type::UserDefined) &&
		    type_spec.type_index() < gTypeInfo.size()) {
			const TypeInfo& ti = gTypeInfo[type_spec.type_index()];
			std::string_view type_name = StringTable::getStringView(ti.name());

			// Check for incomplete instantiation indicating unresolved template parameters
			// But NOT if the name already contains :: (which means ::type was already resolved)
			if (ti.is_incomplete_instantiation_ && type_name.find("::") == std::string_view::npos) {
				has_unresolved_params = true;
				FLASH_LOG(Parser, Debug, "Alias target type '", StringTable::getStringView(ti.name()), "' has unresolved parameters - using deferred instantiation");
			}
			// Phase 6: Use TypeInfo::isTemplateInstantiation() instead of parsing $
			// Check if this is a template instantiation (hash-based naming)
			// But NOT if the name already contains :: (which means ::type was already resolved)
			else if (ti.isTemplateInstantiation()) {
				// Only treat as deferred if there's NO :: in the name
				// If there's ::type or similar, the type has already been resolved to a member type
				if (type_name.find("::") == std::string_view::npos) {
					// Use the stored base template name instead of parsing the $
					std::string_view template_name_part = StringTable::getStringView(ti.baseTemplateName());
					auto template_opt = gTemplateRegistry.lookupTemplate(template_name_part);
					if (template_opt.has_value()) {
						has_unresolved_params = true;
						FLASH_LOG(Parser, Debug, "Alias target '", type_name, "' is template instantiation - using deferred instantiation");
					}
				} else {
					FLASH_LOG(Parser, Debug, "Alias target '", type_name, "' is a resolved member type (not a dependent placeholder)");
				}
			}
			// FALLBACK: Check if the resolved type name is a registered primary template
			// This happens when template arguments are dependent and instantiation was skipped,
			// so the type falls back to the primary template name without any instantiation suffix.
			else {
				// Check if this is a registered template - if so, the parsing of template args
				// with dependent parameters resulted in fallback to the primary template
				auto template_opt = gTemplateRegistry.lookupTemplate(type_name);
				if (template_opt.has_value()) {
					FLASH_LOG(Parser, Debug, "Alias target '", type_name, "' is a primary template (instantiation was skipped due to dependent args) - using deferred instantiation");
					has_unresolved_params = true;
				}
			}

			// Also check if the type is a dependent placeholder (UserDefined type with
			// a name containing our template parameter names)
			// This catches cases like "integral_constant_bool_B" created by dependent template instantiation
			if (!has_unresolved_params && type_spec.type() == Type::UserDefined) {
				for (const auto& param_name : template_param_names) {
					std::string_view param_sv = param_name.view();
					// Check if the type name contains the parameter as a suffix (after underscore)
					// Pattern: "..._<param>" like "integral_constant_bool_B"
					size_t pos = type_name.rfind(param_sv);
					if (pos != std::string_view::npos && pos > 0 && type_name[pos - 1] == '_' &&
					    pos + param_sv.size() == type_name.size()) {
						has_unresolved_params = true;
						FLASH_LOG(Parser, Debug, "Alias target '", type_name, "' is a dependent placeholder containing template param '",
						          param_sv, "' - using deferred instantiation");
						break;
					}
				}
			}
			
			if (has_unresolved_params) {
				// Rewind and re-parse to extract template name and arguments as AST nodes
				restore_token_position(target_type_start_pos);
				
				// Parse the template name (possibly namespace-qualified like ns1::vec)
				if (peek().is_identifier()) {
					StringBuilder name_builder;
					name_builder.append(peek_info().value());
					advance();
					
					// Handle qualified names (e.g., ns1::vec, std::vector)
					while (peek() == "::"_tok) {
						advance();  // consume '::'
						if (peek() == "template"_tok) {
							advance();  // consume 'template' disambiguator
						}
						if (!peek().is_identifier()) break;
						name_builder.append("::"sv).append(peek_info().value());
						advance();
					}
					
					std::string_view full_name = name_builder.commit();
					target_template_name = StringTable::getOrInternStringHandle(full_name);
					
					// Parse template arguments as AST nodes (not evaluated)
					if (peek() == "<"_tok) {
						auto template_args_with_nodes = parse_explicit_template_arguments(&target_template_arg_nodes);
						FLASH_LOG(Parser, Debug, "Captured ", target_template_arg_nodes.size(), " unevaluated template argument nodes for deferred instantiation");
						
						// Debug: log what we captured
						for (size_t i = 0; i < target_template_arg_nodes.size(); ++i) {
							const ASTNode& node = target_template_arg_nodes[i];
							if (node.is<TypeSpecifierNode>()) {
								const TypeSpecifierNode& ts = node.as<TypeSpecifierNode>();
								if (ts.type_index() < gTypeInfo.size()) {
									std::string_view node_type_name = StringTable::getStringView(gTypeInfo[ts.type_index()].name());
									FLASH_LOG(Parser, Debug, "  Node[", i, "]: TypeSpecifier, type=", static_cast<int>(ts.type()), 
									          ", type_name='", node_type_name, "'");
								}
							}
						}
					}
				}
				
				// Note: We already consumed the tokens, so type_spec still points to the unresolved type
				// We don't need to re-parse again - just use the existing type_spec
			}
		}
		
		// Discard the saved position since we've consumed the type
		discard_saved_token(target_type_start_pos);
		
		consume_pointer_ref_modifiers(type_spec);
		
		// Expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after alias template declaration", current_token_);
		}
		
		// Create TemplateAliasNode - use deferred constructor if we have unresolved parameters
		ASTNode alias_node;
		if (has_unresolved_params && target_template_name.isValid()) {
			FLASH_LOG(Parser, Debug, "Creating deferred TemplateAliasNode for '", alias_name, "' -> '", target_template_name.view(), "'");
			alias_node = emplace_node<TemplateAliasNode>(
				std::move(template_params),
				std::move(template_param_names),
				StringTable::getOrInternStringHandle(alias_name),
				type_result.node().value(),
				target_template_name,
				std::move(target_template_arg_nodes)
			);
		} else {
			// Regular (non-deferred) alias
			alias_node = emplace_node<TemplateAliasNode>(
				std::move(template_params),
				std::move(template_param_names),
				StringTable::getOrInternStringHandle(alias_name),
				type_result.node().value()
			);
		}
		
		// Register the alias template in the template registry
		// We'll handle instantiation later when the alias is used
		// Register with QualifiedIdentifier — handles both simple and namespace-qualified keys
		gTemplateRegistry.register_alias_template(
			QualifiedIdentifier::fromQualifiedName(alias_name, gSymbolTable.get_current_namespace_handle()),
			alias_node);
		
		// Clean up template parameter context before returning
		// Note: only clear current_template_param_names_, keep parsing_template_body_ as-is
		current_template_param_names_.clear();
		
		return saved_position.success(alias_node);
	}
	else if (is_variable_template) {
		// Parse storage class specifiers manually (constexpr, inline, static, etc.)
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
		
		// Now parse the variable declaration: Type name = initializer;
		// We need to manually parse type, name, and initializer
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}
		
		// Parse variable name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected variable name in variable template", current_token_);
		}
		Token var_name_token = peek_info();
		advance();
		
		// Check for variable template partial specialization: name<pattern>
		// Example: template<typename T> inline constexpr bool is_reference_v<T&> = true;
		std::vector<TemplateTypeArg> specialization_pattern;
		bool is_partial_spec = false;
		if (peek() == "<"_tok) {
			advance(); // consume '<'
			is_partial_spec = true;
			
			// Parse the specialization pattern (e.g., T&, T*, T&&, or non-type values like 0)
			// These are template argument patterns
			while (peek() != ">"_tok) {
				// Check for typename keyword (for dependent types)
				if (peek() == "typename"_tok) {
					advance(); // consume 'typename'
				}
				
				// Check if this is a non-type value (numeric literal)
				if (peek().is_literal()) {
					// It's a numeric literal - treat as non-type value
					Token value_token = peek_info();
					advance();
					
					// Create template type argument for the value
					TemplateTypeArg arg;
					arg.is_value = true;
					arg.value = std::stoll(std::string(value_token.value()));
					arg.base_type = Type::Int;
					specialization_pattern.push_back(arg);
				} else {
					// Parse the pattern type
					auto pattern_type = parse_type_specifier();
					if (pattern_type.is_error()) {
						return pattern_type;
					}
					
					// Check for reference modifiers
					TypeSpecifierNode& type_spec = pattern_type.node()->as<TypeSpecifierNode>();
					CVQualifier cv = parse_cv_qualifiers();
					type_spec.add_cv_qualifier(cv);
				
					// Parse pointer/reference declarators
					while (peek() == "*"_tok) {
						advance(); // consume '*'
						CVQualifier ptr_cv = parse_cv_qualifiers();
						type_spec.add_pointer_level(ptr_cv);
					}
					
					// Parse reference qualifier
					ReferenceQualifier ref = parse_reference_qualifier();
					if (ref != ReferenceQualifier::None) {
						type_spec.set_reference_qualifier(ref);
					}
					
					// Parse array bounds: [_Nm] or []
					bool is_array = false;
					while (peek() == "["_tok) {
						advance(); // consume '['
						is_array = true;
						// Skip the array bound expression (could be a template parameter like _Nm)
						while (peek() != "]"_tok) {
							advance();
						}
						if (peek() == "]"_tok) {
							advance(); // consume ']'
						}
					}
					
					// Create template type argument
					TemplateTypeArg arg;
					arg.base_type = type_spec.type();
					arg.type_index = type_spec.type_index();
					arg.is_value = false;
					arg.cv_qualifier = type_spec.cv_qualifier();
					arg.pointer_depth = type_spec.pointer_depth();
					arg.ref_qualifier = type_spec.reference_qualifier();
					arg.is_array = is_array;
					// Mark as dependent only for partial specializations
					// For full specializations (template<>), the types are concrete, not dependent
					arg.is_dependent = !template_params.empty();
					
					// Store the type name for pattern matching
					// For template instantiations like ratio<_Num, _Den>, this will be "ratio"
					// For simple types like T, this will be "T"
					if (type_spec.token().value().size() > 0) {
						arg.dependent_name = type_spec.token().handle();
					}
					
					specialization_pattern.push_back(arg);
				}
				
				// Check for comma or closing >
				if (peek() == ","_tok) {
					advance(); // consume ','
				} else {
					break;
				}
			}
			
			if (peek() != ">"_tok) {
				return ParseResult::error("Expected '>' after variable template specialization pattern", current_token_);
			}
			advance(); // consume '>'
		}
		
		// Create DeclarationNode
		auto decl_node = emplace_node<DeclarationNode>(
			type_result.node().value(),
			var_name_token
		);
		
		// Parse initializer
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
		// Check for direct brace initialization: template<typename T> inline constexpr T val{};
		else if (peek() == "{"_tok) {
			const TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
			auto init_result = parse_brace_initializer(type_spec);
			if (init_result.is_error()) {
				return init_result;
			}
			init_expr = init_result.node();
		}
		
		// Expect semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after variable template declaration", current_token_);
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
		
		// Register in template registry
		std::string_view var_name = var_name_token.value();
		if (is_partial_spec) {
			// Register structurally for pattern matching via TemplatePattern::matches()
			const auto& spec_tmpl = template_var_node.as<TemplateVariableDeclarationNode>();
			gTemplateRegistry.registerVariableTemplateSpecialization(
				var_name, spec_tmpl.template_parameters(), specialization_pattern, template_var_node);
			FLASH_LOG(Parser, Debug, "Registered variable template partial specialization (structural): ", var_name,
			          " with ", specialization_pattern.size(), " pattern args");
		} else {
			gTemplateRegistry.registerVariableTemplate(
				QualifiedIdentifier::fromQualifiedName(var_name, gSymbolTable.get_current_namespace_handle()),
				template_var_node);
		}
		
		// Also add to symbol table so identifier lookup works
		gSymbolTable.insert(var_name, template_var_node);
		
		// Clean up template parameter context before returning
		// Note: only clear current_template_param_names_, keep parsing_template_body_ as-is
		// to avoid breaking template argument resolution in subsequent code
		current_template_param_names_.clear();
		
		return saved_position.success(template_var_node);
	}
	else if (is_class_template) {
		// Check if this is a partial specialization by peeking ahead
		// Pattern: template<typename T> struct Name<T&> { ... }
		// After struct/class keyword and name, if we see '<', it's a specialization
		bool is_partial_specialization = false;
		if (!is_specialization && !template_params.empty()) {
			// Save position to peek ahead
			auto peek_pos = save_token_position();
			
			// Try to consume struct/class keyword
			if (consume("struct"_tok) || consume("class"_tok) || consume("union"_tok)) {
				// Skip C++11 attributes between struct/class and name (e.g., [[__deprecated__]])
				skip_cpp_attributes();
				
				// Try to get class name
				if (peek().is_identifier()) {
					advance();
					
					// Check if template arguments follow
					if (peek() == "<"_tok) {
						// This is a partial specialization!
						is_partial_specialization = true;
					}
				}
			}
			
			// Restore position
			restore_token_position(peek_pos);
		}
		
		// Handle full template specialization (template<>)
		if (is_specialization) {
			// Parse: class ClassName<TemplateArgs> { ... }
			// We need to parse the class keyword, name, template arguments, and body separately

			// Set parsing context flags
			parsing_template_class_ = true;
			parsing_template_body_ = true;

			// Save position before struct/class keyword — used if this turns out to be an
			// out-of-line nested class definition so parse_struct_declaration() can re-parse it
			SaveHandle struct_keyword_pos = save_token_position();

			bool is_class = consume("class"_tok);
			bool is_union = false;
			if (!is_class) {
				if (!consume("struct"_tok)) {
					is_union = consume("union"_tok);  // Try union last
				}
			}

			// Skip C++11 attributes between struct/class and name (e.g., [[__deprecated__]])
			skip_cpp_attributes();

			// Parse class name
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected class name after 'class' keyword", current_token_);
			}

			Token class_name_token = peek_info();
			std::string_view template_name = class_name_token.value();
			advance();

			// Parse template arguments: <int>, <float>, etc.
			auto template_args_opt = parse_explicit_template_arguments();
			if (!template_args_opt.has_value()) {
				return ParseResult::error("Expected template arguments in specialization", current_token_);
			}

			std::vector<TemplateTypeArg> template_args = *template_args_opt;

			// Check for out-of-line member class definition: template<> class Foo<Args>::Bar { ... }
			// E.g., template<> class basic_ostream<char, char_traits<char>>::sentry { ... };
			// Register it so the body is re-parsed during template instantiation.
			if (peek() == "::"_tok) {
				auto scope_check = save_token_position();
				advance(); // consume '::'
				if (peek().is_identifier()) {
					discard_saved_token(scope_check);
					std::string_view member_class_name = peek_info().value();
					advance(); // consume member class name
					FLASH_LOG_FORMAT(Templates, Debug, "Out-of-line member class definition (full spec): {}::{}",
					                 template_name, member_class_name);
					
					// Skip base class list if present
					if (peek() == ":"_tok) {
						advance();
						while (!peek().is_eof() && peek() != "{"_tok && peek() != ";"_tok) {
							advance();
						}
					}
					
					// Skip body if present
					if (peek() == "{"_tok) {
						skip_balanced_braces();
					}
					
					// Consume trailing semicolon
					consume(";"_tok);
					
					// Register the out-of-line nested class definition
					// struct_keyword_pos points at the struct/class keyword so parse_struct_declaration()
					// can re-parse "struct Wrapper<T>::Nested { ... }" during instantiation.
					// For full specializations (template<>), store the concrete template_args so the
					// nested class is only applied when instantiation arguments match.
					gTemplateRegistry.registerOutOfLineNestedClass(template_name, OutOfLineNestedClass{
						template_params,
						StringTable::getOrInternStringHandle(member_class_name),
						struct_keyword_pos, template_param_names, is_class,
						template_args  // concrete specialization args (e.g., <int>)
					});
					FLASH_LOG_FORMAT(Templates, Debug, "Registered out-of-line nested class (full spec): {}::{}",
					                 template_name, member_class_name);
					
					// Reset parsing context flags
					parsing_template_class_ = false;
					parsing_template_body_ = false;
					
					return saved_position.success();
				}
				// Not an identifier after '::' - restore parser position
				restore_token_position(scope_check);
			}

			// struct_keyword_pos was only needed for OOL nested class registration above;
			// discard it so it doesn't leak in all other specialization paths.
			discard_saved_token(struct_keyword_pos);

			// Check for forward declaration: template<> struct ClassName<Args>;
			if (peek() == ";"_tok) {
				advance(); // consume ';'
				
				// For forward declarations, just register the type name and return
				// The instantiated name includes the template arguments
				auto instantiated_name = StringTable::getOrInternStringHandle(get_instantiated_class_name(template_name, template_args));
				
				// Create a minimal struct node
				auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(
					instantiated_name,
					is_class,
					is_union
				);
				
				// Register the type so it can be referenced later
				TypeInfo& struct_type_info = add_struct_type(instantiated_name);
				
				// Store template instantiation metadata for O(1) lookup (Phase 6)
				struct_type_info.setTemplateInstantiationInfo(
					QualifiedIdentifier::fromQualifiedName(template_name, gSymbolTable.get_current_namespace_handle()),
					convertToTemplateArgInfo(template_args)
				);
				
				// Register the specialization with the template registry
				gTemplateRegistry.registerSpecialization(
					std::string(template_name),
					template_args,
					struct_node
				);
				
				FLASH_LOG_FORMAT(Templates, Debug, "Registered forward declaration for specialization: {}", 
				                 StringTable::getStringView(instantiated_name));
				
				// Reset parsing context flags
				parsing_template_class_ = false;
				parsing_template_body_ = false;
				
				return saved_position.success(struct_node);
			}

			// Now parse the class body as a regular struct
			// But we need to give it a unique name that includes the template arguments
			auto instantiated_name = StringTable::getOrInternStringHandle(get_instantiated_class_name(template_name, template_args));

			// Create a struct node with the instantiated name
			auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(
				instantiated_name,
				is_class,
				is_union
			);

			// Create struct type info first so we can reference it
			TypeInfo& struct_type_info = add_struct_type(instantiated_name);
			
			// Store template instantiation metadata for O(1) lookup (Phase 6)
			struct_type_info.setTemplateInstantiationInfo(
				QualifiedIdentifier::fromQualifiedName(template_name, gSymbolTable.get_current_namespace_handle()),
				convertToTemplateArgInfo(template_args)
			);

			// Create struct info for tracking members - required before parsing static members
			auto struct_info = std::make_unique<StructTypeInfo>(instantiated_name, struct_ref.default_access());
			struct_info->is_union = is_union;
			
			// Parse base class list (if present): : public Base1, private Base2
			if (peek() == ":"_tok) {
				advance();  // consume ':'

				do {
					// Parse virtual keyword (optional)
					bool is_virtual_base = false;
					if (peek() == "virtual"_tok) {
						is_virtual_base = true;
						advance();
					}

					// Parse access specifier (optional, defaults to public for struct, private for class)
					AccessSpecifier base_access = is_class ? AccessSpecifier::Private : AccessSpecifier::Public;

					if (peek().is_keyword()) {
						std::string_view keyword = peek_info().value();
						if (keyword == "public") {
							base_access = AccessSpecifier::Public;
							advance();
						} else if (keyword == "protected") {
							base_access = AccessSpecifier::Protected;
							advance();
						} else if (keyword == "private") {
							base_access = AccessSpecifier::Private;
							advance();
						}
					}

					// Check for virtual keyword after access specifier
					if (!is_virtual_base && peek() == "virtual"_tok) {
						is_virtual_base = true;
						advance();
					}

					// Parse base class name - could be qualified like ns::Base or simple like Base
					if (!peek().is_identifier()) {
						return ParseResult::error("Expected base class name", peek_info());
					}

					Token base_name_token = advance();
					StringBuilder base_class_name_builder;
					base_class_name_builder.append(base_name_token.value());
					
					// Check for qualified name (e.g., ns::Base or std::false_type)
					while (peek() == "::"_tok) {
						advance(); // consume '::'
						
						if (!peek().is_identifier()) {
							return ParseResult::error("Expected identifier after '::'", peek_info());
						}
						auto next_name_token = advance(); // consume the identifier
						
						base_class_name_builder.append("::"sv);
						base_class_name_builder.append(next_name_token.value());
						base_name_token = next_name_token;  // Update for error reporting
						
						FLASH_LOG_FORMAT(Parser, Debug, "Parsing qualified base class name in full specialization: {}", base_class_name_builder.preview());
					}
					
					std::string_view base_class_name = base_class_name_builder.commit();
					std::vector<ASTNode> template_arg_nodes;
					std::optional<std::vector<TemplateTypeArg>> base_template_args_opt;
					std::optional<StringHandle> member_type_name;
					std::optional<Token> member_name_token;
					
					// Check if this is a template base class (e.g., Base<T>)
						if (peek() == "<"_tok) {
							// Parse template arguments
							base_template_args_opt = parse_explicit_template_arguments(&template_arg_nodes);
							if (!base_template_args_opt.has_value()) {
								return ParseResult::error("Failed to parse template arguments for base class", peek_info());
							}
						
							// Handle member access when current_token_ already points to '::'
							if (current_token_.value() == "::" && !member_type_name.has_value()) {
								if (!peek().is_identifier()) {
									return ParseResult::error("Expected member name after ::", peek_info());
								}
								member_type_name = peek_info().handle();
								member_name_token = peek_info();
								advance(); // consume member name
							}

							// Check for member type access after template arguments (e.g., Base<T>::type)
							if (peek() == "::"_tok) {
								advance(); // consume ::
								if (!peek().is_identifier()) {
									return ParseResult::error("Expected member name after ::", peek_info());
							}
							member_type_name = peek_info().handle();
							member_name_token = peek_info();
							advance(); // consume member name
						}
						// Fallback: consume member access if still present (ensures ::type is handled for dependent bases)
						if (!member_type_name.has_value() && peek() == "::"_tok) {
							advance();
							if (!peek().is_identifier()) {
								return ParseResult::error("Expected member name after ::", peek_info());
							}
							member_type_name = peek_info().handle();
							member_name_token = peek_info();
							advance();
						}

						std::vector<TemplateTypeArg> base_template_args = *base_template_args_opt;
						
						// Check if any template arguments are dependent
						bool has_dependent_args = false;
						for (const auto& arg : base_template_args) {
							if (arg.is_dependent) {
								has_dependent_args = true;
								break;
							}
						}
						
						// If template arguments are dependent, we're inside a template declaration
						if (has_dependent_args) {
							FLASH_LOG_FORMAT(Templates, Debug, "Base class {} has dependent template arguments - deferring resolution", base_class_name);

							std::vector<TemplateArgumentNodeInfo> arg_infos;
							arg_infos.reserve(base_template_args.size());
							for (size_t i = 0; i < base_template_args.size(); ++i) {
								TemplateArgumentNodeInfo info;
								info.is_pack = base_template_args[i].is_pack;
								info.is_dependent = base_template_args[i].is_dependent;
								if (i < template_arg_nodes.size()) {
									info.node = template_arg_nodes[i];
								}
								arg_infos.push_back(std::move(info));
							}

							StringHandle template_name_handle = StringTable::getOrInternStringHandle(base_class_name);
							struct_ref.add_deferred_template_base_class(template_name_handle, std::move(arg_infos), member_type_name, base_access, is_virtual_base);
							continue;  // Skip to next base class or exit loop
						}
						
						// Instantiate base class template if needed and register in AST
						std::optional<std::string_view> instantiated_base_name = instantiate_and_register_base_template(base_class_name, base_template_args);
						if (instantiated_base_name.has_value()) {
							base_class_name = *instantiated_base_name;
						}

						// Resolve member type alias if present (e.g., Base<T>::type)
						if (member_type_name.has_value()) {
							StringBuilder qualified_builder;
							qualified_builder.append(base_class_name);
							qualified_builder.append("::"sv);
							qualified_builder.append(StringTable::getStringView(*member_type_name));
							std::string_view alias_name = qualified_builder.commit();
							
							auto alias_it = gTypesByName.find(StringTable::getOrInternStringHandle(alias_name));
							if (alias_it == gTypesByName.end()) {
								return ParseResult::error("Base class '" + std::string(alias_name) + "' not found", member_name_token.value_or(base_name_token));
							}
							
							base_class_name = alias_name;
							if (member_name_token.has_value()) {
								base_name_token = *member_name_token;
							}
						}
					}

					// Validate and add the base class
					ParseResult result = validate_and_add_base_class(base_class_name, struct_ref, struct_info.get(), base_access, is_virtual_base, base_name_token);
					if (result.is_error()) {
						return result;
					}
				} while (consume(","_tok));
			}
			
			// Expect opening brace
			if (!consume("{"_tok)) {
				return ParseResult::error("Expected '{' after class name in specialization", peek_info());
			}

			// Parse class members (simplified - reuse struct parsing logic)
			// For now, we'll parse a simple class body
			AccessSpecifier current_access = struct_ref.default_access();

			// Set up member function context so functions know they're in a class
			member_function_context_stack_.push_back({
				instantiated_name,
				struct_type_info.type_index_,
				&struct_ref,
				nullptr  // local_struct_info - not needed during template instantiation
			});

			while (!peek().is_eof() && peek() != "}"_tok) {
				// Skip empty declarations (bare ';' tokens) - valid in C++
				if (peek() == ";"_tok) {
					advance();
					continue;
				}
				
				// Check for access specifiers
				if (peek().is_keyword()) {
					if (peek() == "public"_tok) {
						advance();
						if (!consume(":"_tok)) {
							return ParseResult::error("Expected ':' after 'public'", peek_info());
						}
						current_access = AccessSpecifier::Public;
						continue;
					} else if (peek() == "private"_tok) {
						advance();
						if (!consume(":"_tok)) {
							return ParseResult::error("Expected ':' after 'private'", peek_info());
						}
						current_access = AccessSpecifier::Private;
						continue;
					} else if (peek() == "protected"_tok) {
						advance();
						if (!consume(":"_tok)) {
							return ParseResult::error("Expected ':' after 'protected'", peek_info());
						}
						current_access = AccessSpecifier::Protected;
						continue;
					} else if (peek() == "static_assert"_tok) {
						// Handle static_assert inside class body
						auto static_assert_result = parse_static_assert();
						if (static_assert_result.is_error()) {
							return static_assert_result;
						}
						continue;
					} else if (peek() == "enum"_tok) {
						// Handle enum declaration inside class body
						auto enum_result = parse_enum_declaration();
						if (enum_result.is_error()) {
							return enum_result;
						}
						// Note: nested_enum_indices_ tracking is not done here for template class bodies.
						// Enums are registered globally by parse_enum_declaration, and enumerators are
						// typically resolved via the global symbol table before the struct-scoped fallback.
						continue;
					} else if (peek() == "using"_tok) {
						// Handle type alias inside class body: using value_type = T;
						auto alias_result = parse_member_type_alias("using", &struct_ref, current_access);
						if (alias_result.is_error()) {
							return alias_result;
						}
						continue;
					} else if (peek() == "typedef"_tok) {
						// Handle typedef inside class body: typedef T _Type;
						auto alias_result = parse_member_type_alias("typedef", &struct_ref, current_access);
						if (alias_result.is_error()) {
							return alias_result;
						}
						continue;
					} else if (peek() == "template"_tok) {
						// Handle member function template or member template alias
						auto template_result = parse_member_template_or_function(struct_ref, current_access);
						if (template_result.is_error()) {
							return template_result;
						}
						continue;
					} else if (peek() == "static"_tok) {
						// Handle static members: static const int size = 10;
						advance(); // consume "static"
						
						auto static_result = parse_static_member_block(instantiated_name, struct_ref, 
						                                                 struct_info.get(), current_access, 
						                                                 current_template_param_names_, /*use_struct_type_info=*/false);
						if (static_result.is_error()) {
							return static_result;
						}
						continue;
					} else if (peek() == "struct"_tok || peek() == "class"_tok) {
						// Handle nested struct/class declarations inside full specialization body
						advance(); // consume 'struct' or 'class'
						
						// Skip C++11 attributes
						skip_cpp_attributes();
						
						// Skip struct name if present
						if (peek().is_identifier()) {
							advance(); // consume struct name
						}
						
						// Skip template arguments if present (e.g., struct Wrapper<int>)
						if (peek() == "<"_tok) {
							parse_explicit_template_arguments();
						}
						
						// Skip 'final' specifier if present
						if (peek() == "final"_tok) {
							advance();
						}
						
						// Skip base class list if present (e.g., struct Frame : public Base)
						if (peek() == ":"_tok) {
							advance(); // consume ':'
							while (!peek().is_eof() && peek() != "{"_tok && peek() != ";"_tok) {
								advance();
							}
						}
						
						// Skip to body or semicolon
						if (peek() == "{"_tok) {
							skip_balanced_braces();
						}
						
						// Consume trailing semicolon
						if (peek() == ";"_tok) {
							advance();
						}
						continue;
					} else if (peek() == "friend"_tok) {
						// Handle friend declarations inside full specialization body
						auto friend_result = parse_friend_declaration();
						if (friend_result.is_error()) {
							return friend_result;
						}
						continue;
					}
				}

				// Check for constructor (identifier matching template name followed by '(')
				// In full specializations, the constructor uses the base template name (e.g., "Calculator"),
				// not the instantiated name (e.g., "Calculator_int")
				// Must skip specifiers like constexpr, explicit, inline first
				SaveHandle saved_pos = save_token_position();
				bool found_constructor = false;
				bool ctor_is_constexpr = false;
				bool ctor_is_explicit = false;
				{
					// Skip declaration specifiers (constexpr, inline, etc.)
					auto specs = parse_declaration_specifiers();
					ctor_is_constexpr = specs.is_constexpr();
					// Also skip 'explicit' which is constructor-specific
					while (peek() == "explicit"_tok) {
						ctor_is_explicit = true;
						advance();
						if (peek() == "("_tok) {
							skip_balanced_parens(); // explicit(condition)
						}
					}
				}
				if (!peek().is_eof() && peek().is_identifier() &&
				    peek_info().value() == template_name) {
					// Look ahead to see if this is a constructor
					Token name_token = advance();
					std::string_view ctor_name = name_token.value();
					
					if (peek() == "("_tok) {
						// Discard saved position since we're using this as a constructor
						discard_saved_token(saved_pos);
						found_constructor = true;
						
						// This is a constructor - use instantiated_name as the struct name
						auto [ctor_node, ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(instantiated_name, StringTable::getOrInternStringHandle(ctor_name));
						
						// Apply specifiers detected during lookahead
						ctor_ref.set_constexpr(ctor_is_constexpr);
						ctor_ref.set_explicit(ctor_is_explicit);
						
						// Parse parameters using unified parse_parameter_list (Phase 1)
						FlashCpp::ParsedParameterList params;
						auto param_result = parse_parameter_list(params);
						if (param_result.is_error()) {
							return param_result;
						}
						for (const auto& param : params.parameters) {
							ctor_ref.add_parameter_node(param);
						}
						
						// Enter a temporary scope for parsing the initializer list
						gSymbolTable.enter_scope(ScopeType::Function);
						
						// Register parameters in symbol table using helper (Phase 5)
						register_parameters_in_scope(ctor_ref.parameter_nodes());
						
						// Parse exception specifier (noexcept or throw()) before initializer list
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
							advance();  // consume ':'
							
							while (peek() != "{"_tok &&
							       peek() != ";"_tok) {
								auto init_name_token = advance();
								if (init_name_token.type() != Token::Type::Identifier) {
									return ParseResult::error("Expected member or base class name in initializer list", init_name_token);
								}
								
								std::string_view init_name = init_name_token.value();
								
								// Handle namespace-qualified base class names: std::optional<_Tp>{...}
								while (peek() == "::"_tok) {
									advance(); // consume '::'
									if (peek().is_identifier() || peek().is_keyword()) {
										advance(); // consume the qualified name part
									}
								}
								
								// Check for template arguments: Tuple<Rest...>(...)
								if (peek() == "<"_tok) {
									// Parse and skip template arguments - they're part of the base class name
									auto init_template_args_opt = parse_explicit_template_arguments();
									if (!init_template_args_opt.has_value()) {
										return ParseResult::error("Failed to parse template arguments in initializer", peek_info());
									}
									// Modify init_name to include instantiated template name if needed
									// For now, we just consume the template arguments and continue
								}
								
								bool is_paren = peek() == "("_tok;
								bool is_brace = peek() == "{"_tok;
								
								if (!is_paren && !is_brace) {
									return ParseResult::error("Expected '(' or '{' after initializer name", peek_info());
								}
								
								advance();  // consume '(' or '{'
								TokenKind close_kind = [is_paren]() { if (is_paren) return ")"_tok; return "}"_tok; }();
								
								std::vector<ASTNode> init_args;
								if (peek() != close_kind) {
									do {
										ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (arg_result.is_error()) {
											return arg_result;
										}
										if (auto arg_node = arg_result.node()) {
											// Check for pack expansion: expr...
											if (peek() == "..."_tok) {
												advance(); // consume '...'
												// Mark this as a pack expansion - actual expansion happens at instantiation
											}
											init_args.push_back(*arg_node);
										}
									} while (consume(","_tok));
								}
								
								if (!consume(close_kind)) {
									return ParseResult::error(is_paren ?
									    "Expected ')' after initializer arguments" :
									    "Expected '}' after initializer arguments", peek_info());
								}
								
								// Member initializer
								if (!init_args.empty()) {
									ctor_ref.add_member_initializer(init_name, init_args[0]);
								}
								
								if (!consume(","_tok)) {
									break;
								}
							}
						}
						
						// Check for = default or = delete
						bool is_defaulted = false;
						bool is_deleted = false;
						if (peek() == "="_tok) {
							advance(); // consume '='
							
							if (peek().is_keyword()) {
								if (peek() == "default"_tok) {
									advance();
									is_defaulted = true;
									
									if (!consume(";"_tok)) {
										gSymbolTable.exit_scope();
										return ParseResult::error("Expected ';' after '= default'", peek_info());
									}
									
									ctor_ref.set_is_implicit(true);
									auto [block_node, block_ref] = create_node_ref(BlockNode());
									ctor_ref.set_definition(block_node);
									gSymbolTable.exit_scope();
								} else if (peek() == "delete"_tok) {
									advance();
									is_deleted = true;

									if (!consume(";"_tok)) {
										gSymbolTable.exit_scope();
										return ParseResult::error("Expected ';' after '= delete'", peek_info());
									}

									// Determine what kind of constructor this is based on parameters
									size_t num_params = ctor_ref.parameter_nodes().size();
									bool is_copy_ctor = false;
									bool is_move_ctor = false;

									if (num_params == 1) {
										// Check if the parameter is a reference to this type
										const auto& param = ctor_ref.parameter_nodes()[0];
										if (param.is<DeclarationNode>()) {
											const auto& param_decl = param.as<DeclarationNode>();
											const auto& type_node = param_decl.type_node();
											if (type_node.has_value() && type_node.is<TypeSpecifierNode>()) {
												const auto& type_spec = type_node.as<TypeSpecifierNode>();
												std::string_view param_type_name = type_spec.token().value();
												// For template specializations, match against base template name
												if (param_type_name == template_name ||
												    param_type_name == instantiated_name) {
													if (type_spec.is_rvalue_reference()) {
														is_move_ctor = true;
													} else if (type_spec.is_reference()) {
														is_copy_ctor = true;
													}
												}
											}
										}
									}

									// Mark the deleted constructor in the struct AST node
									if (is_copy_ctor) {
										struct_ref.mark_deleted_copy_constructor();
										FLASH_LOG(Templates, Debug, "Marked copy constructor as deleted in struct: ", instantiated_name);
									} else if (is_move_ctor) {
										struct_ref.mark_deleted_move_constructor();
										FLASH_LOG(Templates, Debug, "Marked move constructor as deleted in struct: ", instantiated_name);
									} else {
										// Default constructor (no params or only optional params)
										struct_ref.mark_deleted_default_constructor();
										FLASH_LOG(Templates, Debug, "Marked default constructor as deleted in struct: ", instantiated_name);
									}

									gSymbolTable.exit_scope();
									continue;
								} else {
									gSymbolTable.exit_scope();
									return ParseResult::error("Expected 'default' or 'delete' after '='", peek_info());
								}
							} else {
								gSymbolTable.exit_scope();
								return ParseResult::error("Expected 'default' or 'delete' after '='", peek_info());
							}
						}
						
						// Parse constructor body if present
						if (!is_defaulted && !is_deleted && peek() == "{"_tok) {
							// Parse the constructor body immediately rather than delaying
							// This avoids pointer invalidation issues with delayed parsing
							auto block_result = parse_block();
							gSymbolTable.exit_scope();
							
							if (block_result.is_error()) {
								return block_result;
							}
							
							if (auto block = block_result.node()) {
								ctor_ref.set_definition(*block);
							}
						} else if (!is_defaulted && !is_deleted && !consume(";"_tok)) {
							gSymbolTable.exit_scope();
							return ParseResult::error("Expected '{', ';', '= default', or '= delete' after constructor declaration", peek_info());
						} else if (!is_defaulted && !is_deleted) {
							gSymbolTable.exit_scope();
						}
						
						struct_ref.add_constructor(ctor_node, current_access);
						
						// Add to AST for code generation
						// Full specializations are not template patterns - they need their constructors emitted
						ast_nodes_.push_back(ctor_node);
						continue;
					} else {
						// Not a constructor, restore position
						restore_token_position(saved_pos);
					}
				} else {
					// Not a constructor (identifier didn't match), restore position
					// to before specifiers were consumed during lookahead
					restore_token_position(saved_pos);
				}
				if (found_constructor) continue;

				// Check for destructor (~StructName followed by '(')
				// Use save/restore so specifiers are not lost if this is NOT a destructor
				{
				SaveHandle dtor_saved_pos = save_token_position();
				auto dtor_leading_specs = parse_member_leading_specifiers();
				bool dtor_is_virtual = !!(dtor_leading_specs & FlashCpp::MLS_Virtual);
				if (peek() == "~"_tok) {
				discard_saved_token(dtor_saved_pos);
					advance();  // consume '~'
					
					auto name_token_opt = advance();
					if (name_token_opt.type() != Token::Type::Identifier ||
					    name_token_opt.value() != template_name) {
						return ParseResult::error("Expected struct name after '~' in destructor", name_token_opt);
					}
					Token dtor_name_token = name_token_opt;
					std::string_view dtor_name = dtor_name_token.value();
					
					if (!consume("("_tok)) {
						return ParseResult::error("Expected '(' after destructor name", peek_info());
					}
					
					if (!consume(")"_tok)) {
						return ParseResult::error("Destructor cannot have parameters", peek_info());
					}
					
					auto [dtor_node, dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(instantiated_name, StringTable::getOrInternStringHandle(dtor_name));
					
					// Parse trailing specifiers (noexcept, override, final, = default, = delete, etc.)
					FlashCpp::MemberQualifiers dtor_member_quals;
					FlashCpp::FunctionSpecifiers dtor_func_specs;
					auto dtor_specs_result = parse_function_trailing_specifiers(dtor_member_quals, dtor_func_specs);
					if (dtor_specs_result.is_error()) {
						return dtor_specs_result;
					}
					
					// Apply specifiers
					if (dtor_func_specs.is_noexcept) {
						dtor_ref.set_noexcept(true);
					}
					
					bool is_defaulted = dtor_func_specs.is_defaulted();
					bool is_deleted = dtor_func_specs.is_deleted();
					
					// Handle defaulted destructors
					if (is_defaulted) {
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after '= default'", peek_info());
						}
						
						auto [block_node, block_ref] = create_node_ref(BlockNode());
						NameMangling::MangledName mangled = NameMangling::generateMangledNameFromNode(dtor_ref);
						dtor_ref.set_mangled_name(mangled);
						dtor_ref.set_definition(block_node);
						
						struct_ref.add_destructor(dtor_node, current_access, dtor_is_virtual);
						continue;
					}
					
					// Handle deleted destructors
					if (is_deleted) {
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after '= delete'", peek_info());
						}
						continue;
					}
					
					// Parse function body if present
					if (peek() == "{"_tok) {
						SaveHandle body_start = save_token_position();
						skip_balanced_braces();
						
						delayed_function_bodies_.push_back({
							nullptr,  // member_func_ref
							body_start,
							{},       // initializer_list_start (not used)
							instantiated_name,
							struct_type_info.type_index_,
							&struct_ref,
							false,    // has_initializer_list
							false,    // is_constructor
							true,     // is_destructor
							nullptr,  // ctor_node
							&dtor_ref,  // dtor_node
							{}  // no template parameter names for specializations
						});
					} else if (!consume(";"_tok)) {
						return ParseResult::error("Expected '{' or ';' after destructor declaration", peek_info());
					}
					
					struct_ref.add_destructor(dtor_node, current_access, dtor_is_virtual);
					continue;
				} else {
					// Not a destructor - restore position so specifiers are not lost
					restore_token_position(dtor_saved_pos);
				}
				} // end destructor check scope

				// Special handling for conversion operators: operator type()
				// Conversion operators don't have a return type, so we need to detect them early
				// Skip specifiers (constexpr, explicit, inline) first, then check for 'operator'
				ParseResult member_result;
				FlashCpp::MemberLeadingSpecifiers conv_specs;
				{
					SaveHandle conv_saved = save_token_position();
					bool found_conversion_op = false;
					conv_specs = parse_member_leading_specifiers();
					if (peek() == "operator"_tok) {
						// Check if this is a conversion operator (not operator() or operator<< etc.)
						// Conversion operators have: operator type-name ()
						SaveHandle op_saved = save_token_position();
						Token operator_keyword_token = peek_info();
						advance(); // consume 'operator'
						
						// If next token is not '(' and not an operator symbol, it's likely a conversion operator
						bool is_conversion = false;
						if (peek() != "("_tok &&
						    !peek().is_operator() &&
						    peek() != "["_tok && peek() != "new"_tok && peek() != "delete"_tok) {
							// Try to parse the target type
							auto type_result = parse_type_specifier();
							if (!type_result.is_error() && type_result.node().has_value()) {
								TypeSpecifierNode& target_type = type_result.node()->as<TypeSpecifierNode>();
								
								// Consume pointer/reference modifiers: operator _Tp&(), operator _Tp*(), etc.
								consume_conversion_operator_target_modifiers(target_type);
								
								// Check for ()
								if (peek() == "("_tok) {
									is_conversion = true;
									
									StringBuilder op_name_builder;
									op_name_builder.append("operator ");
									op_name_builder.append(target_type.getReadableString());
									std::string_view operator_name = op_name_builder.commit();
									
									Token identifier_token = Token(Token::Type::Identifier, operator_name,
									                              operator_keyword_token.line(), operator_keyword_token.column(),
									                              operator_keyword_token.file_index());
									
									ASTNode decl_node = emplace_node<DeclarationNode>(
										type_result.node().value(),
										identifier_token
									);
									
									discard_saved_token(op_saved);
									discard_saved_token(conv_saved);
									member_result = ParseResult::success(decl_node);
									found_conversion_op = true;
								}
							}
						}
						if (!is_conversion) {
							restore_token_position(op_saved);
						}
					}
					if (!found_conversion_op) {
						restore_token_position(conv_saved);
						// Parse member declaration (use same logic as regular struct parsing)
						member_result = parse_type_and_name();
					}
				}
				if (member_result.is_error()) {
					return member_result;
				}

				if (!member_result.node().has_value()) {
					return ParseResult::error("Expected member declaration", peek_info());
				}

				// Check if this is a member function (has '(') or data member
				if (peek() == "("_tok) {
					// This is a member function
					if (!member_result.node()->is<DeclarationNode>()) {
						return ParseResult::error("Expected declaration node for member function", peek_info());
					}

					DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();

					// Parse function declaration with parameters
					auto func_result = parse_function_declaration(decl_node);
					if (func_result.is_error()) {
						return func_result;
					}

					if (!func_result.node().has_value()) {
						return ParseResult::error("Failed to create function declaration node", peek_info());
					}

					FunctionDeclarationNode& func_decl = func_result.node()->as<FunctionDeclarationNode>();
					DeclarationNode& func_decl_node = func_decl.decl_node();

					// Create a new FunctionDeclarationNode with member function info
					auto [member_func_node, member_func_ref] =
						emplace_node_ref<FunctionDeclarationNode>(func_decl_node, instantiated_name);

					// Copy parameters from the parsed function
					for (const auto& param : func_decl.parameter_nodes()) {
						member_func_ref.add_parameter_node(param);
					}

					// Copy function body if it exists
					auto definition_opt = func_decl.get_definition();
					if (definition_opt.has_value()) {
						member_func_ref.set_definition(definition_opt.value());
					}

					// Apply leading specifiers to the member function
					member_func_ref.set_is_constexpr(conv_specs & FlashCpp::MLS_Constexpr);
					member_func_ref.set_is_consteval(conv_specs & FlashCpp::MLS_Consteval);
					member_func_ref.set_inline_always(conv_specs & FlashCpp::MLS_Inline);

					// Parse trailing specifiers (const, volatile, &, &&, noexcept, override, final)
					FlashCpp::MemberQualifiers member_quals;
					FlashCpp::FunctionSpecifiers func_specs;
					auto specs_result = parse_function_trailing_specifiers(member_quals, func_specs);
					if (specs_result.is_error()) {
						return specs_result;
					}

					// Check for function body and use delayed parsing
					if (peek() == "{"_tok) {
						// Save position at start of body
						SaveHandle body_start = save_token_position();

						// Skip over the function body by counting braces
						skip_balanced_braces();

						// Record for delayed parsing
						delayed_function_bodies_.push_back({
							&member_func_ref,
							body_start,
							{},       // initializer_list_start (not used)
							instantiated_name,
							struct_type_info.type_index_,
							&struct_ref,
							false,    // has_initializer_list
							false,  // is_constructor
							false,  // is_destructor
							nullptr,  // ctor_node
							nullptr,  // dtor_node
							{}  // no template parameter names for specializations
						});
					} else {
						// No body - expect semicolon
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected '{' or ';' after member function declaration", peek_info());
						}
					}

					// Add to struct
					struct_ref.add_member_function(
						member_func_node,
						current_access,
						!!(conv_specs & FlashCpp::MLS_Virtual) || func_specs.is_virtual,
						func_specs.is_pure_virtual(),
						func_specs.is_override,
						func_specs.is_final,
						member_quals.is_const(),
						member_quals.is_volatile()
					);
					
					// Also add to StructTypeInfo so out-of-line definitions can find the declaration
					if (struct_info) {
						StringHandle func_name_handle = decl_node.identifier_token().handle();
						struct_info->addMemberFunction(func_name_handle, member_func_node,
							current_access,
							!!(conv_specs & FlashCpp::MLS_Virtual) || func_specs.is_virtual,
							func_specs.is_pure_virtual(), func_specs.is_override, func_specs.is_final);
						// Set const/volatile on the last added member
						if (!struct_info->member_functions.empty()) {
							struct_info->member_functions.back().is_const = member_quals.is_const();
							struct_info->member_functions.back().is_volatile = member_quals.is_volatile();
						}
					}
					
					// Add to AST for code generation
					// Full specializations are not template patterns - they need their member functions emitted
					ast_nodes_.push_back(member_func_node);
				} else {
					// This is a data member
					std::optional<ASTNode> default_initializer;

					// Get the type from the member declaration
					if (!member_result.node()->is<DeclarationNode>()) {
						return ParseResult::error("Expected declaration node for member", peek_info());
					}
					const DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();
					const TypeSpecifierNode& type_spec = decl_node.type_node().as<TypeSpecifierNode>();
					std::optional<size_t> bitfield_width;
					std::optional<ASTNode> bitfield_width_expr;

					// Handle bitfield declarations: int x : 5;
					if (auto width_result = parse_bitfield_width(bitfield_width, &bitfield_width_expr); width_result.is_error()) {
						return width_result;
					}

					// Check for member initialization with '=' (C++11 feature)
					if (peek() == "="_tok) {
						advance(); // consume '='

						// Parse the initializer expression
						auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (init_result.is_error()) {
							return init_result;
						}
						if (init_result.node().has_value()) {
							default_initializer = *init_result.node();
						}
					}

					struct_ref.add_member(*member_result.node(), current_access, default_initializer, bitfield_width, bitfield_width_expr);

					// Handle comma-separated declarations (e.g., int x, y, z;)
					while (peek() == ","_tok) {
						advance(); // consume ','

						// Parse the next member name
						auto next_member_name = advance();
						if (next_member_name.type() != Token::Type::Identifier) {
							return ParseResult::error("Expected member name after comma", peek_info());
						}

						std::optional<size_t> additional_bitfield_width;
						std::optional<ASTNode> additional_bitfield_width_expr;
						// Handle bitfield declarations: int x, y : 3;
						if (auto width_result = parse_bitfield_width(additional_bitfield_width, &additional_bitfield_width_expr); width_result.is_error()) {
							return width_result;
						}

						// Check for optional initialization
						std::optional<ASTNode> additional_init;
						if (peek() == "="_tok) {
							advance(); // consume '='
							auto init_result = parse_expression(2, ExpressionContext::Normal);
							if (init_result.is_error()) {
								return init_result;
							}
							if (init_result.node().has_value()) {
								additional_init = *init_result.node();
							}
						}

						// Create declaration with same type
						ASTNode next_member_decl = emplace_node<DeclarationNode>(
							emplace_node<TypeSpecifierNode>(type_spec),
							next_member_name
						);
						struct_ref.add_member(next_member_decl, current_access, additional_init, additional_bitfield_width, additional_bitfield_width_expr);
					}

					// Consume semicolon
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after member declaration", peek_info());
					}
				}

				// Consumed semicolon above in each branch
			}

			// Expect closing brace
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' after class body", peek_info());
			}

			// Pop member function context
			member_function_context_stack_.pop_back();

			// Skip any attributes after struct/class definition (e.g., __attribute__((__deprecated__)))
			skip_cpp_attributes();

			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after class declaration", peek_info());
			}

			// struct_type_info and struct_info were already created above
			// Attach struct_info to type info if not already done
			if (!struct_type_info.getStructInfo()) {
				// Attach here (after member parsing) so static member helpers above can use
				// the original struct_info pointer without hitting moved-from state.
				struct_type_info.setStructInfo(std::move(struct_info));
				if (struct_type_info.getStructInfo()) {
					struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
				}
			}

			// Get pointer to the struct info to add member information
			StructTypeInfo* struct_info_ptr = struct_type_info.getStructInfo();
			if (!struct_info_ptr) {
				// Defensive guard: if attachment above failed for any reason, bail out
				return ParseResult::error(
					"Internal error: missing struct info for specialization '" +
					std::string(StringTable::getStringView(instantiated_name)) + "'",
					peek_info());
			}

			// Add members to struct info
			for (const auto& member_decl : struct_ref.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

				// Calculate member size and alignment
				auto [member_size, member_alignment] = calculateMemberSizeAndAlignment(type_spec);
				size_t referenced_size_bits = type_spec.size_in_bits();

				if (type_spec.type() == Type::Struct) {
					const TypeInfo* member_type_info = nullptr;
					for (const auto& ti : gTypeInfo) {
						if (ti.type_index_ == type_spec.type_index()) {
							member_type_info = &ti;
							break;
						}
					}
					if (member_type_info && member_type_info->getStructInfo()) {
						member_size = member_type_info->getStructInfo()->total_size;
						referenced_size_bits = static_cast<size_t>(member_type_info->getStructInfo()->total_size * 8);
						member_alignment = member_type_info->getStructInfo()->alignment;
					}
				}

				ReferenceQualifier ref_qual = type_spec.reference_qualifier();
				if (ref_qual != ReferenceQualifier::None) {
					// Size and alignment were already set correctly above for references
					referenced_size_bits = referenced_size_bits ? referenced_size_bits : type_spec.size_in_bits();
				}
				// Phase 7B: Intern member name and use StringHandle overload
				StringHandle member_name_handle = decl.identifier_token().handle();
				struct_info_ptr->addMember(
					member_name_handle,
					type_spec.type(),
					type_spec.type_index(),
					member_size,
					member_alignment,
					member_decl.access,
					member_decl.default_initializer,
					ref_qual,
					referenced_size_bits,
					false,
					{},
					static_cast<int>(type_spec.pointer_depth()),
					member_decl.bitfield_width
				);
			}

			// Add member functions to struct info
			bool has_constructor = false;
			for (const auto& member_func_decl : struct_ref.member_functions()) {
				if (member_func_decl.is_constructor) {
					has_constructor = true;
					// Add constructor to struct type info
					struct_info_ptr->addConstructor(
						member_func_decl.function_declaration,
						member_func_decl.access
					);
				} else if (member_func_decl.is_destructor) {
					// Add destructor to struct type info
					struct_info_ptr->addDestructor(
						member_func_decl.function_declaration,
						member_func_decl.access,
						member_func_decl.is_virtual
					);
				} else {
					const FunctionDeclarationNode* func_decl = get_function_decl_node(member_func_decl.function_declaration);
					if (!func_decl) {
						continue;  // Skip if we can't get the function declaration
					}
					const DeclarationNode& decl = func_decl->decl_node();

					// Phase 7B: Intern function name and use StringHandle overload
					StringHandle func_name_handle = decl.identifier_token().handle();
					struct_info_ptr->addMemberFunction(
						func_name_handle,
						member_func_decl.function_declaration,
						member_func_decl.access,
						member_func_decl.is_virtual,
						member_func_decl.is_pure_virtual,
						member_func_decl.is_override,
						member_func_decl.is_final
					);
				}
			}

			// If no constructor was found, mark that we need a default one
			struct_info_ptr->needs_default_constructor = !has_constructor;
			FLASH_LOG(Templates, Debug, "Full spec ", instantiated_name, " has_constructor=", has_constructor);

			// Finalize the struct layout with base classes
			bool finalize_success;
			struct_info_ptr->has_deferred_base_classes = !struct_ref.deferred_template_base_classes().empty();
			if (!struct_ref.base_classes().empty()) {
				finalize_success = struct_info_ptr->finalizeWithBases();
			} else {
				finalize_success = struct_info_ptr->finalize();
			}
			
			// Check for semantic errors during finalization
			if (!finalize_success) {
				return ParseResult::error(struct_info_ptr->getFinalizationError(), Token());
			}

			// Parse delayed function bodies for specialization member functions
			SaveHandle position_after_struct = save_token_position();
			for (auto& delayed : delayed_function_bodies_) {
				// Restore token position to the start of the function body
				restore_token_position(delayed.body_start);

				// Set up function context
				gSymbolTable.enter_scope(ScopeType::Function);
				member_function_context_stack_.push_back({
					delayed.struct_name,
					delayed.struct_type_index,
					delayed.struct_node,
					nullptr  // local_struct_info - not needed for delayed function bodies
				});

				// Set up template parameter names if this is a template member
				std::vector<StringHandle> saved_param_names;
				if (!delayed.template_param_names.empty()) {
					saved_param_names = std::move(current_template_param_names_);
					current_template_param_names_ = delayed.template_param_names;
					parsing_template_body_ = true;
				}

				// Add function parameters to scope (handling constructors, destructors, and regular functions)
				if (delayed.is_constructor && delayed.ctor_node) {
					for (const auto& param : delayed.ctor_node->parameter_nodes()) {
						if (param.is<DeclarationNode>()) {
							const auto& param_decl = param.as<DeclarationNode>();
							gSymbolTable.insert(param_decl.identifier_token().value(), param);
						}
					}
				} else if (!delayed.is_destructor && delayed.func_node) {
					for (const auto& param : delayed.func_node->parameter_nodes()) {
						if (param.is<DeclarationNode>()) {
							const auto& param_decl = param.as<DeclarationNode>();
							gSymbolTable.insert(param_decl.identifier_token().value(), param);
						}
					}
				}
				// Destructors have no parameters

				// Parse the function body
				auto block_result = parse_block();

				// Restore template parameter names
				if (!delayed.template_param_names.empty()) {
					current_template_param_names_ = std::move(saved_param_names);
					parsing_template_body_ = false;
				}

				if (block_result.is_error()) {
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					return block_result;
				}

				if (auto block = block_result.node()) {
					if (delayed.is_constructor && delayed.ctor_node) {
						delayed.ctor_node->set_definition(*block);
					} else if (delayed.is_destructor && delayed.dtor_node) {
						delayed.dtor_node->set_definition(*block);
					} else if (delayed.func_node) {
						delayed.func_node->set_definition(*block);
					}
				}

				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
			}

			// Clear delayed function bodies
			delayed_function_bodies_.clear();

			// Restore position after struct
			restore_token_position(position_after_struct);

			// Register the specialization
			// NOTE:
			// At this point we have parsed a specialization of the primary template.
			// Two forms are supported:
			//  - Full/Exact specialization: template<> struct Container<bool> { ... };
			//  - Partial specialization   : template<typename T> struct Container<T*> { ... };
			//
			// Full specializations:
			//   - template_params is empty (template<>)
			//   - template_args holds fully concrete TemplateTypeArg values (e.g., bool)
			//   - We must register an exact specialization that will be preferred for a
			//     matching instantiation (e.g., Container<bool>).
			//
			// Partial specializations:
			//   - template_params is non-empty (e.g., <typename T>)
			//   - template_args/pattern_args use TemplateTypeArg to encode the pattern
			//     (T*, T&, const T, etc.) and are handled via pattern matching.
			//
			// Implementation:
			//   - If template_params is empty, treat as full specialization and register
			//     via registerSpecialization().
			//   - Otherwise, treat as partial specialization pattern and register via
			//     registerSpecializationPattern().
			if (template_params.empty()) {
				// Full specialization: exact match on concrete arguments
				gTemplateRegistry.registerSpecialization(template_name, template_args, struct_node);
			} else {
				// Partial specialization: register as a pattern for matching
				gTemplateRegistry.registerSpecializationPattern(template_name, template_params, template_args, struct_node);
			}
		
			// Reset parsing context flags
			parsing_template_class_ = false;
			parsing_template_body_ = false;
			current_template_param_names_.clear();
		
			// Don't add specialization to AST - it's stored in the template registry
			// and will be used when Container<int> is instantiated
			return saved_position.success();
		}
		
		// Handle partial specialization (template<typename T> struct X<T&>)
		if (is_partial_specialization) {
			// Save position before struct/class keyword — used if this turns out to be an
			// out-of-line nested class definition so parse_struct_declaration() can re-parse it
			SaveHandle struct_keyword_pos = save_token_position();

			// Parse the struct/class/union keyword
			bool is_class = consume("class"_tok);
			bool is_union = false;
			if (!is_class) {
				if (!consume("struct"_tok)) {
					is_union = consume("union"_tok);
				}
			}
			
			// Parse class name
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected class name", current_token_);
			}
			
			Token class_name_token = peek_info();
			std::string_view template_name = class_name_token.value();
			advance();
			
			// Parse the specialization pattern: <T&>, <T*, U>, etc.
			auto pattern_args_opt = parse_explicit_template_arguments();
			if (!pattern_args_opt.has_value()) {
				return ParseResult::error("Expected template argument pattern in partial specialization", current_token_);
			}
			
			std::vector<TemplateTypeArg> pattern_args = *pattern_args_opt;
			
			// Check for out-of-line member class definition: template<...> class Foo<...>::Bar { ... }
			// E.g., template<typename _CharT, typename _Traits>
			//        class basic_ostream<_CharT, _Traits>::sentry { ... };
			// This defines a nested class member of a class template outside the class body.
			// Register it so the body is re-parsed during template instantiation.
			if (peek() == "::"_tok) {
				auto scope_check = save_token_position();
				advance(); // consume '::'
				if (peek().is_identifier()) {
					discard_saved_token(scope_check);
					std::string_view member_class_name = peek_info().value();
					advance(); // consume member class name
					FLASH_LOG_FORMAT(Templates, Debug, "Out-of-line member class definition: {}::{}",
					                 template_name, member_class_name);
					
					// Skip base class list if present
					if (peek() == ":"_tok) {
						advance();
						while (!peek().is_eof() && peek() != "{"_tok && peek() != ";"_tok) {
							advance();
						}
					}
					
					// Skip body if present
					if (peek() == "{"_tok) {
						skip_balanced_braces();
					}
					
					// Consume trailing semicolon
					consume(";"_tok);
					
					// Register the out-of-line nested class definition
					// struct_keyword_pos points at the struct/class keyword so parse_struct_declaration()
					// can re-parse "struct Wrapper<T>::Nested { ... }" during instantiation.
					// Partial specializations leave specialization_args empty — applies to all instantiations.
					gTemplateRegistry.registerOutOfLineNestedClass(template_name, OutOfLineNestedClass{
						template_params,
						StringTable::getOrInternStringHandle(member_class_name),
						struct_keyword_pos, template_param_names, is_class,
						{}  // no specialization args — applies to all instantiations
					});
					FLASH_LOG_FORMAT(Templates, Debug, "Registered out-of-line nested class: {}::{}",
					                 template_name, member_class_name);
					
					// Clean up template parameter context
					current_template_param_names_.clear();
					parsing_template_class_ = false;
					parsing_template_body_ = false;
					
					return saved_position.success();
				}
				// Not an identifier after '::' - restore parser position
				restore_token_position(scope_check);
			}
			
			// struct_keyword_pos was only needed for OOL nested class registration above;
			// discard it so it doesn't leak in all other partial specialization paths.
			discard_saved_token(struct_keyword_pos);
			
			// Generate a unique name for the pattern template
			// We use the template parameter names + modifiers to create unique pattern names
			// E.g., Container<T*> -> Container_pattern_TP
			//       Container<T**> -> Container_pattern_TPP
			//       Container<T&> -> Container_pattern_TR
			StringBuilder pattern_name_builder;
			pattern_name_builder.append(template_name).append("_pattern");
			for (const auto& arg : pattern_args) {
				// Add modifiers to make pattern unique
				pattern_name_builder.append("_");
				// Add pointer markers
				for (size_t i = 0; i < arg.pointer_depth; ++i) {
					pattern_name_builder.append("P");
				}
				// Add array marker
				if (arg.is_array) {
					pattern_name_builder.append("A");
					if (arg.array_size.has_value()) {
						pattern_name_builder.append("[").append(static_cast<int64_t>(*arg.array_size)).append("]");
					}
				}
				if (arg.member_pointer_kind == MemberPointerKind::Object) {
					pattern_name_builder.append("MPO");
				} else if (arg.member_pointer_kind == MemberPointerKind::Function) {
					pattern_name_builder.append("MPF");
				}
				// Add reference markers
				if (arg.is_rvalue_reference()) {
					pattern_name_builder.append("RR");
				} else if (arg.is_reference()) {
					pattern_name_builder.append("R");
				}
				// Add const/volatile markers
				if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0) {
					pattern_name_builder.append("C");
				}
				if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0) {
					pattern_name_builder.append("V");
				}
			}
			auto instantiated_name = StringTable::getOrInternStringHandle(pattern_name_builder);
			
			// Register this as a pattern struct name for O(1) lookup
			gTemplateRegistry.registerPatternStructName(instantiated_name);
			
			// Create a struct node for this specialization
			auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(
				instantiated_name,
				is_class,
				is_union
			);
			
			// Create struct type info early so we can add base classes
			TypeInfo& struct_type_info = add_struct_type(instantiated_name);
			
			// Mark as template instantiation with the base template name
			// This allows constructor detection (e.g., template<typename U> allocator(const allocator<U>&))
			// to find the base template name and match it against the constructor name
			struct_type_info.setTemplateInstantiationInfo(
				QualifiedIdentifier::fromQualifiedName(template_name, gSymbolTable.get_current_namespace_handle()), {});
			
			// Create StructTypeInfo for this specialization
			auto struct_info = std::make_unique<StructTypeInfo>(instantiated_name, struct_ref.default_access());
			struct_info->is_union = is_union;
			
			// Parse base class list (if present): : public Base1, private Base2
			if (peek() == ":"_tok) {
				advance();  // consume ':'

				do {
					// Parse virtual keyword (optional)
					bool is_virtual_base = false;
					if (peek() == "virtual"_tok) {
						is_virtual_base = true;
						advance();
					}

					// Parse access specifier (optional, defaults to public for struct, private for class)
					AccessSpecifier base_access = is_class ? AccessSpecifier::Private : AccessSpecifier::Public;

					if (peek().is_keyword()) {
						std::string_view keyword = peek_info().value();
						if (keyword == "public") {
							base_access = AccessSpecifier::Public;
							advance();
						} else if (keyword == "protected") {
							base_access = AccessSpecifier::Protected;
							advance();
						} else if (keyword == "private") {
							base_access = AccessSpecifier::Private;
							advance();
						}
					}

					// Check for virtual keyword after access specifier
					if (!is_virtual_base && peek() == "virtual"_tok) {
						is_virtual_base = true;
						advance();
					}

					// Parse base class name - could be qualified like ns::Base or simple like Base
					auto base_name_token = advance();
					if (base_name_token.type() != Token::Type::Identifier) {
						return ParseResult::error("Expected base class name", base_name_token);
					}

					std::string base_class_name_str{base_name_token.value()};
					
					// Check for qualified name (e.g., ns::Base or ns::inner::Base)
					while (peek() == "::"_tok) {
						advance(); // consume '::'
						
						if (!peek().is_identifier()) {
							return ParseResult::error("Expected identifier after '::'", peek_info());
						}
						auto next_name_token = advance(); // consume the identifier
						
						base_class_name_str += "::";
						base_class_name_str += next_name_token.value();
						base_name_token = next_name_token;  // Update for error reporting
						
						FLASH_LOG_FORMAT(Parser, Debug, "Parsing qualified base class name: {}", base_class_name_str);
					}
					
					std::string_view base_class_name = StringTable::getOrInternStringHandle(StringBuilder().append(base_class_name_str)).view();
					
					// Check if this is a template base class (e.g., Base<T>)
					if (peek() == "<"_tok) {
						// Parse template arguments, collecting AST nodes for deferred resolution
						std::vector<ASTNode> template_arg_nodes;
						auto template_args_opt = parse_explicit_template_arguments(&template_arg_nodes);
						if (!template_args_opt.has_value()) {
							return ParseResult::error("Failed to parse template arguments for base class", peek_info());
						}
						
						std::vector<TemplateTypeArg> template_args = *template_args_opt;
						
						// Check if any template arguments are dependent or pack expansions
						bool has_dependent_args = false;
						for (const auto& arg : template_args) {
							if (arg.is_dependent || arg.is_pack) {
								has_dependent_args = true;
								break;
							}
						}
						
						// If template arguments are dependent, we're inside a template declaration
						// Defer base class resolution until template instantiation
						if (has_dependent_args) {
							FLASH_LOG_FORMAT(Templates, Debug, "Base class {} has dependent template arguments - deferring resolution", base_class_name);
							
							// Build TemplateArgumentNodeInfo structures for deferred resolution
							std::vector<TemplateArgumentNodeInfo> arg_infos;
							arg_infos.reserve(template_args.size());
							for (size_t i = 0; i < template_args.size(); ++i) {
								TemplateArgumentNodeInfo info;
								info.is_pack = template_args[i].is_pack;
								info.is_dependent = template_args[i].is_dependent;
								if (i < template_arg_nodes.size()) {
									info.node = template_arg_nodes[i];
								}
								arg_infos.push_back(std::move(info));
							}
							
							StringHandle template_name_handle = StringTable::getOrInternStringHandle(base_class_name);
							struct_ref.add_deferred_template_base_class(template_name_handle, std::move(arg_infos), std::nullopt, base_access, is_virtual_base);
							continue;  // Skip to next base class or exit loop
						}
						
						// Instantiate base class template if needed and register in AST
						instantiate_and_register_base_template(base_class_name, template_args);
					}

					// Validate and add the base class
					ParseResult result = validate_and_add_base_class(base_class_name, struct_ref, struct_info.get(), base_access, is_virtual_base, base_name_token);
					if (result.is_error()) {
						return result;
					}
				} while (consume(","_tok));
			}
			
			// Handle stray member access tokens (e.g., ::type) that weren't consumed earlier
			while ((current_token_.value() == "::") ||
			       (peek() == "::"_tok)) {
				if (current_token_.value() == "::") {
					// Current token is '::' - consume following identifier
					if (peek().is_identifier()) {
						advance(); // consume identifier
					} else {
						break;
					}
				} else {
					advance(); // consume '::'
					if (peek().is_identifier()) {
						advance(); // consume identifier
					} else {
						break;
					}
				}
			}


			// Check for forward declaration: template<typename T> struct Name<T*>;
			if (peek() == ";"_tok) {
				advance(); // consume ';'
				
				// Register the partial specialization pattern in the template registry
				// This allows the template to be found when instantiated
				std::vector<std::string_view> param_names_view;
				for (const auto& name : template_param_names) {
					param_names_view.push_back(StringTable::getStringView(name));
				}
				auto template_class_node = emplace_node<TemplateClassDeclarationNode>(
					template_params,
					std::move(param_names_view),
					struct_node
				);
				
				// Build pattern key for lookup
				StringBuilder pattern_key;
				pattern_key.append(template_name).append("_pattern");
				for (const auto& arg : pattern_args) {
					pattern_key.append("_");
					for (size_t i = 0; i < arg.pointer_depth; ++i) {
						pattern_key.append("P");
					}
					if (arg.is_rvalue_reference()) {
						pattern_key.append("RR");
					} else if (arg.is_reference()) {
						pattern_key.append("R");
					}
				}
				std::string_view pattern_key_view = pattern_key.commit();
				
				gTemplateRegistry.registerSpecialization(template_name, pattern_args, template_class_node);
				FLASH_LOG_FORMAT(Parser, Debug, "Registered forward declaration for partial specialization: {} with pattern {}", template_name, pattern_key_view);
				
				// Clean up template parameter context
				current_template_param_names_.clear();
				parsing_template_body_ = false;
				
				return saved_position.success(template_class_node);
			}
			
			// Ensure we're positioned at the specialization body even if complex base parsing left extra tokens
			while (peek() != "{"_tok && peek() != ";"_tok) {
				advance();
			}
			
			// Check again for forward declaration after consuming any extra tokens
			if (peek() == ";"_tok) {
				advance(); // consume ';'
				
				std::vector<std::string_view> param_names_view2;
				for (const auto& name : template_param_names) {
					param_names_view2.push_back(StringTable::getStringView(name));
				}
				auto template_class_node = emplace_node<TemplateClassDeclarationNode>(
					template_params,
					std::move(param_names_view2),
					struct_node
				);
				
				gTemplateRegistry.registerSpecialization(template_name, pattern_args, template_class_node);
				FLASH_LOG_FORMAT(Parser, Debug, "Registered forward declaration for partial specialization (after extra tokens): {}", template_name);
				
				current_template_param_names_.clear();
				parsing_template_body_ = false;
				
				return saved_position.success(template_class_node);
			}

			// Expect opening brace
			if (!consume("{"_tok)) {
				return ParseResult::error("Expected '{' or ';' after partial specialization header", peek_info());
			}
			
			AccessSpecifier current_access = struct_ref.default_access();
			
			// Set up member function context
			member_function_context_stack_.push_back({
				instantiated_name,
				struct_type_info.type_index_,
				&struct_ref,
				nullptr  // local_struct_info - not needed during template instantiation
			});
			
			// Set up struct parsing context for inherited member lookups (e.g., _S_test from base class)
			// This enables using type = decltype(_S_test<_Tp1, _Tp2>(0)); to find _S_test in base classes
			// BUGFIX: Pass local_struct_info for static member visibility in template partial specializations
			// This fixes the issue where static constexpr members (e.g., __g, __d2) are not visible
			// when used as template arguments in typedef declarations within the same struct body
			struct_parsing_context_stack_.push_back({
				StringTable::getStringView(instantiated_name),
				&struct_ref,
				struct_info.get(),
				gSymbolTable.get_current_namespace_handle(),
				{}
			});
			
			// Parse class body (same as full specialization)
			while (!peek().is_eof() && peek() != "}"_tok) {
				// Skip empty declarations (bare ';' tokens) - valid in C++
				if (peek() == ";"_tok) {
					advance();
					continue;
				}
				
				// Check for access specifiers
				if (peek().is_keyword()) {
					if (peek() == "public"_tok) {
						advance();
						if (!consume(":"_tok)) {
							return ParseResult::error("Expected ':' after 'public'", peek_info());
						}
						current_access = AccessSpecifier::Public;
						continue;
					} else if (peek() == "private"_tok) {
						advance();
						if (!consume(":"_tok)) {
							return ParseResult::error("Expected ':' after 'private'", peek_info());
						}
						current_access = AccessSpecifier::Private;
						continue;
					} else if (peek() == "protected"_tok) {
						advance();
						if (!consume(":"_tok)) {
							return ParseResult::error("Expected ':' after 'protected'", peek_info());
						}
						current_access = AccessSpecifier::Protected;
						continue;
					} else if (peek() == "enum"_tok) {
						// Handle enum declaration inside partial specialization
						auto enum_result = parse_enum_declaration();
						if (enum_result.is_error()) {
							return enum_result;
						}
						// Note: nested_enum_indices_ tracking is not done here for template class bodies.
						// Enums are registered globally by parse_enum_declaration, and enumerators are
						// typically resolved via the global symbol table before the struct-scoped fallback.
						continue;
					} else if (peek() == "struct"_tok || peek() == "class"_tok) {
						// Handle nested struct/class declarations inside partial specialization body
						// e.g., struct __type { ... };
						// e.g., class _Sp_counted_ptr final : public _Sp_counted_base<_Lp> { ... };
						advance(); // consume 'struct' or 'class'
						
						// Skip C++11 attributes
						skip_cpp_attributes();
						
						// Skip struct name if present
						if (peek().is_identifier()) {
							advance(); // consume struct name
						}
						
						// Skip template arguments if present (e.g., struct Wrapper<int>)
						if (peek() == "<"_tok) {
							skip_template_arguments();
						}
						
						// Skip 'final' specifier if present
						if (peek() == "final"_tok) {
							advance();
						}
						
						// Skip base class list if present (e.g., : public Base<T>)
						if (peek() == ":"_tok) {
							advance(); // consume ':'
							while (!peek().is_eof() && peek() != "{"_tok && peek() != ";"_tok) {
								advance();
							}
						}
						
						// Skip to body or semicolon
						if (peek() == "{"_tok) {
							skip_balanced_braces();
						}
						
						// Consume trailing semicolon
						if (peek() == ";"_tok) {
							advance();
						}
						continue;
					} else if (peek() == "static"_tok) {
						// Handle static members: static const int size = 10;
						advance(); // consume "static"
						
						auto static_result = parse_static_member_block(instantiated_name, struct_ref, 
						                                                 struct_info.get(), current_access, 
						                                                 current_template_param_names_, /*use_struct_type_info=*/false);
						if (static_result.is_error()) {
							return static_result;
						}
						continue;
					} else if (peek() == "using"_tok) {
						// Handle type alias inside partial specialization: using _Type = T;
						auto alias_result = parse_member_type_alias("using", &struct_ref, current_access);
						if (alias_result.is_error()) {
							return alias_result;
						}
						continue;
					} else if (peek() == "typedef"_tok) {
						// Handle typedef inside partial specialization: typedef T _Type;
						auto alias_result = parse_member_type_alias("typedef", &struct_ref, current_access);
						if (alias_result.is_error()) {
							return alias_result;
						}
						continue;
					} else if (peek() == "template"_tok) {
						// Handle member function template or member template alias
						auto template_result = parse_member_template_or_function(struct_ref, current_access);
						if (template_result.is_error()) {
							return template_result;
						}
						continue;
					} else if (peek() == "static_assert"_tok) {
						// Handle static_assert inside partial specialization body
						auto static_assert_result = parse_static_assert();
						if (static_assert_result.is_error()) {
							return static_assert_result;
						}
						continue;
					} else if (peek() == "constexpr"_tok || 
					           peek() == "consteval"_tok ||
					           peek() == "inline"_tok ||
					           peek() == "explicit"_tok) {
						// Handle constexpr/consteval/inline/explicit before constructor or member function
						// Consume the specifier and continue to constructor/member check below
					}
				}
				
				// Check for constexpr, consteval, inline, explicit specifiers (can appear on constructors and member functions)
				[[maybe_unused]] auto partial_member_specs = parse_member_leading_specifiers();
				
				// Check for constructor (identifier matching template name followed by '('
				// In partial specializations, the constructor uses the base template name (e.g., "Calculator"),
				// not the instantiated pattern name (e.g., "Calculator_pattern_P")
				SaveHandle saved_pos = save_token_position();
				if (!peek().is_eof() && peek().is_identifier() &&
				    peek_info().value() == template_name) {
					// Look ahead to see if this is a constructor (next token is '(')
					Token name_token = advance();
					if (name_token.type() == Token::Type::EndOfFile) {
						return ParseResult::error("Expected constructor name", Token());
					}
					std::string_view ctor_name = name_token.value();
					
					if (peek() == "("_tok) {
						// Discard saved position since we're using this as a constructor
						discard_saved_token(saved_pos);
						
						// This is a constructor - use instantiated_name as the struct name
						auto [ctor_node, ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(instantiated_name, StringTable::getOrInternStringHandle(ctor_name));
						
						// Parse parameters using unified parse_parameter_list (Phase 1)
						FlashCpp::ParsedParameterList params;
						auto param_result = parse_parameter_list(params);
						if (param_result.is_error()) {
							return param_result;
						}
						for (const auto& param : params.parameters) {
							ctor_ref.add_parameter_node(param);
						}
						
						// Enter a temporary scope for parsing the initializer list
						gSymbolTable.enter_scope(ScopeType::Function);
						
						// Register parameters in symbol table using helper (Phase 5)
						register_parameters_in_scope(ctor_ref.parameter_nodes());
						
						// Parse exception specifier (noexcept or throw()) before initializer list
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
							advance();  // consume ':'
							
							while (peek() != "{"_tok &&
							       peek() != ";"_tok) {
								auto init_name_token = advance();
								if (init_name_token.type() != Token::Type::Identifier) {
									return ParseResult::error("Expected member or base class name in initializer list", init_name_token);
								}
								
								std::string_view init_name = init_name_token.value();
								
								// Handle namespace-qualified base class names: std::optional<_Tp>{...}
								while (peek() == "::"_tok) {
									advance(); // consume '::'
									if (peek().is_identifier() || peek().is_keyword()) {
										advance(); // consume the qualified name part
									}
								}
								
								// Check for template arguments: Tuple<Rest...>(...)
								if (peek() == "<"_tok) {
									// Parse and skip template arguments - they're part of the base class name
									auto template_args_opt = parse_explicit_template_arguments();
									if (!template_args_opt.has_value()) {
										return ParseResult::error("Failed to parse template arguments in initializer", peek_info());
									}
									// Modify init_name to include instantiated template name if needed
									// For now, we just consume the template arguments and continue
								}
								
								bool is_paren = peek() == "("_tok;
								bool is_brace = peek() == "{"_tok;
								
								if (!is_paren && !is_brace) {
									return ParseResult::error("Expected '(' or '{' after initializer name", peek_info());
								}
								
								advance();  // consume '(' or '{'
								TokenKind close_kind = [is_paren]() { if (is_paren) return ")"_tok; return "}"_tok; }();
								
								std::vector<ASTNode> init_args;
								if (peek() != close_kind) {
									do {
										ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (arg_result.is_error()) {
											return arg_result;
										}
										if (auto arg_node = arg_result.node()) {
											// Check for pack expansion: expr...
											if (peek() == "..."_tok) {
												advance(); // consume '...'
												// Mark this as a pack expansion - actual expansion happens at instantiation
											}
											init_args.push_back(*arg_node);
										}
									} while (consume(","_tok));
								}
								
								if (!consume(close_kind)) {
									return ParseResult::error(is_paren ?
									    "Expected ')' after initializer arguments" :
									    "Expected '}' after initializer arguments", peek_info());
								}
								
								// Member initializer
								if (!init_args.empty()) {
									ctor_ref.add_member_initializer(init_name, init_args[0]);
								}
								
								if (!consume(","_tok)) {
									break;
								}
							}
						}
						
						// Check for = default or = delete
						bool is_defaulted = false;
						bool is_deleted = false;
						if (peek() == "="_tok) {
							advance(); // consume '='
							
							if (peek().is_keyword()) {
								if (peek() == "default"_tok) {
									advance();
									is_defaulted = true;
									
									if (!consume(";"_tok)) {
										gSymbolTable.exit_scope();
										return ParseResult::error("Expected ';' after '= default'", peek_info());
									}
									
									ctor_ref.set_is_implicit(true);
									auto [block_node, block_ref] = create_node_ref(BlockNode());
									ctor_ref.set_definition(block_node);
									gSymbolTable.exit_scope();
								} else if (peek() == "delete"_tok) {
									advance();
									is_deleted = true;

									if (!consume(";"_tok)) {
										gSymbolTable.exit_scope();
										return ParseResult::error("Expected ';' after '= delete'", peek_info());
									}

									// Determine what kind of constructor this is based on parameters
									size_t num_params = ctor_ref.parameter_nodes().size();
									bool is_copy_ctor = false;
									bool is_move_ctor = false;

									if (num_params == 1) {
										// Check if the parameter is a reference to this type
										const auto& param = ctor_ref.parameter_nodes()[0];
										if (param.is<DeclarationNode>()) {
											const auto& param_decl = param.as<DeclarationNode>();
											const auto& type_node = param_decl.type_node();
											if (type_node.has_value() && type_node.is<TypeSpecifierNode>()) {
												const auto& type_spec = type_node.as<TypeSpecifierNode>();
												std::string_view param_type_name = type_spec.token().value();
												// For template specializations, match against base template name
												if (param_type_name == template_name ||
												    param_type_name == instantiated_name) {
													if (type_spec.is_rvalue_reference()) {
														is_move_ctor = true;
													} else if (type_spec.is_reference()) {
														is_copy_ctor = true;
													}
												}
											}
										}
									}

									// Mark the deleted constructor in the struct AST node
									if (is_copy_ctor) {
										struct_ref.mark_deleted_copy_constructor();
										FLASH_LOG(Templates, Debug, "Marked copy constructor as deleted in struct: ", instantiated_name);
									} else if (is_move_ctor) {
										struct_ref.mark_deleted_move_constructor();
										FLASH_LOG(Templates, Debug, "Marked move constructor as deleted in struct: ", instantiated_name);
									} else {
										// Default constructor (no params or only optional params)
										struct_ref.mark_deleted_default_constructor();
										FLASH_LOG(Templates, Debug, "Marked default constructor as deleted in struct: ", instantiated_name);
									}

									gSymbolTable.exit_scope();
									continue;
								} else {
									gSymbolTable.exit_scope();
									return ParseResult::error("Expected 'default' or 'delete' after '='", peek_info());
								}
							} else {
								gSymbolTable.exit_scope();
								return ParseResult::error("Expected 'default' or 'delete' after '='", peek_info());
							}
						}
						
						// Parse constructor body if present
						if (!is_defaulted && !is_deleted && peek() == "{"_tok) {
							SaveHandle body_start = save_token_position();
							
							auto type_it = gTypesByName.find(instantiated_name);
							size_t struct_type_index = 0;
							if (type_it != gTypesByName.end()) {
								struct_type_index = type_it->second->type_index_;
							}
							
							skip_balanced_braces();
							gSymbolTable.exit_scope();
							
							delayed_function_bodies_.push_back({
								nullptr,
								body_start,
								{},
								instantiated_name,
								struct_type_index,
								&struct_ref,
								false,    // has_initializer_list
								true,  // is_constructor
								false,
								&ctor_ref,
								nullptr,
								{}  // template_param_names
							});
						} else if (!is_defaulted && !is_deleted && !consume(";"_tok)) {
							gSymbolTable.exit_scope();
							return ParseResult::error("Expected '{', ';', '= default', or '= delete' after constructor declaration", peek_info());
						} else if (!is_defaulted && !is_deleted) {
							gSymbolTable.exit_scope();
						}
						
						struct_ref.add_constructor(ctor_node, current_access);
						continue;
					} else {
						// Not a constructor, restore position
						restore_token_position(saved_pos);
					}
				} else {
					discard_saved_token(saved_pos);
				}
				
				// Check for destructor (~StructName followed by '(')
				if (peek() == "~"_tok) {
					advance();  // consume '~'
					
					auto name_token_opt = advance();
					if (name_token_opt.type() != Token::Type::Identifier ||
					    name_token_opt.value() != template_name) {
						return ParseResult::error("Expected struct name after '~' in destructor", name_token_opt);
					}
					Token dtor_name_token = name_token_opt;
					std::string_view dtor_name = dtor_name_token.value();
					
					if (!consume("("_tok)) {
						return ParseResult::error("Expected '(' after destructor name", peek_info());
					}
					
					if (!consume(")"_tok)) {
						return ParseResult::error("Destructor cannot have parameters", peek_info());
					}
					
					auto [dtor_node, dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(instantiated_name, StringTable::getOrInternStringHandle(dtor_name));
					
					// Parse trailing specifiers (noexcept, override, final, = default, = delete, etc.)
					FlashCpp::MemberQualifiers dtor_member_quals;
					FlashCpp::FunctionSpecifiers dtor_func_specs;
					auto dtor_specs_result = parse_function_trailing_specifiers(dtor_member_quals, dtor_func_specs);
					if (dtor_specs_result.is_error()) {
						return dtor_specs_result;
					}
					
					// Apply specifiers
					if (dtor_func_specs.is_noexcept) {
						dtor_ref.set_noexcept(true);
					}
					
					bool is_defaulted = dtor_func_specs.is_defaulted();
					bool is_deleted = dtor_func_specs.is_deleted();
					
					// Handle defaulted destructors
					if (is_defaulted) {
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after '= default'", peek_info());
						}
						
						// Create an empty block for the destructor body
						auto [block_node, block_ref] = create_node_ref(BlockNode());
						NameMangling::MangledName mangled = NameMangling::generateMangledNameFromNode(dtor_ref);
						dtor_ref.set_mangled_name(mangled);
						dtor_ref.set_definition(block_node);
						
						struct_ref.add_destructor(dtor_node, current_access);
						continue;
					}
					
					// Handle deleted destructors
					if (is_deleted) {
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after '= delete'", peek_info());
						}
						// Deleted destructors are not added to the struct
						continue;
					}
					
					// Parse function body if present (and not defaulted/deleted)
					if (peek() == "{"_tok) {
						// Save position at start of body
						SaveHandle body_start = save_token_position();
						
						// Skip over the function body by counting braces
						skip_balanced_braces();
						
						// Record for delayed parsing
						delayed_function_bodies_.push_back({
							nullptr,  // member_func_ref
							body_start,
							{},       // initializer_list_start (not used)
							instantiated_name,
							struct_type_info.type_index_,
							&struct_ref,
							false,    // has_initializer_list
							false,    // is_constructor
							true,     // is_destructor
							nullptr,  // ctor_node
							&dtor_ref,  // dtor_node
							{}  // no template parameter names for specializations
						});
					} else if (!consume(";"_tok)) {
						return ParseResult::error("Expected '{' or ';' after destructor declaration", peek_info());
					}
					
					struct_ref.add_destructor(dtor_node, current_access);
					continue;
				}
				
				// Special handling for conversion operators: operator type()
				// Conversion operators don't have a return type, so we need to detect them early
				// Skip specifiers (constexpr, explicit, inline) first, then check for 'operator'
				ParseResult member_result;
				FlashCpp::MemberLeadingSpecifiers conv_specs;
				{
					SaveHandle conv_saved = save_token_position();
					bool found_conversion_op = false;
					conv_specs = parse_member_leading_specifiers();
					if (peek() == "operator"_tok) {
						// Check if this is a conversion operator (not operator() or operator<< etc.)
						SaveHandle op_saved = save_token_position();
						Token operator_keyword_token = peek_info();
						advance(); // consume 'operator'
						
						// If next token is not '(' and not an operator symbol, it's likely a conversion operator
						bool is_conversion = false;
						if (peek() != "("_tok &&
						    !peek().is_operator() &&
						    peek() != "["_tok && peek() != "new"_tok && peek() != "delete"_tok) {
							// Try to parse the target type
							auto type_result = parse_type_specifier();
							if (!type_result.is_error() && type_result.node().has_value()) {
								TypeSpecifierNode& target_type = type_result.node()->as<TypeSpecifierNode>();
								
								// Consume pointer/reference modifiers: operator _Tp&(), operator _Tp*(), etc.
								consume_conversion_operator_target_modifiers(target_type);
								
								// Check for ()
								if (peek() == "("_tok) {
									is_conversion = true;
									
									StringBuilder op_name_builder;
									op_name_builder.append("operator ");
									op_name_builder.append(target_type.getReadableString());
									std::string_view operator_name = op_name_builder.commit();
									
									Token identifier_token = Token(Token::Type::Identifier, operator_name,
									                              operator_keyword_token.line(), operator_keyword_token.column(),
									                              operator_keyword_token.file_index());
									
									ASTNode decl_node = emplace_node<DeclarationNode>(
										type_result.node().value(),
										identifier_token
									);
									
									discard_saved_token(op_saved);
									discard_saved_token(conv_saved);
									member_result = ParseResult::success(decl_node);
									found_conversion_op = true;
								}
							}
						}
						if (!is_conversion) {
							restore_token_position(op_saved);
						}
					}
					if (!found_conversion_op) {
						restore_token_position(conv_saved);
						// Parse member declaration (use same logic as regular struct parsing)
						member_result = parse_type_and_name();
					}
				}
				if (member_result.is_error() || !member_result.node().has_value()) {
					// Error recovery for partial specialization body: skip to next ';' or '}'
					// This allows parsing to continue past unsupported member patterns
					FLASH_LOG(Templates, Warning, "Partial specialization body: skipping unparseable member declaration at ", peek_info().value());
					while (!peek().is_eof() && peek() != "}"_tok) {
						if (peek() == ";"_tok) {
							advance(); // consume ';'
							break;
						}
						if (peek() == "{"_tok) {
							skip_balanced_braces();
							if (peek() == ";"_tok) advance();
							break;
						}
						advance();
					}
					continue;
				}
				
				// Check if this is a member function (has '(') or data member
				if (peek() == "("_tok) {
					// This is a member function
					if (!member_result.node()->is<DeclarationNode>()) {
						return ParseResult::error("Expected declaration node for member function", peek_info());
					}
					
					DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();
					
					// Parse function declaration with parameters
					auto func_result = parse_function_declaration(decl_node);
					if (func_result.is_error()) {
						return func_result;
					}
					
					if (!func_result.node().has_value()) {
						return ParseResult::error("Failed to create function declaration node", peek_info());
					}
					
					FunctionDeclarationNode& func_decl = func_result.node()->as<FunctionDeclarationNode>();
					DeclarationNode& func_decl_node = func_decl.decl_node();
					
					// Create a new FunctionDeclarationNode with member function info
					auto [member_func_node, member_func_ref] =
						emplace_node_ref<FunctionDeclarationNode>(func_decl_node, StringTable::getStringView(instantiated_name));
					
					// Copy parameters from the parsed function
					for (const auto& param : func_decl.parameter_nodes()) {
						member_func_ref.add_parameter_node(param);
					}
					
					// Apply leading specifiers to the member function
					member_func_ref.set_is_constexpr(conv_specs & FlashCpp::MLS_Constexpr);
					member_func_ref.set_is_consteval(conv_specs & FlashCpp::MLS_Consteval);
					member_func_ref.set_inline_always(conv_specs & FlashCpp::MLS_Inline);
					
					// Parse trailing specifiers (const, volatile, noexcept, override, final, = default, = delete)
					FlashCpp::MemberQualifiers member_quals;
					FlashCpp::FunctionSpecifiers func_specs;
					auto specs_result = parse_function_trailing_specifiers(member_quals, func_specs);
					if (specs_result.is_error()) {
						return specs_result;
					}
					
					// Extract parsed specifiers
					bool is_defaulted = func_specs.is_defaulted();
					bool is_deleted = func_specs.is_deleted();
					
					// Handle defaulted functions: create implicit function with empty body
					if (is_defaulted) {
						// Expect ';'
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after '= default'", peek_info());
						}
						
						// Mark as implicit
						member_func_ref.set_is_implicit(true);
						
						// Create empty block for the function body
						auto [block_node, block_ref] = create_node_ref(BlockNode());
						member_func_ref.set_definition(block_node);
						
						// Add member function to struct
						struct_ref.add_member_function(member_func_node, current_access);
						continue;
					}
					
					// Handle deleted functions: skip adding to struct
					if (is_deleted) {
						// Expect ';'
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after '= delete'", peek_info());
						}
						// Deleted functions are not added to the struct
						continue;
					}
					
					// Check for function body and use delayed parsing
					if (peek() == "{"_tok) {
						// Save position at start of body
						SaveHandle body_start = save_token_position();
						
						// Skip over the function body by counting braces
						skip_balanced_braces();
						
						// Record for delayed parsing
						delayed_function_bodies_.push_back({
							&member_func_ref,
							body_start,
							{},       // initializer_list_start (not used)
							instantiated_name,
							struct_type_info.type_index_,
							&struct_ref,
							false,    // has_initializer_list
							false,  // is_constructor
							false,  // is_destructor
							nullptr,  // ctor_node
							nullptr,  // dtor_node
							{}  // no template parameter names for specializations
						});
					} else {
						// Just a declaration, consume the semicolon
						consume(";"_tok);
					}
					
					// Add member function to struct
					struct_ref.add_member_function(member_func_node, current_access);
				} else {
					// Data member - need to handle default initializers (e.g., `T* ptr = nullptr;`)
					ASTNode member_node = *member_result.node();
					if (member_node.is<DeclarationNode>()) {
						const DeclarationNode& decl_node = member_node.as<DeclarationNode>();
						const TypeSpecifierNode& type_spec = decl_node.type_node().as<TypeSpecifierNode>();
						std::optional<size_t> bitfield_width;
						std::optional<ASTNode> bitfield_width_expr;

						// Handle bitfield declarations: int x : 5;
						if (auto width_result = parse_bitfield_width(bitfield_width, &bitfield_width_expr); width_result.is_error()) {
							return width_result;
						}

						// Check for default initializer
						std::optional<ASTNode> default_initializer;
						if (peek() == "="_tok) {
							advance(); // consume '='
							// Parse the initializer expression
							auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (init_result.is_error()) {
								return init_result;
							}
							if (init_result.node().has_value()) {
								default_initializer = *init_result.node();
							}
						} else if (peek() == "{"_tok) {
							// Brace-init default member initializer: _Tp _M_tp{};
							auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (init_result.is_error()) {
								return init_result;
							}
							if (init_result.node().has_value()) {
								default_initializer = *init_result.node();
							}
						}
						struct_ref.add_member(member_node, current_access, default_initializer, bitfield_width, bitfield_width_expr);

						// Handle comma-separated declarations (e.g., int x, y, z;)
						while (peek() == ","_tok) {
							advance(); // consume ','

							// Parse the next member name
							auto next_member_name = advance();
							if (next_member_name.type() != Token::Type::Identifier) {
								return ParseResult::error("Expected member name after comma", peek_info());
							}

							std::optional<size_t> additional_bitfield_width;
							std::optional<ASTNode> additional_bitfield_width_expr;
							// Handle bitfield declarations: int x, y : 3;
							if (auto width_result = parse_bitfield_width(additional_bitfield_width, &additional_bitfield_width_expr); width_result.is_error()) {
								return width_result;
							}

							// Check for optional initialization
							std::optional<ASTNode> additional_init;
							if (peek() == "="_tok) {
								advance(); // consume '='
								auto init_result = parse_expression(2, ExpressionContext::Normal);
								if (init_result.is_error()) {
									return init_result;
								}
								if (init_result.node().has_value()) {
									additional_init = *init_result.node();
								}
							}

							// Create declaration with same type
							ASTNode next_member_decl = emplace_node<DeclarationNode>(
								emplace_node<TypeSpecifierNode>(type_spec),
								next_member_name
							);
							struct_ref.add_member(next_member_decl, current_access, additional_init, additional_bitfield_width, additional_bitfield_width_expr);
						}
					}
					// Consume semicolon after data member
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after member declaration", peek_info());
					}
				}
			}
			
			// Expect closing brace
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' after class body", peek_info());
			}
			
			// Pop member function context
			member_function_context_stack_.pop_back();
			
			// Pop struct parsing context
			if (!struct_parsing_context_stack_.empty()) {
				struct_parsing_context_stack_.pop_back();
			}
			
			// Skip any attributes after struct/class definition (e.g., __attribute__((__deprecated__)))
			skip_cpp_attributes();
			
			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after class declaration", peek_info());
			}
			
			// Add members to struct info (struct_info was created earlier before parsing base classes)
			for (const auto& member_decl : struct_ref.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
				
				// Calculate member size and alignment
				auto [member_size, member_alignment] = calculateMemberSizeAndAlignment(type_spec);
				
				ReferenceQualifier ref_qual = type_spec.reference_qualifier();
				// Phase 7B: Intern member name and use StringHandle overload
				StringHandle member_name_handle = decl.identifier_token().handle();
				struct_info->addMember(
					member_name_handle,
					type_spec.type(),
					type_spec.type_index(),
					member_size,
					member_alignment,
					member_decl.access,
					member_decl.default_initializer,
					ref_qual,
					ref_qual != ReferenceQualifier::None ? get_type_size_bits(type_spec.type()) : 0,
					false,
					{},
					static_cast<int>(type_spec.pointer_depth()),
					member_decl.bitfield_width
				);
			}
			
			// Add member functions to struct info
			for (const auto& member_func_decl : struct_ref.member_functions()) {
				if (member_func_decl.is_constructor) {
					// Add constructor to struct type info
					struct_info->addConstructor(
						member_func_decl.function_declaration,
						member_func_decl.access
					);
				} else if (member_func_decl.is_destructor) {
					// Add destructor to struct type info
					struct_info->addDestructor(
						member_func_decl.function_declaration,
						member_func_decl.access,
						member_func_decl.is_virtual
					);
				} else {
					// Handle both regular functions and member function templates
					if (member_func_decl.function_declaration.is<TemplateFunctionDeclarationNode>()) {
						// Member function template - get the inner function declaration
						const TemplateFunctionDeclarationNode& template_decl = member_func_decl.function_declaration.as<TemplateFunctionDeclarationNode>();
						const FunctionDeclarationNode& func_decl = template_decl.function_declaration().as<FunctionDeclarationNode>();
						const DeclarationNode& decl = func_decl.decl_node();
						
						// Phase 7B: Intern function name and use StringHandle overload
						StringHandle func_name_handle = decl.identifier_token().handle();
						struct_info->addMemberFunction(
							func_name_handle,
							member_func_decl.function_declaration,
							member_func_decl.access,
							member_func_decl.is_virtual,
							member_func_decl.is_pure_virtual,
							member_func_decl.is_override,
							member_func_decl.is_final
						);
					} else {
						// Regular member function
						const FunctionDeclarationNode& func_decl = member_func_decl.function_declaration.as<FunctionDeclarationNode>();
						const DeclarationNode& decl = func_decl.decl_node();
						
						// Phase 7B: Intern function name and use StringHandle overload
						StringHandle func_name_handle = decl.identifier_token().handle();
						struct_info->addMemberFunction(
							func_name_handle,
							member_func_decl.function_declaration,
							member_func_decl.access,
							member_func_decl.is_virtual,
							member_func_decl.is_pure_virtual,
							member_func_decl.is_override,
							member_func_decl.is_final
						);
					}
				}
			}
			
			// Finalize the struct layout with base classes
			bool finalize_success;
			struct_info->has_deferred_base_classes = !struct_ref.deferred_template_base_classes().empty();
			if (!struct_ref.base_classes().empty()) {
				finalize_success = struct_info->finalizeWithBases();
			} else {
				finalize_success = struct_info->finalize();
			}
			
			// Check for semantic errors during finalization
			if (!finalize_success) {
				return ParseResult::error(struct_info->getFinalizationError(), Token());
			}
			
			// Store struct info
			struct_type_info.setStructInfo(std::move(struct_info));
if (struct_type_info.getStructInfo()) {
	struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
}
			
			// Parse delayed function bodies for partial specialization member functions
			SaveHandle position_after_struct = save_token_position();
			for (auto& delayed : delayed_function_bodies_) {
				// Restore token position to the start of the function body
				restore_token_position(delayed.body_start);
				
				// Set up function context
				gSymbolTable.enter_scope(ScopeType::Function);
				member_function_context_stack_.push_back({
					delayed.struct_name,
					delayed.struct_type_index,
					delayed.struct_node,
					nullptr  // local_struct_info - not needed for delayed function bodies
				});
				
				// Register member functions in symbol table so member-to-member calls resolve correctly
				register_member_functions_in_scope(delayed.struct_node, delayed.struct_type_index);
				
				// Add 'this' pointer to symbol table
				auto [this_type_node, this_type_ref] = emplace_node_ref<TypeSpecifierNode>(
					Type::Struct, delayed.struct_type_index,
					0, Token()
				);
				this_type_ref.add_pointer_level();
				
				Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
				auto [this_decl_node, this_decl_ref] = emplace_node_ref<DeclarationNode>(this_type_node, this_token);
				gSymbolTable.insert("this"sv, this_decl_node);
				
				// Add function parameters to scope
				if (delayed.func_node) {
					for (const auto& param : delayed.func_node->parameter_nodes()) {
						if (param.is<DeclarationNode>()) {
							const auto& param_decl = param.as<DeclarationNode>();
							gSymbolTable.insert(param_decl.identifier_token().value(), param);
						}
					}
				} else if (delayed.ctor_node) {
					for (const auto& param : delayed.ctor_node->parameter_nodes()) {
						if (param.is<DeclarationNode>()) {
							const auto& param_decl = param.as<DeclarationNode>();
							gSymbolTable.insert(param_decl.identifier_token().value(), param);
						}
					}
				}
				
				// Parse the function body
				auto block_result = parse_block();
				if (block_result.is_error()) {
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					return block_result;
				}
				
				if (auto block = block_result.node()) {
					if (delayed.func_node) {
						delayed.func_node->set_definition(*block);
					} else if (delayed.ctor_node) {
						delayed.ctor_node->set_definition(*block);
					}
				}
				
				member_function_context_stack_.pop_back();
				gSymbolTable.exit_scope();
			}
			
			// Clear delayed function bodies
			delayed_function_bodies_.clear();
			
			// Restore position after struct
			restore_token_position(position_after_struct);
			
			// Register the specialization PATTERN (not exact match)
			// This allows pattern matching during instantiation
			gTemplateRegistry.registerSpecializationPattern(template_name, template_params, pattern_args, struct_node);
			
			// Clean up template parameter context before returning
			current_template_param_names_.clear();
			
			return saved_position.success(struct_node);
		}

		// Set flag to indicate we're parsing a template class
		// This will prevent delayed function bodies from being parsed immediately
		parsing_template_class_ = true;
		parsing_template_body_ = true;
		template_param_names_.clear();
		for (const auto& param : template_params) {
			if (param.is<TemplateParameterNode>()) {
				const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
				template_param_names_.push_back(tparam.name());
			}
		}

		// Set template parameter context for current_template_param_names_
		std::vector<StringHandle> template_param_names_for_body;
		for (const auto& param : template_params) {
			if (param.is<TemplateParameterNode>()) {
				const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
				template_param_names_for_body.push_back(tparam.nameHandle());
			}
		}
		current_template_param_names_ = std::move(template_param_names_for_body);

		// Parse class template
		// Save scope/stack state before try block so we can restore on exception
		const size_t saved_struct_stack_size = struct_parsing_context_stack_.size();
		const size_t saved_member_func_stack_size = member_function_context_stack_.size();
		const size_t saved_scope_depth = gSymbolTable.get_current_scope_handle().scope_level;
		try {
			decl_result = parse_struct_declaration();
		} catch (const std::bad_any_cast& e) {
			// Restore parser state that may have been partially modified
			while (struct_parsing_context_stack_.size() > saved_struct_stack_size)
				struct_parsing_context_stack_.pop_back();
			while (member_function_context_stack_.size() > saved_member_func_stack_size)
				member_function_context_stack_.pop_back();
			while (gSymbolTable.get_current_scope_handle().scope_level > saved_scope_depth)
				gSymbolTable.exit_scope();

			FLASH_LOG(Templates, Error, "bad_any_cast during template struct parsing: ", e.what());
			// Skip to end of struct body
			while (!peek().is_eof() && peek() != ";"_tok) {
				if (peek() == "{"_tok) {
					skip_balanced_braces();
				} else {
					advance();
				}
			}
			if (peek() == ";"_tok) advance();
			decl_result = ParseResult::success();
		}

		// Clear template parameter context
		current_template_param_names_.clear();

		// Reset flag
		parsing_template_class_ = false;
		parsing_template_body_ = false;
		template_param_names_.clear();
		current_template_param_names_.clear();
	} else {
		// Could be:
		// 1. Deduction guide: template<typename T> ClassName(T) -> ClassName<T>;
		// 2. Function template: template<typename T> T max(T a, T b) { ... }
		// 3. Out-of-line member function: template<typename T> void Vector<T>::push_back(T v) { ... }

		// Check for deduction guide by looking for ClassName(...) -> pattern
		// Save position to peek ahead
		auto deduction_guide_check_pos = save_token_position();
		bool is_deduction_guide = false;
		std::string_view guide_class_name;
		
		// Try to peek: if we see Identifier ( ... ) ->, it's likely a deduction guide
		if (peek().is_identifier()) {
			guide_class_name = peek_info().value();
			advance();
			if (peek() == "("_tok) {
				advance(); // consume '('
				// Skip parameter list
				int paren_depth = 1; // Start at 1 since we already consumed '('
				while (!peek().is_eof() && paren_depth > 0) {
					if (peek() == "("_tok) paren_depth++;
					else if (peek() == ")"_tok) paren_depth--;
					advance();
				}
				// Check for ->
				if (peek() == "->"_tok) {
					is_deduction_guide = true;
				}
			}
		}
		restore_token_position(deduction_guide_check_pos);
		
		if (is_deduction_guide) {
			// Parse: ClassName(params) -> ClassName<args>;
			// class name
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected class name in deduction guide", current_token_);
			}
			std::string_view class_name = peek_info().value();
			advance();
			
			// Parse parameter list
			if (peek() != "("_tok) {
				return ParseResult::error("Expected '(' in deduction guide", current_token_);
			}
			advance(); // consume '('
			
			std::vector<ASTNode> guide_params;
			if (peek() != ")"_tok) {
				// Parse parameters
				while (true) {
					auto param_type_result = parse_type_specifier();
					if (param_type_result.is_error()) {
						return param_type_result;
					}
					guide_params.push_back(*param_type_result.node());

					// Allow pointer/reference declarators directly in guide parameters (e.g., T*, const T&, etc.)
					if (!guide_params.empty() && guide_params.back().is<TypeSpecifierNode>()) {
						TypeSpecifierNode& param_type = guide_params.back().as<TypeSpecifierNode>();

						// Handle array reference pattern: _Type(&)[_ArrayExtent] or _Type(&&)[_ArrayExtent]
						// Also handle function pointer pattern: _Type(*)(Args...)
						if (peek() == "("_tok) {
							SaveHandle paren_pos = save_token_position();
							advance(); // consume '('
							
							auto pre_ref_qualifiers = param_type.reference_qualifier();
							auto pre_pointer_depth = param_type.pointer_depth();
							bool is_func_ptr = (peek() == "*"_tok);
							consume_pointer_ref_modifiers(param_type);
							
							// Optional identifier inside parens
							if (param_type.is_reference() && peek().is_identifier()) {
								advance(); // skip name
							}
							
							if ((param_type.is_reference() || is_func_ptr) && peek() == ")"_tok) {
								advance(); // consume ')'
								if (param_type.is_reference() && peek() == "["_tok) {
									advance(); // consume '['
									// Skip array extent expression
									while (!peek().is_eof() && peek() != "]"_tok) {
										advance();
									}
									if (peek() == "]"_tok) {
										advance(); // consume ']'
									}
									param_type.set_array(true);
									discard_saved_token(paren_pos);
								} else if (is_func_ptr && peek() == "("_tok) {
									// Function pointer parameter list: (*)(Args...)
									advance(); // consume '('
									while (!peek().is_eof() && peek() != ")"_tok) {
										auto fp_param_result = parse_type_specifier();
										if (fp_param_result.is_error()) break;
										while (peek() == "*"_tok || peek() == "&"_tok || peek() == "&&"_tok ||
											   peek() == "const"_tok || peek() == "volatile"_tok) {
											advance();
										}
										if (peek() == "..."_tok) advance();
										if (peek() == ","_tok) { advance(); } else { break; }
									}
									if (peek() == ")"_tok) {
										advance(); // consume ')'
										// Handle noexcept on function pointer
										if (peek() == "noexcept"_tok) {
											advance();
											if (peek() == "("_tok) skip_balanced_parens();
										}
										discard_saved_token(paren_pos);
									} else {
										param_type.limit_pointer_depth(pre_pointer_depth);
										param_type.set_reference_qualifier(pre_ref_qualifiers);
										restore_token_position(paren_pos);
									}
								} else {
									param_type.limit_pointer_depth(pre_pointer_depth); // restore
									param_type.set_reference_qualifier(pre_ref_qualifiers); // restore
									restore_token_position(paren_pos);
								}
							} else {
								param_type.limit_pointer_depth(pre_pointer_depth); // restore
								param_type.set_reference_qualifier(pre_ref_qualifiers); // restore
								restore_token_position(paren_pos);
							}
						}

						// Parse pointer levels with optional CV-qualifiers
						consume_pointer_ref_modifiers(param_type);
					}
					
					// Handle pack expansion '...' (e.g., _Up...)
					if (peek() == "..."_tok) {
						advance(); // consume '...'
					}

					// Optional parameter name (ignored)
					if (peek().is_identifier()) {
						advance();
					}

					// Also handle '...' after parameter name
					if (peek() == "..."_tok) {
						advance(); // consume '...'
					}

					// Handle default argument (e.g., _Allocator = _Allocator())
					if (peek() == "="_tok) {
						advance(); // consume '='
						// Skip the default argument expression (balanced parens/angles)
						int paren_depth = 0;
						int angle_depth = 0;
						while (!peek().is_eof()) {
							if (peek() == "("_tok) { advance(); paren_depth++; }
							else if (peek() == ")"_tok && paren_depth > 0) { advance(); paren_depth--; }
							else if (peek() == "<"_tok) { advance(); angle_depth++; }
							else if (peek() == ">"_tok && angle_depth > 0) { advance(); angle_depth--; }
							else if (peek() == ">>"_tok && angle_depth >= 2) { advance(); angle_depth -= 2; }
							else if (peek() == ">>"_tok && angle_depth == 1) { split_right_shift_token(); advance(); angle_depth--; }
							else if (paren_depth == 0 && angle_depth == 0 &&
									 (peek() == ","_tok || peek() == ")"_tok)) {
								break;
							}
							else { advance(); }
						}
					}

					if (peek() == ","_tok) {
						advance();
						continue;
					}
					break;
				}
			}
			
			if (peek() != ")"_tok) {
				return ParseResult::error("Expected ')' in deduction guide", current_token_);
			}
			advance(); // consume ')'
			
			// Expect ->
			if (peek() != "->"_tok) {
				return ParseResult::error("Expected '->' in deduction guide", current_token_);
			}
			advance(); // consume '->'
			
			// Parse deduced type: ClassName<args>
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected class name after '->' in deduction guide", current_token_);
			}
			advance(); // consume class name (should match)
			
			// Parse template arguments
			std::vector<ASTNode> deduced_type_nodes;
			auto deduced_args_opt = parse_explicit_template_arguments(&deduced_type_nodes);
			if (!deduced_args_opt.has_value()) {
				return ParseResult::error("Expected template arguments in deduction guide", current_token_);
			}
			if (deduced_type_nodes.size() != deduced_args_opt->size()) {
				return ParseResult::error("Unsupported deduction guide arguments", current_token_);
			}
			
			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after deduction guide", current_token_);
			}
			
			// Create DeductionGuideNode
			auto guide_node = emplace_node<DeductionGuideNode>(
				std::move(template_params),
				class_name,
				std::move(guide_params),
				std::move(deduced_type_nodes)
			);
			
			// Register the deduction guide
			gTemplateRegistry.register_deduction_guide(class_name, guide_node);
			
			return saved_position.success();
		}

		// Try to detect out-of-line member function definition
		// Pattern: ReturnType ClassName<TemplateArgs>::FunctionName(...)
		auto out_of_line_result = try_parse_out_of_line_template_member(template_params, template_param_names);
		if (out_of_line_result.has_value()) {
			return saved_position.success();  // Successfully parsed out-of-line definition
		}

		// Check if this is a function template specialization (template<>)
		// For specializations, we need to parse and instantiate immediately as a concrete function
		if (is_specialization) {
			// Parse the function with explicit template arguments in the name
			// Pattern: template<> ReturnType FunctionName<Args>(params) { body }
			
			// Parse return type and function name
			auto type_and_name_result = parse_type_and_name();
			if (type_and_name_result.is_error()) {
				return type_and_name_result;
			}
			
			if (!type_and_name_result.node().has_value() || !type_and_name_result.node()->is<DeclarationNode>()) {
				return ParseResult::error("Expected function name in template specialization", current_token_);
			}
			
			DeclarationNode& decl_node = type_and_name_result.node()->as<DeclarationNode>();
			std::string_view func_base_name = decl_node.identifier_token().value();
			
			// Parse explicit template arguments (e.g., <int>, <int, int>)
			std::vector<TemplateTypeArg> spec_template_args;
			if (peek() == "<"_tok) {
				auto template_args_opt = parse_explicit_template_arguments();
				if (!template_args_opt.has_value()) {
					return ParseResult::error("Failed to parse template arguments in function specialization", current_token_);
				}
				spec_template_args = *template_args_opt;
			}
			
			// Parse function parameters
			auto func_result = parse_function_declaration(decl_node);
			if (func_result.is_error()) {
				return func_result;
			}
			
			if (!func_result.node().has_value() || !func_result.node()->is<FunctionDeclarationNode>()) {
				return ParseResult::error("Failed to parse function in template specialization", current_token_);
			}
			
			FunctionDeclarationNode& func_node = func_result.node()->as<FunctionDeclarationNode>();
			
			// Store non-type template arguments on the function node for use in codegen
			// This enables generating correct mangled names for template specializations like get<0>
			std::vector<int64_t> non_type_args;
			for (const auto& arg : spec_template_args) {
				if (arg.is_value) {
					non_type_args.push_back(arg.value);
				}
			}
			if (!non_type_args.empty()) {
				func_node.set_non_type_template_args(std::move(non_type_args));
			}

			// Consume trailing specifiers (const, volatile, noexcept, throw(), __attribute__, etc.)
			// CV and ref qualifiers are captured in spec_quals for signature matching
			FlashCpp::MemberQualifiers spec_quals;
			skip_function_trailing_specifiers(spec_quals);

			// Parse the function body, or accept forward declaration (;)
			// C++ allows full specialization declarations without a body:
			//   template<> void foo<int>(int);
			if (peek() == ";"_tok) {
				advance(); // consume ';'
				// Forward declaration of a full specialization.
				// Register it with the template registry so the signature is known when used later.
				NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
				StringHandle func_handle = StringTable::getOrInternStringHandle(func_base_name);
				StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, func_handle);
				std::string_view qualified_specialization_name = StringTable::getStringView(qualified_handle);
				gTemplateRegistry.registerSpecialization(qualified_specialization_name, spec_template_args, *func_result.node());

				return saved_position.success(*func_result.node());
			}
			if (peek() != "{"_tok) {
				std::string error_msg = "Template specializations must have a definition (body)";
				if (!peek().is_eof()) {
					error_msg += ", found '" + std::string(peek_info().value()) + "'";
				}
				return ParseResult::error(error_msg, current_token_);
			}
			
			// Enter function scope for parsing the body
			gSymbolTable.enter_scope(ScopeType::Function);
			
			// Add parameters to symbol table
			for (const auto& param : func_node.parameter_nodes()) {
				if (param.is<DeclarationNode>()) {
					const DeclarationNode& param_decl = param.as<DeclarationNode>();
					gSymbolTable.insert(param_decl.identifier_token().value(), param);
				}
			}
			
			// Parse the function body
			auto body_result = parse_block();
			gSymbolTable.exit_scope();
			
			if (body_result.is_error()) {
				return body_result;
			}
			
			// Set the body on the function
			if (body_result.node().has_value()) {
				func_node.set_definition(*body_result.node());
			}
			
			// Register the specialization with the template registry
			// This makes it available when the template is instantiated with these args
			// Build the qualified name including current namespace path
			NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
			StringHandle func_handle = StringTable::getOrInternStringHandle(func_base_name);
			StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, func_handle);
			std::string_view qualified_specialization_name = StringTable::getStringView(qualified_handle);
			
			ASTNode func_node_copy = *func_result.node();
			
			// Compute and set the proper mangled name for the specialization
			// Extract namespace path as string_view vector
			std::string_view qualified_namespace = gNamespaceRegistry.getQualifiedName(current_handle);
			std::vector<std::string_view> ns_path = splitQualifiedNamespace(qualified_namespace);
			
			// Generate proper C++ ABI mangled name
			FunctionDeclarationNode& func_for_mangling = func_node_copy.as<FunctionDeclarationNode>();
			NameMangling::MangledName specialization_mangled_name;
			
			// Check if this specialization has non-type template arguments (like get<0>, get<1>)
			if (func_for_mangling.has_non_type_template_args()) {
				// Use the version that includes non-type template arguments in the mangled name
				const std::vector<int64_t>& spec_non_type_args = func_for_mangling.non_type_template_args();
				const DeclarationNode& decl = func_for_mangling.decl_node();
				const TypeSpecifierNode& return_type = decl.type_node().as<TypeSpecifierNode>();
				
				// Build parameter type list
				std::vector<TypeSpecifierNode> param_types;
				for (const auto& param_node : func_for_mangling.parameter_nodes()) {
					if (param_node.is<DeclarationNode>()) {
						const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
						param_types.push_back(param_decl.type_node().as<TypeSpecifierNode>());
					}
				}
				
				specialization_mangled_name = NameMangling::generateMangledNameWithTemplateArgs(
					func_base_name, return_type, param_types, spec_non_type_args, 
					func_for_mangling.is_variadic(), "", ns_path);
			} else if (!spec_template_args.empty()) {
				// Use the version that includes TYPE template arguments in the mangled name
				// This handles specializations like sum<int>, sum<int, int>
				const DeclarationNode& decl = func_for_mangling.decl_node();
				const TypeSpecifierNode& return_type = decl.type_node().as<TypeSpecifierNode>();
				
				// Build parameter type list
				std::vector<TypeSpecifierNode> param_types;
				for (const auto& param_node : func_for_mangling.parameter_nodes()) {
					if (param_node.is<DeclarationNode>()) {
						const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
						param_types.push_back(param_decl.type_node().as<TypeSpecifierNode>());
					}
				}
				
				specialization_mangled_name = NameMangling::generateMangledNameWithTypeTemplateArgs(
					func_base_name, return_type, param_types, spec_template_args, 
					func_for_mangling.is_variadic(), "", ns_path);
			} else {
				// Regular specialization without any template args (shouldn't happen but fallback)
				specialization_mangled_name = 
					NameMangling::generateMangledNameFromNode(func_for_mangling, ns_path);
			}
			
			func_for_mangling.set_mangled_name(specialization_mangled_name.view());
			
			gTemplateRegistry.registerSpecialization(qualified_specialization_name, spec_template_args, func_node_copy);
			
			// Also add to symbol table so codegen can find it during overload resolution
			// Use the base function name (without template args) so it can be looked up
			gSymbolTable.insert(func_base_name, func_node_copy);
			
			// Also add to AST so it gets code-generated
			return saved_position.success(func_node_copy);
		}

		// Otherwise, parse as function template using shared helper (Phase 6)
		// Note: current_template_param_names_ was already set earlier (line ~22659) after template parameter
		// parsing, so template parameters are recognized when parsing the return type.
		
		ASTNode template_func_node;
		auto body_result = parse_template_function_declaration_body(template_params, requires_clause, template_func_node);
		
		// Clean up template parameter context
		current_template_param_names_.clear();
		parsing_template_body_ = false;
		has_parameter_packs_ = saved_has_packs;
		
		if (body_result.is_error()) {
			return body_result;
		}

		// Get the function name for registration
		const TemplateFunctionDeclarationNode& template_decl = template_func_node.as<TemplateFunctionDeclarationNode>();
		const FunctionDeclarationNode& func_decl = template_decl.function_declaration().as<FunctionDeclarationNode>();
		const DeclarationNode& func_decl_node = func_decl.decl_node();

		// Register the template in the template registry
		// If we're in a namespace, register with both simple and qualified names
		std::string_view simple_name = func_decl_node.identifier_token().value();
		
		// Add debug logging for __call_is_nt to track hang location
		if (simple_name == "__call_is_nt") {
			FLASH_LOG(Templates, Info, "[DEBUG_HANG] Registering __call_is_nt template");
			FLASH_LOG(Templates, Info, "[DEBUG_HANG] Function has ", func_decl.parameter_nodes().size(), " parameters");
		}
		
		// Register with QualifiedIdentifier — handles both simple and namespace-qualified keys
		gTemplateRegistry.registerTemplate(
			QualifiedIdentifier::fromQualifiedName(simple_name, gSymbolTable.get_current_namespace_handle()),
			template_func_node);

		// Add the template function to the symbol table so it can be found during overload resolution
		gSymbolTable.insert(simple_name, template_func_node);

		return saved_position.success(template_func_node);
	}

	if (decl_result.is_error()) {
		return decl_result;
	}

	if (!decl_result.node().has_value()) {
		return ParseResult::error("Expected function or class declaration after template parameter list", current_token_);
	}

	ASTNode decl_node = *decl_result.node();

	// Create appropriate template node based on what was parsed
	// Note: Function templates are now handled above via parse_template_function_declaration_body() (Phase 6)
	if (decl_node.is<StructDeclarationNode>()) {
		// Create a TemplateClassDeclarationNode with parameter names for lookup
		std::vector<std::string_view> param_names;
		for (const auto& param : template_params) {
			if (param.is<TemplateParameterNode>()) {
				param_names.push_back(param.as<TemplateParameterNode>().name());
			}
		}
		
		auto template_class_node = emplace_node<TemplateClassDeclarationNode>(
			std::move(template_params),
			std::move(param_names),
			decl_node
		);
		
		// Attach deferred member function bodies for two-phase lookup
		// These will be parsed during template instantiation when TypeInfo is available
		if (!pending_template_deferred_bodies_.empty()) {
			auto& template_class = template_class_node.as<TemplateClassDeclarationNode>();
			template_class.set_deferred_bodies(std::move(pending_template_deferred_bodies_));
			pending_template_deferred_bodies_.clear();  // Clear for next template
		}

		// Register the template in the template registry
		// If we're in a namespace, register with both simple and qualified names
		const StructDeclarationNode& struct_decl = decl_node.as<StructDeclarationNode>();
		std::string_view simple_name = StringTable::getStringView(struct_decl.name());
		
		// Register with QualifiedIdentifier — handles both simple and namespace-qualified keys
		// Note: simple_name may already be qualified (e.g., "std::numeric_limits") if
		// parse_struct_declaration prepended the namespace. fromQualifiedName() handles both cases.
		FLASH_LOG_FORMAT(Templates, Debug, "Registering template class: '{}'", simple_name);
		gTemplateRegistry.registerTemplate(
			QualifiedIdentifier::fromQualifiedName(simple_name, gSymbolTable.get_current_namespace_handle()),
			template_class_node);

		// Primary templates shouldn't be added to AST - only instantiations and specializations
		// Return success with no node so the caller doesn't add it to ast_nodes_
		return saved_position.success();
	} else {
		return ParseResult::error("Unsupported template declaration type", current_token_);
	}
}

// Parse a C++20 concept declaration
// Syntax: concept Name = constraint_expression;
// Where constraint_expression can be a requires expression, a type trait, or a conjunction/disjunction
ParseResult Parser::parse_member_struct_template(StructDeclarationNode& struct_node, [[maybe_unused]] AccessSpecifier access) {
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
	std::vector<std::string_view> template_param_names;

	auto param_list_result = parse_template_parameter_list(template_params);
	if (param_list_result.is_error()) {
		return param_list_result;
	}

	// Extract parameter names for later lookup
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			template_param_names.push_back(param.as<TemplateParameterNode>().name());
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

	// Skip requires clause if present (for partial specializations with constraints)
	// e.g., template<typename T> requires Constraint<T> struct Name<T> { ... };
	std::optional<ASTNode> requires_clause;
	if (peek() == "requires"_tok) {
		Token requires_token = peek_info();
		advance(); // consume 'requires'
		
		// Parse the constraint expression
		auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (constraint_result.is_error()) {
			return constraint_result;
		}
		
		// Create RequiresClauseNode (but we just skip it for member struct templates)
		requires_clause = emplace_node<RequiresClauseNode>(
			*constraint_result.node(),
			requires_token
		);
	}

	// Expect 'struct' or 'class' or 'union' keyword
	if (!peek().is_keyword() ||
	    (peek() != "struct"_tok && peek() != "class"_tok && peek() != "union"_tok)) {
		return ParseResult::error("Expected 'struct' or 'class' or 'union' after template parameter list", current_token_);
	}
	
	bool is_class = (peek() == "class"_tok);
	bool is_union = (peek() == "union"_tok);
	[[maybe_unused]] Token struct_keyword_token = peek_info();
	advance(); // consume 'struct' or 'class' or 'union'

	// Skip C++11 attributes between struct/class and name (e.g., [[__deprecated__]])
	skip_cpp_attributes();

	// Parse the struct name
	if (!peek().is_identifier()) {
		return ParseResult::error("Expected struct/class name after 'struct'/'class' keyword", current_token_);
	}
	Token struct_name_token = peek_info();
	std::string_view struct_name = struct_name_token.value();
	advance(); // consume struct name

	// Check if this is a forward declaration (template<...> struct Name;)
	if (peek() == ";"_tok) {
		advance(); // consume ';'
		// For forward declarations, we just register the template without a body
		// Create a minimal struct node
		auto qualified_name = StringTable::getOrInternStringHandle(
			StringBuilder().append(struct_node.name()).append("::"sv).append(struct_name));
		
		auto forward_struct_node = emplace_node<StructDeclarationNode>(
			qualified_name,
			is_class,
			is_union
		);
		forward_struct_node.as<StructDeclarationNode>().set_is_forward_declaration(true);
		
		// Create template struct node for the forward declaration
		auto template_struct_node = emplace_node<TemplateClassDeclarationNode>(
			std::move(template_params),
			std::move(template_param_names),
			forward_struct_node
		);
		
		// Register the template
		gTemplateRegistry.registerTemplate(qualified_name, template_struct_node);
		gTemplateRegistry.registerTemplate(struct_name_token.handle(), template_struct_node);
		
		FLASH_LOG_FORMAT(Parser, Info, "Registered member struct template forward declaration: {}", 
			StringTable::getStringView(qualified_name));
		
		return saved_position.success();
	}

	// Check if this is a partial specialization by looking for '<' after the struct name
	// e.g., template<typename T, typename... Rest> struct List<T, Rest...> : List<Rest...> { };
	bool is_partial_specialization = false;
	if (peek() == "<"_tok) {
		is_partial_specialization = true;
	}

	// Handle partial specialization of member struct template
	if (is_partial_specialization) {
		// Save current template param names and set up the new ones for pattern parsing
		// This allows template parameter references like _Sz in the pattern <_Sz, _List<_Uint, _UInts...>, true>
		auto saved_template_param_names = std::move(current_template_param_names_);
		current_template_param_names_.clear();
		for (const auto& name : template_param_names) {
			current_template_param_names_.emplace_back(StringTable::getOrInternStringHandle(name));
		}
		
		// Parse the specialization pattern: <T, Rest...>, etc.
		auto pattern_args_opt = parse_explicit_template_arguments();
		
		// Restore the original template param names
		current_template_param_names_ = std::move(saved_template_param_names);
		
		if (!pattern_args_opt.has_value()) {
			return ParseResult::error("Expected template argument pattern in partial specialization", current_token_);
		}
		
		std::vector<TemplateTypeArg> pattern_args = *pattern_args_opt;
		
		// Generate a unique name for the pattern template
		// We use the template parameter names + modifiers to create unique pattern names
		// E.g., List<T*> -> ParentClass::List_pattern_TP
		StringBuilder pattern_name;
		pattern_name.append(struct_name).append("_pattern"sv);
		for (const auto& arg : pattern_args) {
			// Add modifiers to make pattern unique
			pattern_name.append("_"sv);
			
			// Handle non-type value parameters (e.g., true, false, 42)
			if (arg.is_value) {
				pattern_name.append("V"sv).append(arg.value);
				continue;
			}
			
			// Add pointer markers
			for (size_t i = 0; i < arg.pointer_depth; ++i) {
				pattern_name.append("P"sv);
			}
			// Add array marker
			if (arg.is_array) {
				pattern_name.append("A"sv);
				if (arg.array_size.has_value()) {
					pattern_name.append("["sv).append(static_cast<int64_t>(*arg.array_size)).append("]"sv);
				}
			}
			if (arg.member_pointer_kind == MemberPointerKind::Object) {
				pattern_name.append("MPO"sv);
			} else if (arg.member_pointer_kind == MemberPointerKind::Function) {
				pattern_name.append("MPF"sv);
			}
			// Add reference markers
			if (arg.is_rvalue_reference()) {
				pattern_name.append("RR"sv);
			} else if (arg.is_reference()) {
				pattern_name.append("R"sv);
			}
			// Add const/volatile markers
			if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0) {
				pattern_name.append("C"sv);
			}
			if ((static_cast<uint8_t>(arg.cv_qualifier) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0) {
				pattern_name.append("V"sv);
			}
		}
		
		// When there's a requires clause, add a unique counter suffix to disambiguate
		// multiple partial specializations with the same pattern but different constraints.
		// e.g., __cat<_Iter> with requires A<_Iter> vs __cat<_Iter> with requires B<_Iter>
		if (requires_clause.has_value()) {
			static std::atomic<size_t> constrained_pattern_counter{0};
			pattern_name.append("_C"sv).append(static_cast<int64_t>(constrained_pattern_counter.fetch_add(1)));
		}
		
		// Qualify with parent struct name
		std::string_view pattern_name_str = pattern_name.commit();
		auto qualified_pattern_name = StringTable::getOrInternStringHandle(
			StringBuilder().append(struct_node.name()).append("::"sv).append(pattern_name_str));
		
		// Register this as a pattern struct name for O(1) lookup
		gTemplateRegistry.registerPatternStructName(qualified_pattern_name);
		
		// Create a struct node for this partial specialization
		auto [member_struct_node, member_struct_ref] = emplace_node_ref<StructDeclarationNode>(
			qualified_pattern_name,
			is_class,
			is_union
		);
		
		// Parse base class list if present (e.g., : List<Rest...>)
		if (peek() == ":"_tok) {
			advance();  // consume ':'
			
			// For now, we'll skip base class parsing for member struct templates
			// to keep the implementation simple. We just consume tokens until '{'
			// TODO: Implement full base class parsing for member struct template partial specializations
			while (peek() != "{"_tok) {
				advance();
			}
		}
		
		// Expect '{' to start struct body
		if (peek() != "{"_tok) {
			return ParseResult::error("Expected '{' to start struct body", current_token_);
		}
		advance(); // consume '{'
		
		// Parse struct body with simple member parsing
		AccessSpecifier current_access = is_class ? AccessSpecifier::Private : AccessSpecifier::Public;
		
		// Set template context flags so static_assert deferral works correctly
		// Use ScopeGuard to ensure flags are restored on all exit paths (including error returns)
		auto saved_tpn_partial = std::move(current_template_param_names_);
		current_template_param_names_.clear();
		for (const auto& name : template_param_names) {
			current_template_param_names_.emplace_back(StringTable::getOrInternStringHandle(name));
		}
		bool saved_ptb_partial = parsing_template_body_;
		parsing_template_body_ = true;
		ScopeGuard restore_template_context_partial([&]() {
			current_template_param_names_ = std::move(saved_tpn_partial);
			parsing_template_body_ = saved_ptb_partial;
		});
		
		while (!peek().is_eof() && peek() != "}"_tok) {
			// Skip empty declarations (bare ';' tokens) - valid in C++
			if (peek() == ";"_tok) {
				advance();
				continue;
			}
			
			// Check for access specifiers
			if (peek().is_keyword()) {
				std::string_view keyword = peek_info().value();
				if (keyword == "public" || keyword == "private" || keyword == "protected") {
					advance(); // consume access specifier
					if (!consume(":"_tok)) {
						return ParseResult::error("Expected ':' after access specifier", current_token_);
					}
					if (keyword == "public") current_access = AccessSpecifier::Public;
					else if (keyword == "private") current_access = AccessSpecifier::Private;
					else if (keyword == "protected") current_access = AccessSpecifier::Protected;
					continue;
				}
				// Handle static_assert inside member struct template body
				if (keyword == "static_assert") {
					auto static_assert_result = parse_static_assert();
					if (static_assert_result.is_error()) {
						return static_assert_result;
					}
					continue;
				}
				// Handle nested struct/class declarations inside partial specialization body
				// e.g., struct __type { ... };
				if (keyword == "struct" || keyword == "class") {
					// Skip the entire nested struct declaration including its body
					advance(); // consume 'struct' or 'class'
					
					// Skip struct name if present
					if (peek().is_identifier()) {
						advance(); // consume struct name
					}
					
					// Skip to body or semicolon
					if (peek() == "{"_tok) {
						skip_balanced_braces();
					}
					
					// Consume trailing semicolon
					if (peek() == ";"_tok) {
						advance();
					}
					continue;
				}
				// Handle member type alias (using) declarations
				if (keyword == "using") {
					auto alias_result = parse_member_type_alias("using", &member_struct_ref, current_access);
					if (alias_result.is_error()) {
						return alias_result;
					}
					continue;
				}
				// Handle static members (including static constexpr with initializers)
				if (keyword == "static") {
					advance(); // consume 'static'
					
					// Check if it's const or constexpr
					CVQualifier cv_qual = CVQualifier::None;
					[[maybe_unused]] bool is_constexpr = false;
					while (peek().is_keyword()) {
						auto kw = peek();
						if (kw == "const"_tok) {
							cv_qual |= CVQualifier::Const;
							advance();
						} else if (kw == "constexpr"_tok) {
							is_constexpr = true;
							cv_qual |= CVQualifier::Const; // constexpr implies const
							advance();
						} else if (kw == "inline"_tok) {
							advance();
						} else {
							break;
						}
					}
					
					// Parse type and name
					auto type_and_name_result = parse_type_and_name();
					if (type_and_name_result.is_error()) {
						return type_and_name_result;
					}
					
					// Check for initialization (e.g., = sizeof(T))
					std::optional<ASTNode> init_expr_opt;
					if (peek() == "="_tok) {
						advance(); // consume '='
						
						// Parse the initializer expression
						auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (init_result.is_error()) {
							return init_result;
						}
						if (init_result.node().has_value()) {
							init_expr_opt = *init_result.node();
						}
					}
					
					// Check if this is a static member function (has '(')
					// Static member functions in member template structs should be skipped for now
					// (they will be instantiated when the template is used)
					if (peek() == "("_tok) {
						skip_member_declaration_to_semicolon();
						continue;
					}
					
					// Expect semicolon (for static data member)
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after static member declaration", current_token_);
					}
					
					// Store the static member in the struct (as a pattern for instantiation)
					if (type_and_name_result.node().has_value()) {
						const DeclarationNode& decl = type_and_name_result.node()->as<DeclarationNode>();
						const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
						
						// Calculate size and alignment for the static member (handles pointers/references correctly)
						auto [static_member_size, static_member_alignment] = calculateMemberSizeAndAlignment(type_spec);
						ReferenceQualifier ref_qual = type_spec.reference_qualifier();
						int ptr_depth = static_cast<int>(type_spec.pointer_depth());
						
						// Add to struct's static members
						StringHandle static_member_name_handle = decl.identifier_token().handle();
						member_struct_ref.add_static_member(
							static_member_name_handle,
							type_spec.type(),
							type_spec.type_index(),
							static_member_size,
							static_member_alignment,
							current_access,
							init_expr_opt,
							cv_qual,
							ref_qual,
							ptr_depth
						);
					}
					continue;
				}
				// Handle nested template declarations (member function templates, member struct templates, etc.)
				if (keyword == "template") {
					auto template_result = parse_member_template_or_function(member_struct_ref, current_access);
					if (template_result.is_error()) {
						return template_result;
					}
					continue;
				}
			}
			// This ensures specifiers like constexpr, inline, static aren't lost for non-constructor members
			SaveHandle member_saved_pos = save_token_position();
			
			// Handle specifiers before checking for constructor
			// Use parse_declaration_specifiers for common keywords, then check explicit separately
			[[maybe_unused]] auto member_specs = parse_declaration_specifiers();
			
			// Handle 'explicit' keyword separately (constructor-specific, not in parse_declaration_specifiers)
			// C++20 explicit(condition) - also skip the condition expression
			[[maybe_unused]] bool is_member_explicit = false;
			if (peek() == "explicit"_tok) {
				is_member_explicit = true;
				advance();
				if (peek() == "("_tok) {
					skip_balanced_parens();
				}
			}
			
			// Check for constructor (identifier matching struct name followed by '(')
			// For member struct templates, struct_name is the simple name (e.g., "_Int")
			if (!peek().is_eof() && peek().is_identifier() &&
			    peek_info().value() == struct_name) {
				// Save position after specifiers for constructor lookahead
				SaveHandle ctor_lookahead_pos = save_token_position();
				// Look ahead to see if this is a constructor (next token is '(')
				advance(); // consume struct name
				
				if (peek() == "("_tok) {
					// This is a constructor - skip it for now
					// Member struct template constructors will be instantiated when the template is used
					discard_saved_token(ctor_lookahead_pos);
					discard_saved_token(member_saved_pos);
					FLASH_LOG_FORMAT(Parser, Debug, "parse_member_struct_template: Skipping constructor for {}", struct_name);
					skip_member_declaration_to_semicolon();
					continue;
				} else {
					// Not a constructor, restore position to BEFORE specifiers so they get re-parsed
					discard_saved_token(ctor_lookahead_pos);
					restore_token_position(member_saved_pos);
				}
			} else {
				// Not starting with struct name - restore position to BEFORE specifiers
				// so parse_type_and_name() can properly handle the specifiers
				restore_token_position(member_saved_pos);
			}
			
			// Parse member declaration (data member or function)
			auto member_result = parse_type_and_name();
			if (member_result.is_error()) {
				return member_result;
			}
			
			if (!member_result.node().has_value()) {
				return ParseResult::error("Expected member declaration", peek_info());
			}
			
			// Check if this is a member function (has '(') or data member (has ';', ':', or '=')
			if (peek() == ":"_tok) {
				// Bitfield data member
				std::optional<size_t> bitfield_width;
				std::optional<ASTNode> bitfield_width_expr;
				if (auto width_result = parse_bitfield_width(bitfield_width, &bitfield_width_expr); width_result.is_error()) {
					return width_result;
				}

				std::optional<ASTNode> init;
				if (peek() == "="_tok) {
					advance(); // consume '='
					auto init_result = parse_expression(2, ExpressionContext::Normal);
					if (init_result.is_error()) {
						return init_result;
					}
					init = init_result.node();
				}

				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after bitfield member", current_token_);
				}
				member_struct_ref.add_member(*member_result.node(), current_access, init, bitfield_width, bitfield_width_expr);
			} else if (peek() == ";"_tok) {
				// Simple data member
				advance(); // consume ';'
				member_struct_ref.add_member(*member_result.node(), current_access, std::nullopt);
			} else if (peek() == "="_tok) {
				// Data member with initializer
				advance(); // consume '='
				// Parse initializer expression
				auto init_result = parse_expression(2, ExpressionContext::Normal);
				if (init_result.is_error()) {
					return init_result;
				}
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after member initializer", current_token_);
				}
				member_struct_ref.add_member(*member_result.node(), current_access, init_result.node());
			} else {
				// Skip other complex cases for now (member functions, etc.)
				// Just consume tokens until we hit ';' or '}'
				int brace_depth = 0;
				while (!peek().is_eof()) {
					if (peek() == "{"_tok) {
						brace_depth++;
						advance();
					} else if (peek() == "}"_tok) {
						if (brace_depth == 0) {
							break;  // End of struct body
						}
						brace_depth--;
						advance();
					} else if (peek() == ";"_tok && brace_depth == 0) {
						advance();
						break;
					} else {
						advance();
					}
				}
			}
		}
		
		// ScopeGuard restore_template_context_partial handles restoration automatically
		
		// Expect '}' to close struct body
		if (peek() != "}"_tok) {
			return ParseResult::error("Expected '}' to close struct body", current_token_);
		}
		advance(); // consume '}'
		
		// Skip any attributes after struct/class definition (e.g., __attribute__((__deprecated__)))
		skip_cpp_attributes();
		
		// Expect ';' to end struct declaration
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after struct declaration", current_token_);
		}
		
		// Register the partial specialization pattern FIRST (before moving template_params)
		// For member struct templates, we need to store the pattern with the parent struct name
		auto qualified_simple_name = StringTable::getOrInternStringHandle(
			StringBuilder().append(struct_node.name()).append("::"sv).append(struct_name));
		
		// Create template struct node for the partial specialization
		auto template_struct_node = emplace_node<TemplateClassDeclarationNode>(
			template_params,  // Copy, don't move yet
			template_param_names,  // Copy, don't move yet
			member_struct_node
		);
		
		// Register pattern under qualified name (MakeUnsigned::List)
		gTemplateRegistry.registerSpecializationPattern(
			StringTable::getStringView(qualified_simple_name),
			template_params,
			pattern_args,
			template_struct_node
		);
		
		// Also register pattern under simple name (List) for consistency with primary template
		// This ensures patterns are found regardless of whether qualified or simple name is used
		gTemplateRegistry.registerSpecializationPattern(
			struct_name,
			template_params,
			pattern_args,
			template_struct_node
		);
		
		FLASH_LOG_FORMAT(Parser, Info, "Registered member struct template partial specialization: {} with pattern", 
			StringTable::getStringView(qualified_pattern_name));
		
		return saved_position.success();
	}

	// Not a partial specialization - continue with primary template parsing
	// Create the struct declaration node first so we can add base classes to it
	// Member structs are prefixed with parent struct name for uniqueness
	auto qualified_name = StringTable::getOrInternStringHandle(
		StringBuilder().append(struct_node.name()).append("::"sv).append(struct_name));
	
	auto [member_struct_node, member_struct_ref] = emplace_node_ref<StructDeclarationNode>(
		qualified_name, 
		is_class,
		is_union
	);

	// Handle base class list if present (e.g., : true_type<T>)
	if (peek() == ":"_tok) {
		advance();  // consume ':'
		
		// Parse base class(es) - skip tokens until '{' for now
		// TODO: Implement full base class parsing for member struct templates
		while (peek() != "{"_tok) {
			advance();
		}
	}

	// Expect '{' to start struct body
	if (peek() != "{"_tok) {
		return ParseResult::error("Expected '{' to start struct body", current_token_);
	}
	advance(); // consume '{'

	// Parse struct body (members, methods, etc.)
	// For template member structs, parse members but don't instantiate dependent types yet
	// This matches C++ semantics where template members are parsed but not instantiated until needed
	AccessSpecifier current_access = is_class ? AccessSpecifier::Private : AccessSpecifier::Public;
	
	// Set template context flags so static_assert deferral works correctly
	// Use ScopeGuard to ensure flags are restored on all exit paths (including error returns)
	auto saved_template_param_names_body = std::move(current_template_param_names_);
	current_template_param_names_.clear();
	for (const auto& name : template_param_names) {
		current_template_param_names_.emplace_back(StringTable::getOrInternStringHandle(name));
	}
	bool saved_parsing_template_body = parsing_template_body_;
	parsing_template_body_ = true;
	ScopeGuard restore_template_context_body([&]() {
		current_template_param_names_ = std::move(saved_template_param_names_body);
		parsing_template_body_ = saved_parsing_template_body;
	});
	
	while (!peek().is_eof() && peek() != "}"_tok) {
		// Skip empty declarations (bare ';' tokens) - valid in C++
		if (peek() == ";"_tok) {
			advance();
			continue;
		}
		
		// Check for access specifiers
		if (peek().is_keyword()) {
			std::string_view keyword = peek_info().value();
			if (keyword == "public" || keyword == "private" || keyword == "protected") {
				advance(); // consume access specifier
				if (!consume(":"_tok)) {
					return ParseResult::error("Expected ':' after access specifier", current_token_);
				}
				if (keyword == "public") current_access = AccessSpecifier::Public;
				else if (keyword == "private") current_access = AccessSpecifier::Private;
				else if (keyword == "protected") current_access = AccessSpecifier::Protected;
				continue;
			}
			// Handle static_assert inside member struct template body
			if (keyword == "static_assert") {
				auto static_assert_result = parse_static_assert();
				if (static_assert_result.is_error()) {
					return static_assert_result;
				}
				continue;
			}
			// Handle member function templates - skip them for now
			// They will be properly instantiated when the member template struct is used
			if (keyword == "template") {
				advance(); // consume 'template'
				skip_member_declaration_to_semicolon();
				continue;
			}
			// Handle static members (including static constexpr with initializers)
			if (keyword == "static") {
				advance(); // consume 'static'
				
				// Check if it's const or constexpr
				while (peek().is_keyword()) {
					auto kw = peek();
					if (kw == "const"_tok || kw == "constexpr"_tok || kw == "inline"_tok) {
						advance();
					} else {
						break;
					}
				}
				
				// Parse type and name
				auto type_and_name_result = parse_type_and_name();
				if (type_and_name_result.is_error()) {
					return type_and_name_result;
				}
				
				// Check if this is a static member function (has '(')
				// Static member functions in member template structs should be skipped for now
				if (peek() == "("_tok) {
					skip_member_declaration_to_semicolon();
					continue;
				}
				
				// Check for initialization (e.g., = sizeof(T))
				if (peek() == "="_tok) {
					advance(); // consume '='
					
					// Parse the initializer expression
					auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (init_result.is_error()) {
						return init_result;
					}
					// We parse but don't store the initializer for member templates
				}
				
				// Expect semicolon (for static data member)
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after static member declaration", current_token_);
				}
				
				// For member templates, we just skip static members
				// Full instantiation will handle them properly
				continue;
			}
			// Handle 'using' type aliases: using type = T;
			if (keyword == "using") {
				auto alias_result = parse_member_type_alias("using", &member_struct_ref, current_access);
				if (alias_result.is_error()) {
					return alias_result;
				}
				continue;
			}
			// Handle 'typedef' type aliases: typedef T type;
			if (keyword == "typedef") {
				auto alias_result = parse_member_type_alias("typedef", &member_struct_ref, current_access);
				if (alias_result.is_error()) {
					return alias_result;
				}
				continue;
			}
		}

		// Save position BEFORE parsing specifiers so we can restore if needed
		// This ensures specifiers like constexpr, inline, static aren't lost for non-constructor members
		SaveHandle member_saved_pos2 = save_token_position();
		
		// Handle specifiers before checking for constructor
		// Use parse_declaration_specifiers for common keywords, then check explicit separately
		[[maybe_unused]] auto member_specs2 = parse_declaration_specifiers();
		
		// Handle 'explicit' keyword separately (constructor-specific, not in parse_declaration_specifiers)
		// C++20 explicit(condition) - also skip the condition expression
		[[maybe_unused]] bool is_member_explicit2 = false;
		if (peek() == "explicit"_tok) {
			is_member_explicit2 = true;
			advance();
			if (peek() == "("_tok) {
				skip_balanced_parens();
			}
		}
		
		// Check for constructor (identifier matching struct name followed by '(')
		// For member struct templates, struct_name is the simple name (e.g., "_Int")
		if (!peek().is_eof() && peek().is_identifier() &&
		    peek_info().value() == struct_name) {
			// Save position after specifiers for constructor lookahead
			SaveHandle ctor_lookahead_pos2 = save_token_position();
			// Look ahead to see if this is a constructor (next token is '(')
			advance(); // consume struct name
			
			if (peek() == "("_tok) {
				// This is a constructor - skip it for now
				// Member struct template constructors will be instantiated when the template is used
				discard_saved_token(ctor_lookahead_pos2);
				discard_saved_token(member_saved_pos2);
				FLASH_LOG_FORMAT(Parser, Debug, "parse_member_struct_template (primary): Skipping constructor for {}", struct_name);
				skip_member_declaration_to_semicolon();
				continue;
			} else {
				// Not a constructor, restore position to BEFORE specifiers so they get re-parsed
				discard_saved_token(ctor_lookahead_pos2);
				restore_token_position(member_saved_pos2);
			}
		} else {
			// Not starting with struct name - restore position to BEFORE specifiers
			// so parse_type_and_name() can properly handle the specifiers
			restore_token_position(member_saved_pos2);
		}

		// Parse member declaration (data member or function)
		auto member_result = parse_type_and_name();
		if (member_result.is_error()) {
			return member_result;
		}
		
		if (!member_result.node().has_value()) {
			return ParseResult::error("Expected member declaration", peek_info());
		}
		
		// Check if this is a member function (has '(') or data member (has ';')
		if (peek() == "("_tok) {
			// Member function
			DeclarationNode& decl_node = member_result.node()->as<DeclarationNode>();
			
			// Parse function declaration with parameters
			auto func_result = parse_function_declaration(decl_node);
			if (func_result.is_error()) {
				return func_result;
			}
			
			if (!func_result.node().has_value()) {
				return ParseResult::error("Failed to create function declaration node", peek_info());
			}
			
			FunctionDeclarationNode& func_decl = func_result.node()->as<FunctionDeclarationNode>();
			
			// Create member function node
			auto [member_func_node, member_func_ref] =
				emplace_node_ref<FunctionDeclarationNode>(decl_node, qualified_name);
			
			// Copy parameters
			for (const auto& param : func_decl.parameter_nodes()) {
				member_func_ref.add_parameter_node(param);
			}
			
			// Parse trailing specifiers
			FlashCpp::MemberQualifiers member_quals;
			FlashCpp::FunctionSpecifiers func_specs;
			auto specs_result = parse_function_trailing_specifiers(member_quals, func_specs);
			if (specs_result.is_error()) {
				return specs_result;
			}
			
			// Handle function body or semicolon
			// For member struct templates, we skip the body and save the position for later
			// re-parsing during template instantiation (similar to member function templates)
			if (peek() == "{"_tok) {
				// Save position for re-parsing during instantiation
				SaveHandle body_start = save_token_position();
				member_func_ref.set_template_body_position(body_start);
				
				// Skip over the body (skip_balanced_braces consumes the '{' and everything up to the matching '}')
				skip_balanced_braces();
			} else if (peek() == ";"_tok) {
				advance(); // consume ';'
			}
			
			// Add member function to struct
			member_struct_ref.add_member_function(member_func_node, current_access);
		} else if (peek() == ":"_tok) {
			// Bitfield data member
			std::optional<size_t> bitfield_width;
			std::optional<ASTNode> bitfield_width_expr;
			if (auto width_result = parse_bitfield_width(bitfield_width, &bitfield_width_expr); width_result.is_error()) {
				return width_result;
			}
			std::optional<ASTNode> init;
			if (peek() == "="_tok) {
				advance(); // consume '='
				auto init_result = parse_expression(2, ExpressionContext::Normal);
				if (init_result.is_error()) {
					return init_result;
				}
				init = init_result.node();
			}
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after bitfield member", peek_info());
			}
			member_struct_ref.add_member(*member_result.node(), current_access, init, bitfield_width, bitfield_width_expr);

		} else if (peek() == ";"_tok) {
			// Data member
			advance(); // consume ';'
			member_struct_ref.add_member(*member_result.node(), current_access, std::nullopt);
		} else if (peek() == "="_tok) {
			// Data member with initializer
			advance(); // consume '='
			auto init_result = parse_expression(2, ExpressionContext::Normal);
			if (init_result.is_error()) {
				return init_result;
			}
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after member initializer", peek_info());
			}
			member_struct_ref.add_member(*member_result.node(), current_access, init_result.node());
		} else {
			return ParseResult::error("Expected '(' or ';' after member declaration", peek_info());
		}
	}
	
	// ScopeGuard restore_template_context_body handles restoration automatically

	// Expect '}' to close struct body
	if (peek() != "}"_tok) {
		return ParseResult::error("Expected '}' to close struct body", current_token_);
	}
	advance(); // consume '}'

	// Skip any attributes after struct/class definition (e.g., __attribute__((__deprecated__)))
	skip_cpp_attributes();

	// Expect ';' to end struct declaration
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after struct declaration", current_token_);
	}

	// Create template struct node (using TemplateClassDeclarationNode which handles both struct and class)
	auto template_struct_node = emplace_node<TemplateClassDeclarationNode>(
		std::move(template_params),
		std::move(template_param_names),
		member_struct_node
	);

	// Register the template in the global registry with qualified name
	gTemplateRegistry.registerTemplate(qualified_name, template_struct_node);
	
	// Also register with simple name for lookups within the parent struct
	gTemplateRegistry.registerTemplate(struct_name_token.handle(), template_struct_node);

	FLASH_LOG_FORMAT(Parser, Info, "Registered member struct template: {}", StringTable::getStringView(qualified_name));

	// template_scope automatically cleans up template parameters when it goes out of scope

	return saved_position.success();
}

// Parse member variable template: template<...> static constexpr Type var = ...;
// This handles variable templates declared inside struct/class bodies.
