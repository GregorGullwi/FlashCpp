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
					bool is_const = false;
					[[maybe_unused]] bool is_constexpr = false;
					while (peek().is_keyword()) {
						auto kw = peek();
						if (kw == "const"_tok) {
							is_const = true;
							advance();
						} else if (kw == "constexpr"_tok) {
							is_constexpr = true;
							is_const = true; // constexpr implies const
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
						
						// Calculate size and alignment for the static member
						size_t static_member_size = get_type_size_bits(type_spec.type()) / 8;
						size_t static_member_alignment = get_type_alignment(type_spec.type(), static_member_size);
						
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
							is_const
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
					
					if (peek() == ">"_tok || peek() == ","_tok || peek() == "..."_tok) {
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
