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
	[[maybe_unused]] bool saved_has_packs = has_parameter_packs_;
	has_parameter_packs_ = has_packs;
	
	// Set template parameter context EARLY, before any code that might call parse_type_specifier()
	// This includes variable template detection below which needs to recognize template params
	// like _Int in return types: typename tuple_element<_Int, pair<_Tp1, _Tp2>>::type&
	current_template_param_names_ = template_param_names;
	parsing_template_body_ = true;

	// Check if this is a nested template (member function template of a class template)
	// Pattern: template<typename T> template<typename U> ReturnType Class<T>::method(U u) { ... }
	// At this point, outer template params are registered, so the inner parse can see them.
	// TODO: Full nested template instantiation is not yet supported. Currently we skip the
	// inner template definition entirely, which means member function templates of class
	// templates will parse but won't generate code. Calls to such functions will fail at
	// link time. This allows standard headers like <algorithm> to parse without errors.
	if (peek() == "template"_tok) {
		// Save the inner template params, then skip forward to find the function body and consume it
		auto inner_saved = save_token_position();
		advance(); // consume inner 'template'
		if (peek() == "<"_tok) {
			skip_template_arguments(); // skip inner template params
			// Now skip everything until we find the function body '{' or a ';'
			// We need to handle nested braces, parens, etc.
			while (!peek().is_eof()) {
				if (peek() == "{"_tok) {
					skip_balanced_braces();
					discard_saved_token(inner_saved);
					return saved_position.success();
				} else if (peek() == ";"_tok) {
					advance();
					discard_saved_token(inner_saved);
					return saved_position.success();
				} else if (peek() == "("_tok) {
					skip_balanced_parens();
				} else {
					advance();
				}
			}
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
		// The integral_constant<bool, B> gets instantiated as "integral_constant_bool_unknown"
		bool has_unresolved_params = false;
		StringHandle target_template_name;
		std::vector<ASTNode> target_template_arg_nodes;

		if ((type_spec.type() == Type::Struct || type_spec.type() == Type::UserDefined) &&
		    type_spec.type_index() < gTypeInfo.size()) {
			const TypeInfo& ti = gTypeInfo[type_spec.type_index()];
			std::string_view type_name = StringTable::getStringView(ti.name());

			// Check for "_unknown" suffix indicating unresolved template parameters
			if (type_name.find("_unknown") != std::string_view::npos) {
				has_unresolved_params = true;
				FLASH_LOG(Parser, Debug, "Alias target type '", type_name, "' has unresolved parameters - using deferred instantiation");
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
				
				// Parse the template name
				if (peek().is_identifier()) {
					target_template_name = peek_info().handle();
					advance();
					
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
				
				// Note: We already consumed the tokens, so type_spec still points to the _unknown type
				// We don't need to re-parse again - just use the existing type_spec
			}
		}
		
		// Discard the saved position since we've consumed the type
		discard_saved_token(target_type_start_pos);
		
		// Handle pointer depth (*, **, etc.)
		while (peek() == "*"_tok) {
			advance(); // consume '*'
			
			// Parse CV-qualifiers after the * (const, volatile)
			CVQualifier ptr_cv = parse_cv_qualifiers();
			
			type_spec.add_pointer_level(ptr_cv);
		}
		
		// Handle reference modifiers (&, &&)
		// The lexer may produce either:
		// - A single '&&' token for rvalue reference
		// - Two separate '&' tokens for rvalue reference  
		// - A single '&' token for lvalue reference
		if (peek() == "&&"_tok) {
			advance(); // consume '&&'
			type_spec.set_reference(true);  // true = rvalue reference
		} else if (peek() == "&"_tok) {
			advance(); // consume first '&'
			
			// Check for rvalue reference (&&) as two tokens
			if (peek() == "&"_tok) {
				advance(); // consume second '&'
				type_spec.set_reference(true);  // true = rvalue reference
			} else {
				type_spec.set_lvalue_reference(true);  // lvalue reference
			}
		}
		
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
		// Register with simple name for unqualified lookups
		gTemplateRegistry.register_alias_template(std::string(alias_name), alias_node);
		
		// If in a namespace, also register with qualified name for namespace-qualified lookups
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		if (!current_handle.isGlobal()) {
			StringHandle name_handle = StringTable::getOrInternStringHandle(alias_name);
			StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, name_handle);
			std::string_view qualified_name = StringTable::getStringView(qualified_handle);
			FLASH_LOG_FORMAT(Templates, Debug, "Registering alias template with qualified name: {}", qualified_name);
			gTemplateRegistry.register_alias_template(std::string(qualified_name), alias_node);
		}
		
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
					size_t ptr_depth = 0;
					while (peek() == "*"_tok) {
						advance(); // consume '*'
						ptr_depth++;
						CVQualifier ptr_cv = parse_cv_qualifiers();
						type_spec.add_pointer_level(ptr_cv);
					}
					
					// Parse reference qualifier
					ReferenceQualifier ref = parse_reference_qualifier();
					if (ref == ReferenceQualifier::LValueReference) {
						type_spec.set_reference(false);
					} else if (ref == ReferenceQualifier::RValueReference) {
						type_spec.set_reference(true);
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
					arg.pointer_depth = ptr_depth + type_spec.pointer_levels().size();
					arg.is_reference = type_spec.is_lvalue_reference();
					arg.is_rvalue_reference = type_spec.is_rvalue_reference();
					arg.is_array = is_array;
					// Mark as dependent - this is a partial specialization pattern,
					// so all types are template parameters (dependent types)
					arg.is_dependent = true;
					
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
			// For partial specializations, register with a name that includes the pattern
			// This allows lookup during instantiation
			// Build a unique name for the pattern
			StringBuilder pattern_name;
			pattern_name.append(var_name);
			for (const auto& arg : specialization_pattern) {
				pattern_name.append("_");
				
				// Include base type name for user-defined types to distinguish patterns
				// e.g., __is_ratio_v<ratio<N,D>> gets pattern "__is_ratio_v_ratio"
				// For simple dependent types (like T&), DON'T include the type name
				// e.g., is_reference_v<T&> should get pattern "is_reference_v_R", not "is_reference_v_TR"
				// BUT for dependent TEMPLATE INSTANTIATIONS (like ratio<_Num, _Den>), DO include the base template name
				bool included_type_name = false;
				
				// First, check if we have a valid type_index pointing to a named type
				if ((arg.base_type == Type::UserDefined || arg.base_type == Type::Struct || arg.base_type == Type::Enum)) {
					if (arg.type_index < gTypeInfo.size() && arg.type_index > 0) {
						// Get the simple name (without namespace) for pattern matching
						std::string_view type_name = StringTable::getStringView(gTypeInfo[arg.type_index].name());
						// Strip namespace prefix if present
						size_t last_colon = type_name.rfind("::");
						if (last_colon != std::string_view::npos) {
							type_name = type_name.substr(last_colon + 2);
						}
						
						// Only include type name for TEMPLATE INSTANTIATION patterns, not simple template parameters
						// Use TypeInfo-based detection (includes fallback for backward compatibility)
						auto [is_template_instantiation_placeholder, base_name] = isDependentTemplatePlaceholder(type_name);
						
						if (is_template_instantiation_placeholder) {
							// This is a dependent template instantiation like ratio<_Num, _Den>
							pattern_name.append(base_name);
							included_type_name = true;
						} else if (!arg.is_dependent) {
							// Non-dependent concrete type - include the full type name
							pattern_name.append(type_name);
							included_type_name = true;
						}
						// For simple dependent types (like T), don't include the name - they match any type
					}
				}
				
				// If type_index didn't give us a name and we have dependent_name, check if it's a template
				// This handles cases where the template instantiation was skipped due to dependent args
				if (!included_type_name && arg.dependent_name.isValid()) {
					std::string_view dep_name = StringTable::getStringView(arg.dependent_name);
					// Check if this name is a known template (not just a simple type like T)
					// Template names are identifiers that have registered templates
					auto template_opt = gTemplateRegistry.lookupTemplate(dep_name);
					if (template_opt.has_value()) {
						// This is a template instantiation pattern - include the template name
						pattern_name.append(dep_name);
						included_type_name = true;
					}
					// else: It's a simple type parameter like T - don't include in pattern
				}
				
				if (arg.is_reference) {
					pattern_name.append("R");  // lvalue reference
				} else if (arg.is_rvalue_reference) {
					pattern_name.append("RR"); // rvalue reference
				}
				for (size_t i = 0; i < arg.pointer_depth; i++) {
					pattern_name.append("P");  // pointer
				}
			}
			std::string_view pattern_key = pattern_name.commit();
			gTemplateRegistry.registerVariableTemplate(pattern_key, template_var_node);
			FLASH_LOG(Parser, Debug, "Registered variable template partial specialization: ", pattern_key);
			
			// If in a namespace, also register with qualified pattern name
			NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
			if (!current_handle.isGlobal()) {
				StringHandle pattern_handle = StringTable::getOrInternStringHandle(pattern_key);
				StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, pattern_handle);
				std::string_view qualified_pattern_key = StringTable::getStringView(qualified_handle);
				gTemplateRegistry.registerVariableTemplate(qualified_pattern_key, template_var_node);
				FLASH_LOG(Parser, Debug, "Registered variable template partial specialization with qualified name: ", qualified_pattern_key);
			}
		} else {
			gTemplateRegistry.registerVariableTemplate(var_name, template_var_node);
			
			// If in a namespace, also register with qualified name for namespace-qualified lookups
			NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
			if (!current_handle.isGlobal()) {
				StringHandle var_handle = StringTable::getOrInternStringHandle(var_name);
				StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, var_handle);
				std::string_view qualified_name = StringTable::getStringView(qualified_handle);
				FLASH_LOG_FORMAT(Templates, Debug, "Registering variable template with qualified name: {}", qualified_name);
				gTemplateRegistry.registerVariableTemplate(qualified_name, template_var_node);
			}
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
					StringTable::getOrInternStringHandle(template_name),
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
				StringTable::getOrInternStringHandle(template_name),
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

			while (peek() != "}"_tok) {
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
						// Enums inside structs don't need to be added to the struct explicitly
						// They're registered in the global type system by parse_enum_declaration
						// The semicolon is already consumed by parse_enum_declaration
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
						
						// Skip struct name if present
						if (peek().is_identifier()) {
							advance(); // consume struct name
						}
						
						// Skip template arguments if present (e.g., struct Wrapper<int>)
						if (peek() == "<"_tok) {
							parse_explicit_template_arguments();
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
					ctor_is_constexpr = specs.is_constexpr;
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
					
					bool is_defaulted = dtor_func_specs.is_defaulted;
					bool is_deleted = dtor_func_specs.is_deleted;
					
					// Handle defaulted destructors
					if (is_defaulted) {
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after '= default'", peek_info());
						}
						
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
					DeclarationNode& func_decl_node = const_cast<DeclarationNode&>(func_decl.decl_node());

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
						func_specs.is_pure_virtual,
						func_specs.is_override,
						func_specs.is_final
					);
					
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

					struct_ref.add_member(*member_result.node(), current_access, default_initializer);

					// Handle comma-separated declarations (e.g., int x, y, z;)
					while (peek() == ","_tok) {
						advance(); // consume ','

						// Parse the next member name
						auto next_member_name = advance();
						if (next_member_name.type() != Token::Type::Identifier) {
							return ParseResult::error("Expected member name after comma", peek_info());
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
						struct_ref.add_member(next_member_decl, current_access, additional_init);
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

				bool is_ref_member = type_spec.is_reference();
				bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
				if (is_ref_member) {
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
					is_ref_member,
					is_rvalue_ref_member,
					referenced_size_bits
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
				if (arg.is_rvalue_reference) {
					pattern_name_builder.append("RR");
				} else if (arg.is_reference) {
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
				StringTable::getOrInternStringHandle(template_name), {});
			
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
					if (arg.is_rvalue_reference) {
						pattern_key.append("RR");
					} else if (arg.is_reference) {
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
			struct_parsing_context_stack_.push_back({StringTable::getStringView(instantiated_name), &struct_ref, struct_info.get(), {}});
			
			// Parse class body (same as full specialization)
			while (peek() != "}"_tok) {
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
						// Enums inside structs don't need to be added to the struct explicitly
						// They're registered in the global type system by parse_enum_declaration
						// The semicolon is already consumed by parse_enum_declaration
						continue;
					} else if (peek() == "struct"_tok || peek() == "class"_tok) {
						// Handle nested struct/class declarations inside partial specialization body
						// e.g., struct __type { ... };
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
					
					bool is_defaulted = dtor_func_specs.is_defaulted;
					bool is_deleted = dtor_func_specs.is_deleted;
					
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
				
				// Parse member declaration using delayed parsing for function bodies
				// (Same approach as full specialization to ensure member_function_context is available)
				auto member_result = parse_type_and_name();
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
					DeclarationNode& func_decl_node = const_cast<DeclarationNode&>(func_decl.decl_node());
					
					// Create a new FunctionDeclarationNode with member function info
					auto [member_func_node, member_func_ref] =
						emplace_node_ref<FunctionDeclarationNode>(func_decl_node, StringTable::getStringView(instantiated_name));
					
					// Copy parameters from the parsed function
					for (const auto& param : func_decl.parameter_nodes()) {
						member_func_ref.add_parameter_node(param);
					}
					
					// Parse trailing specifiers (const, volatile, noexcept, override, final, = default, = delete)
					FlashCpp::MemberQualifiers member_quals;
					FlashCpp::FunctionSpecifiers func_specs;
					auto specs_result = parse_function_trailing_specifiers(member_quals, func_specs);
					if (specs_result.is_error()) {
						return specs_result;
					}
					
					// Extract parsed specifiers
					bool is_defaulted = func_specs.is_defaulted;
					bool is_deleted = func_specs.is_deleted;
					
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
						}
						struct_ref.add_member(member_node, current_access, default_initializer);

						// Handle comma-separated declarations (e.g., int x, y, z;)
						while (peek() == ","_tok) {
							advance(); // consume ','

							// Parse the next member name
							auto next_member_name = advance();
							if (next_member_name.type() != Token::Type::Identifier) {
								return ParseResult::error("Expected member name after comma", peek_info());
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
							struct_ref.add_member(next_member_decl, current_access, additional_init);
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
				
				bool is_ref_member = type_spec.is_reference();
				bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
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
					is_ref_member,
					is_rvalue_ref_member,
					(is_ref_member || is_rvalue_ref_member) ? get_type_size_bits(type_spec.type()) : 0
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
		decl_result = parse_struct_declaration();

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

						// Parse pointer levels with optional CV-qualifiers
						while (peek() == "*"_tok) {
							advance(); // consume '*'

							CVQualifier ptr_cv = parse_cv_qualifiers();

							param_type.add_pointer_level(ptr_cv);
						}

						// Parse references (& or &&)
						ReferenceQualifier ref_qual = parse_reference_qualifier();
						if (ref_qual == ReferenceQualifier::RValueReference) {
							param_type.set_reference(true);
						} else if (ref_qual == ReferenceQualifier::LValueReference) {
							param_type.set_lvalue_reference(true);
						}
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
			
			// Parse the function body (specializations must be defined, not just declared)
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
		
		// Register with simple name (for backward compatibility and unqualified lookups)
		gTemplateRegistry.registerTemplate(simple_name, template_func_node);
		
		if (simple_name == "__call_is_nt") {
			FLASH_LOG(Templates, Info, "[DEBUG_HANG] Successfully registered __call_is_nt");
		}
		
		// If in a namespace, also register with qualified name for namespace-qualified lookups
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		if (!current_handle.isGlobal()) {
			StringHandle name_handle = StringTable::getOrInternStringHandle(simple_name);
			StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, name_handle);
			std::string_view qualified_name = StringTable::getStringView(qualified_handle);
			FLASH_LOG_FORMAT(Templates, Debug, "Registering template with qualified name: {}", qualified_name);
			gTemplateRegistry.registerTemplate(qualified_name, template_func_node);
		}

		// Add the template function to the symbol table so it can be found during overload resolution
		gSymbolTable.insert(simple_name, template_func_node);
		
		if (simple_name == "__call_is_nt") {
			FLASH_LOG(Templates, Info, "[DEBUG_HANG] Completed all registration for __call_is_nt");
		}

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
		
		// Register with simple name (for backward compatibility and unqualified lookups)
		FLASH_LOG_FORMAT(Templates, Debug, "Registering template class with simple name: '{}'", simple_name);
		gTemplateRegistry.registerTemplate(simple_name, template_class_node);
		
		// If in a namespace, also register with qualified name for namespace-qualified lookups
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		if (!current_handle.isGlobal()) {
			StringHandle name_handle = StringTable::getOrInternStringHandle(simple_name);
			StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, name_handle);
			std::string_view qualified_name = StringTable::getStringView(qualified_handle);
			FLASH_LOG_FORMAT(Templates, Debug, "Registering template with qualified name: {}", qualified_name);
			gTemplateRegistry.registerTemplate(qualified_name, template_class_node);
		}

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
ParseResult Parser::parse_concept_declaration() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'concept' keyword
	Token concept_token = peek_info();
	if (!consume("concept"_tok)) {
		return ParseResult::error("Expected 'concept' keyword", peek_info());
	}

	// Parse the concept name
	if (!peek().is_identifier()) {
		return ParseResult::error("Expected concept name after 'concept'", current_token_);
	}
	Token concept_name_token = peek_info();
	advance(); // consume concept name

	// For now, we'll support simple concepts without explicit template parameters
	// In full C++20, concepts can have template parameters: template<typename T> concept Name = ...
	// But the simplified syntax is: concept Name = ...;
	// We'll parse the simplified form for now

	// Expect '=' before the constraint expression
	if (peek() != "="_tok) {
		return ParseResult::error("Expected '=' after concept name", current_token_);
	}
	advance(); // consume '='

	// Parse the constraint expression
	// This is typically a requires expression, a type trait, or a boolean expression
	// For now, we'll accept any expression
	auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	if (constraint_result.is_error()) {
		return constraint_result;
	}

	// Expect ';' at the end
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after concept definition", current_token_);
	}

	// Create the ConceptDeclarationNode
	// For simplified concepts (without template<>), we use an empty template parameter list
	std::vector<TemplateParameterNode> template_params;
	
	auto concept_node = emplace_node<ConceptDeclarationNode>(
		concept_name_token,
		std::move(template_params),
		*constraint_result.node(),
		concept_token
	);

	// Register the concept in the global concept registry
	// This will be done in the semantic analysis phase
	// For now, we just return the node

	return saved_position.success(concept_node);
}

// Parse C++20 requires expression: requires(params) { requirements; } or requires { requirements; }
ParseResult Parser::parse_requires_expression() {
	ScopedTokenPosition saved_position(*this);

	// Consume 'requires' keyword
	Token requires_token = current_token_;
	if (!consume("requires"_tok)) {
		return ParseResult::error("Expected 'requires' keyword", current_token_);
	}

	// Enter a new scope for the requires expression parameters
	gSymbolTable.enter_scope(ScopeType::Block);
	
	// RAII guard to ensure scope is exited on all code paths (success or error)
	ScopeGuard scope_guard([&]() { gSymbolTable.exit_scope(); });
	
	// Check if there are parameters: requires(T a, T b) { ... }
	// or no parameters: requires { ... }
	std::vector<ASTNode> parameters;
	if (peek() == "("_tok) {
		advance(); // consume '('
		
		// Parse parameter list (similar to function parameters)
		// For now, we'll accept a simple parameter list
		// Full implementation would parse: Type name, Type name, ...
		while (peek() != ")"_tok) {
			// Parse type
			auto type_result = parse_type_specifier();
			if (type_result.is_error()) {
				return type_result;
			}
			
			// Parse pointer/reference modifiers after the type
			TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
			
			// Check for parenthesized declarator: type(&name)(params) or type(*name)(params)
			// This is used for function pointer/reference parameters
			if (peek() == "("_tok) {
				advance(); // consume '('
				
				// Expect & or * for function reference/pointer
				if (peek() == "&"_tok) {
					advance(); // consume '&'
					type_spec.set_reference(false);  // lvalue reference
				} else if (peek() == "*"_tok) {
					advance(); // consume '*'
					type_spec.add_pointer_level(CVQualifier::None);
				} else {
					return ParseResult::error("Expected '&' or '*' in function declarator", current_token_);
				}
				
				// Parse parameter name
				if (!peek().is_identifier()) {
					return ParseResult::error("Expected identifier in function declarator", current_token_);
				}
				Token param_name = peek_info();
				advance();
				
				// Expect closing ')'
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function declarator name", current_token_);
				}
				
				// Parse function parameter list: (params)
				if (!consume("("_tok)) {
					return ParseResult::error("Expected '(' for function parameter list", current_token_);
				}
				
				// Skip function parameters (we don't need them for requires expressions)
				int paren_depth = 1;
				while (paren_depth > 0 && !peek().is_eof()) {
					if (peek() == "("_tok) {
						paren_depth++;
					} else if (peek() == ")"_tok) {
						paren_depth--;
					}
					if (paren_depth > 0) advance();
				}
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function parameter list", current_token_);
				}
				
				// Create a declaration node for the parameter
				auto decl_node = emplace_node<DeclarationNode>(*type_result.node(), param_name);
				parameters.push_back(decl_node);
				
				// Add parameter to the scope so it can be used in the requires body
				gSymbolTable.insert(param_name.value(), decl_node);
				
				// Check for comma (more parameters) or end
				if (peek() == ","_tok) {
					advance(); // consume ','
				}
				
				continue; // Skip the rest of the loop for this parameter
			}
			
			// Parse cv-qualifiers (const, volatile)
			CVQualifier cv = parse_cv_qualifiers();
			type_spec.add_cv_qualifier(cv);
			
			// Parse pointer declarators
			while (peek() == "*"_tok) {
				advance(); // consume '*'
				CVQualifier ptr_cv = parse_cv_qualifiers();
				type_spec.add_pointer_level(ptr_cv);
			}
			
			// Parse reference qualifiers (& or &&)
			ReferenceQualifier ref = parse_reference_qualifier();
			if (ref == ReferenceQualifier::LValueReference) {
				type_spec.set_reference(false);  // false = lvalue reference
			} else if (ref == ReferenceQualifier::RValueReference) {
				type_spec.set_reference(true);   // true = rvalue reference
			}
			
			// Parse parameter name
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected parameter name in requires expression", current_token_);
			}
			Token param_name = peek_info();
			advance();
			
			// Check if this is a function reference/pointer: type(&name)(params) or type(*name)(params)
			// After the parameter name, if we see '(', it's a function declarator
			if (peek() == "("_tok) {
				// Pattern: void(&f)(T) means f is a reference to function taking T
				advance(); // consume '('
				
				// Parse the function parameter list (simplified - just skip to ')')
				// We don't need the full parameter info for requires expressions
				int paren_depth = 1;
				while (paren_depth > 0 && !peek().is_eof()) {
					if (peek() == "("_tok) {
						paren_depth++;
					} else if (peek() == ")"_tok) {
						paren_depth--;
					}
					if (paren_depth > 0) advance();
				}
				
				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after function declarator parameter list", current_token_);
				}
			}
			
			// Create a declaration node for the parameter
			auto decl_node = emplace_node<DeclarationNode>(*type_result.node(), param_name);
			parameters.push_back(decl_node);
			
			// Add parameter to the scope so it can be used in the requires body
			gSymbolTable.insert(param_name.value(), decl_node);
			
			// Check for comma (more parameters) or end
			if (peek() == ","_tok) {
				advance(); // consume ','
			}
		}
		
		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after requires expression parameters", current_token_);
		}
	}

	// Expect '{'
	if (!consume("{"_tok)) {
		return ParseResult::error("Expected '{' to begin requires expression body", current_token_);
	}

	// Enable SFINAE context for the requires expression body
	// In requires expressions, function lookup failures and type errors should not produce errors -
	// they indicate that the constraint is not satisfied (the expression is invalid)
	bool prev_sfinae_context = in_sfinae_context_;
	in_sfinae_context_ = true;
	
	// RAII guard to restore SFINAE context on all code paths
	ScopeGuard sfinae_guard([&]() { in_sfinae_context_ = prev_sfinae_context; });

	// Parse requirements (expressions that must be valid)
	std::vector<ASTNode> requirements;
	while (peek() != "}"_tok) {
		// Check for different types of requirements:
		// 1. Type requirement: typename TypeName;
		// 2. Compound requirement: { expression } -> Type; or just { expression };
		// 3. Nested requirement: requires constraint;
		// 4. Simple requirement: expression;
		
		if (peek().is_keyword() && peek() == "typename"_tok) {
			// Type requirement: typename T::type; or typename Op<Args...>;
			advance(); // consume 'typename'
			
			// Parse the type name - can be identifier, qualified name, or template instantiation
			if (!peek().is_identifier()) {
				return ParseResult::error("Expected type name after 'typename' in requires expression", current_token_);
			}
			Token type_name = peek_info();
			advance();
			
			// Handle qualified names (T::type) and template arguments (Op<Args...>)
			// Only continue parsing if we see :: or < 
			while (!peek().is_eof() && 
			       (peek() == "::"_tok || peek() == "<"_tok)) {
				if (peek() == "::"_tok) {
					advance(); // consume '::'
					if (peek().is_identifier()) {
						advance(); // consume qualified name part
					}
				} else if (peek() == "<"_tok) {
					// Parse template arguments using balanced bracket parsing
					advance(); // consume '<'
					int angle_depth = 1;
					while (angle_depth > 0 && !peek().is_eof()) {
						if (peek() == "<"_tok) {
							angle_depth++;
						} else if (peek() == ">"_tok) {
							angle_depth--;
						} else if (peek() == ">>"_tok) {
							// Handle >> as two >
							angle_depth -= 2;
						}
						advance();
					}
				}
			}
			
			// Create an identifier node for the type requirement
			auto type_req_node = emplace_node<IdentifierNode>(type_name);
			requirements.push_back(type_req_node);
			
			// Expect ';' after type requirement
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after type requirement in requires expression", current_token_);
			}
			continue;
		}
		
		if (peek() == "{"_tok) {
			// Compound requirement: { expression } noexcept_opt -> type-constraint_opt ;
			Token lbrace_token = peek_info();
			advance(); // consume '{'
			
			// Parse the expression
			auto expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (expr_result.is_error()) {
				return expr_result;
			}
			
			// Expect '}'
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' after compound requirement expression", current_token_);
			}
			
			// Check for optional noexcept specifier
			bool is_noexcept = false;
			if (peek() == "noexcept"_tok) {
				advance(); // consume 'noexcept'
				is_noexcept = true;
			}
			
			// Check for optional return type constraint: -> ConceptName or -> Type
			std::optional<ASTNode> return_type_constraint;
			if (peek() == "->"_tok) {
				advance(); // consume '->'
				
				// Parse the return type constraint (concept name or type)
				// This can be a concept name (identifier) or a type specifier
				auto type_result = parse_type_specifier();
				if (type_result.is_error()) {
					return type_result;
				}
				return_type_constraint = *type_result.node();
			}
			
			// Create CompoundRequirementNode
			auto compound_req = emplace_node<CompoundRequirementNode>(
				*expr_result.node(),
				return_type_constraint,
				is_noexcept,
				lbrace_token
			);
			requirements.push_back(compound_req);
			
			// Expect ';' after compound requirement
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after compound requirement in requires expression", current_token_);
			}
			continue;
		}
		
		if (peek().is_keyword() && peek() == "requires"_tok) {
			// Nested requirement: requires constraint;
			Token nested_requires_token = peek_info();
			advance(); // consume 'requires'
			
			// Parse the nested constraint expression
			auto constraint_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (constraint_result.is_error()) {
				return constraint_result;
			}
			
			// Create a RequiresClauseNode for the nested requirement
			auto nested_req = emplace_node<RequiresClauseNode>(
				*constraint_result.node(),
				nested_requires_token
			);
			requirements.push_back(nested_req);
			
			// Expect ';' after nested requirement
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after nested requirement in requires expression", current_token_);
			}
			continue;
		}
		
		// Simple requirement: just an expression
		auto req_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (req_result.is_error()) {
			return req_result;
		}
		requirements.push_back(*req_result.node());
		
		// Expect ';' after each requirement
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after requirement in requires expression", current_token_);
		}
	}

	// Expect '}'
	if (!consume("}"_tok)) {
		return ParseResult::error("Expected '}' to end requires expression", current_token_);
	}

	// Scope will be exited automatically by scope_guard

	// Create RequiresExpressionNode
	auto requires_expr_node = emplace_node<RequiresExpressionNode>(
		std::move(requirements),
		requires_token
	);

	return saved_position.success(requires_expr_node);
}

// Parse template parameter list: typename T, int N, ...
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

		// Expect identifier (parameter name)
		if (!peek().is_identifier()) {
			FLASH_LOG(Parser, Error, "Expected identifier for template template parameter name, got: ",
				(!peek().is_eof() ? std::string("'") + std::string(peek_info().value()) + "'" : "<EOF>"));
			return ParseResult::error("Expected identifier for template template parameter name", current_token_);
		}

		Token param_name_token = peek_info();
		std::string_view param_name = param_name_token.value();
		advance(); // consume parameter name

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
					
					// Apply pointer qualifiers if present (e.g., T*, T**, const T*)
					while (peek() == "*"_tok) {
						advance(); // consume '*'
						CVQualifier ptr_cv = parse_cv_qualifiers();
						type_spec.add_pointer_level(ptr_cv);
					}
					
					// Apply reference qualifiers if present (e.g., T& or T&&)
					apply_trailing_reference_qualifiers(type_spec);
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
					
					// Apply pointer qualifiers if present (e.g., T*, T**, const T*)
					while (peek() == "*"_tok) {
						advance(); // consume '*'
						CVQualifier ptr_cv = parse_cv_qualifiers();
						type_spec.add_pointer_level(ptr_cv);
					}
					
					// Apply reference qualifiers if present (e.g., T& or T&&)
					apply_trailing_reference_qualifiers(type_spec);
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
ParseResult Parser::parse_template_template_parameter_form() {
	ScopedTokenPosition saved_position(*this);

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
	bool is_constexpr = specs.is_constexpr;
	bool is_consteval = specs.is_consteval;
	bool is_constinit = specs.is_constinit;
	
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

	// Skip trailing requires clause during template instantiation
	// (the constraint was already evaluated during template argument deduction)
	skip_trailing_requires_clause();

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
				// For deleted template functions, we just record the pattern
				// The function is still registered as a template but will be rejected if called
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
					if (more_specs.is_constexpr) specs.is_constexpr = true;
					if (more_specs.is_consteval) specs.is_consteval = true;
					if (more_specs.is_constinit) specs.is_constinit = true;
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
				ctor_ref.set_constexpr(specs.is_constexpr);
				
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
						template_param_name_handles  // template_param_names for template constructors
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
					// Skip pointer/reference qualifiers on conversion target type
					while (peek() == "*"_tok || peek() == "&"_tok || peek() == "&&"_tok) {
						advance();
					}
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
						                                member_quals.is_const, member_quals.is_volatile);

						auto qualified_name = StringTable::getOrInternStringHandle(
							StringBuilder().append(struct_node.name()).append("::"sv).append(operator_name));
						gTemplateRegistry.registerTemplate(StringTable::getStringView(qualified_name), template_func_node);
						gTemplateRegistry.registerTemplate(operator_name, template_func_node);

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
	gTemplateRegistry.registerTemplate(StringTable::getStringView(qualified_name), template_func_node);
	
	// Also register with simple name for unqualified lookups (needed for inherited member template function calls)
	gTemplateRegistry.registerTemplate(decl_node.identifier_token().value(), template_func_node);

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

	// Handle pointer depth (*, **, etc.)
	while (peek() == "*"_tok) {
		advance(); // consume '*'

		// Parse CV-qualifiers after the * (const, volatile)
		CVQualifier ptr_cv = parse_cv_qualifiers();

		type_spec.add_pointer_level(ptr_cv);
	}

	// Handle reference modifiers (&, &&)
	if (peek() == "&"_tok) {
		advance(); // consume first '&'

		// Check for rvalue reference (&&)
		if (peek() == "&"_tok) {
			advance(); // consume second '&'
			type_spec.set_reference(true);  // true = rvalue reference
		} else {
			type_spec.set_lvalue_reference(true);  // lvalue reference
		}
	} else if (peek() == "&&"_tok) {
		// Handle && as a single token (rvalue reference)
		advance(); // consume '&&'
		type_spec.set_reference(true);  // true = rvalue reference
	}

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
	StringBuilder sb;
	std::string_view qualified_name = sb.append(struct_node.name()).append("::").append(alias_name).commit();
	gTemplateRegistry.register_alias_template(std::string(qualified_name), alias_node);

	FLASH_LOG_FORMAT(Parser, Info, "Registered member template alias: {}", qualified_name);

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
		
		// Create template struct node for the forward declaration
		auto template_struct_node = emplace_node<TemplateClassDeclarationNode>(
			std::move(template_params),
			std::move(template_param_names),
			forward_struct_node
		);
		
		// Register the template
		gTemplateRegistry.registerTemplate(StringTable::getStringView(qualified_name), template_struct_node);
		gTemplateRegistry.registerTemplate(struct_name, template_struct_node);
		
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
			if (arg.is_rvalue_reference) {
				pattern_name.append("RR"sv);
			} else if (arg.is_reference) {
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
		
		while (peek() != "}"_tok) {
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
					advance(); // consume 'using'
					
					// Parse the alias name
					if (!peek().is_identifier()) {
						return ParseResult::error("Expected alias name after 'using'", current_token_);
					}
					std::string_view alias_name = peek_info().value();
					advance(); // consume alias name
					
					// Check if this is an inheriting constructor: using Base::Base;
					// or a using-declaration: using Base::member;
					if (peek() == "::"_tok) {
						// Parse the full qualified name
						std::string_view base_class_name = alias_name;
						
						while (peek() == "::"_tok) {
							advance(); // consume '::'
							
							if (peek().is_identifier()) {
								alias_name = peek_info().value();  // Track last identifier
								advance(); // consume identifier
								
								// Skip template arguments if present
								if (peek() == "<"_tok) {
									skip_template_arguments();
								}
							}
						}
						
						// Check if this is an inheriting constructor
						bool is_inheriting_constructor = (alias_name == base_class_name);
						
						if (is_inheriting_constructor) {
							FLASH_LOG(Parser, Debug, "Inheriting constructors from '", base_class_name, "' in member struct template");
						} else {
							FLASH_LOG(Parser, Debug, "Using-declaration imports member '", alias_name, "' in member struct template");
						}
						
						// Consume trailing semicolon
						if (peek() == ";"_tok) {
							advance();
						}
						
						continue;  // Move to next member
					}
					
					// Expect '=' for type alias
					if (peek() != "="_tok) {
						return ParseResult::error("Expected '=' after alias name", current_token_);
					}
					advance(); // consume '='
					
					// Parse the aliased type
					auto type_result = parse_type_specifier();
					if (type_result.is_error()) {
						return type_result;
					}
					
					// Parse reference modifiers after the type (e.g., T&, T&&)
					// This allows patterns like: using type = remove_reference_t<T>&&;
					if (type_result.node().has_value()) {
						TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
						
						// Handle && (rvalue reference) - either as single token or two & tokens
						if (peek() == "&&"_tok) {
							advance(); // consume '&&'
							type_spec.set_reference(true);  // true = rvalue reference
						} else if (peek() == "&"_tok) {
							advance(); // consume first '&'
							if (peek() == "&"_tok) {
								advance(); // consume second '&'
								type_spec.set_reference(true);  // rvalue reference
							} else {
								type_spec.set_lvalue_reference(true);  // lvalue reference
							}
						}
					}
					
					// Expect ';'
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after using declaration", current_token_);
					}
					
					// Store the type alias in the struct
					if (type_result.node().has_value()) {
						StringHandle alias_name_handle = StringTable::getOrInternStringHandle(alias_name);
						member_struct_ref.add_type_alias(alias_name_handle, *type_result.node(), current_access);
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
			}
			
			// Save position BEFORE parsing specifiers so we can restore if needed
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
			
			// Check if this is a member function (has '(') or data member (has ';' or '=')
			if (peek() == ";"_tok) {
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
	
	while (peek() != "}"_tok) {
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
		} else if (peek() == ";"_tok) {
			// Data member
			advance(); // consume ';'
			member_struct_ref.add_member(*member_result.node(), current_access, std::nullopt);
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
	gTemplateRegistry.registerTemplate(StringTable::getStringView(qualified_name), template_struct_node);
	
	// Also register with simple name for lookups within the parent struct
	gTemplateRegistry.registerTemplate(struct_name, template_struct_node);

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
	std::string_view parent_name = StringTable::getStringView(struct_node.name());
	std::string_view qualified_name = StringBuilder()
		.append(parent_name)
		.append("::"sv)
		.append(var_name)
		.commit();
	
	// Register in template registry
	gTemplateRegistry.registerVariableTemplate(var_name, template_var_node);
	gTemplateRegistry.registerVariableTemplate(qualified_name, template_var_node);
	
	FLASH_LOG_FORMAT(Parser, Info, "Registered member variable template: {}", qualified_name);
	
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
		const_cast<Parser*>(this)->instantiateLazyStaticMember(type_name_handle, member_name_handle);
		
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
			const_cast<Parser*>(this)->instantiateLazyStaticMember(owner_struct->name, member_name_handle);
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
		const_cast<Parser*>(this)->instantiateLazyStaticMember(type_name_handle2, member_name_handle2);
		
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
					// Check for second & (in case lexer didn't combine them)
					if (peek() == "&"_tok) {
						is_rvalue_ref = true;
						is_lvalue_ref = false;
						advance(); // consume second '&'
					}
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
							type_node.set_reference(false);  // lvalue reference
						} else if (is_rvalue_ref) {
							type_node.set_reference(true);   // rvalue reference
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
		// 2. Its type name contains "_unknown" (composite type with template parameters)
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
				
				if (is_template_param || type_name.find("_unknown") != std::string_view::npos) {
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
std::optional<ASTNode> Parser::try_instantiate_template_explicit(std::string_view template_name, const std::vector<TemplateTypeArg>& explicit_types) {
	// FIRST: Check if we have an explicit specialization for these template arguments
	// This handles cases like: template<> int sum<int, int>(int, int) being called as sum<int, int>(3, 7)
	auto specialization_opt = gTemplateRegistry.lookupSpecialization(template_name, explicit_types);
	if (specialization_opt.has_value()) {
		FLASH_LOG(Templates, Debug, "Found explicit specialization for ", template_name);
		return *specialization_opt;
	}

	// Look up the template in the registry
	auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
	if (!template_opt.has_value()) {
		return std::nullopt;  // No template with this name
	}

	const ASTNode& template_node = *template_opt;
	if (!template_node.is<TemplateFunctionDeclarationNode>()) {
		return std::nullopt;  // Not a function template
	}

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

	// Check if template has a variadic parameter pack
	bool has_variadic_pack = false;
	for (const auto& param : template_params) {
		if (param.is<TemplateParameterNode>()) {
			const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
			if (tparam.is_variadic()) {
				has_variadic_pack = true;
				break;
			}
		}
	}

	// Verify we have the right number of template arguments
	// For variadic templates, we allow any number of arguments >= number of non-pack parameters
	if (!has_variadic_pack && explicit_types.size() != template_params.size()) {
		return std::nullopt;  // Wrong number of template arguments for non-variadic template
	}
	
	// For variadic templates, count non-pack parameters and verify we have at least that many
	if (has_variadic_pack) {
		size_t non_pack_params = 0;
		for (const auto& param : template_params) {
			if (param.is<TemplateParameterNode>()) {
				const TemplateParameterNode& tparam = param.as<TemplateParameterNode>();
				if (!tparam.is_variadic()) {
					++non_pack_params;
				}
			}
		}
		if (explicit_types.size() < non_pack_params) {
			return std::nullopt;  // Not enough template arguments
		}
	}

	// Build template argument list
	std::vector<TemplateArgument> template_args;
	size_t explicit_idx = 0;  // Track position in explicit_types
	for (size_t i = 0; i < template_params.size(); ++i) {
		if (!template_params[i].is<TemplateParameterNode>()) {
			FLASH_LOG_FORMAT(Templates, Error, "Template parameter {} is not a TemplateParameterNode (type: {})", i, template_params[i].type_name());
			continue;
		}
		const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
		if (param.kind() == TemplateParameterKind::Template) {
			// Template template parameter - extract the template name from explicit_types[i]
			// The parser stores template names as Type::Struct with a type_index pointing to the TypeInfo
			StringHandle tpl_name_handle;
			if (i < explicit_types.size()) {
				const auto& arg = explicit_types[i];
				// Template arguments are stored as Type::Struct with type_index pointing to the template's TypeInfo
				if (arg.base_type == Type::Struct && arg.type_index < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[arg.type_index];
					tpl_name_handle = type_info.name();
				} else if (arg.is_dependent) {
					// For dependent template arguments, use the dependent_name
					tpl_name_handle = arg.dependent_name;
				}
			}
			template_args.push_back(TemplateArgument::makeTemplate(tpl_name_handle));
			++explicit_idx;
		} else if (param.is_variadic()) {
			// Variadic parameter pack - consume all remaining explicit types
			for (size_t j = explicit_idx; j < explicit_types.size(); ++j) {
				template_args.push_back(toTemplateArgument(explicit_types[j]));
			}
			explicit_idx = explicit_types.size();  // All types consumed
		} else {
			// Regular type parameter - bounds check before access
			if (explicit_idx >= explicit_types.size()) {
				// Not enough explicit types - this overload doesn't match
				FLASH_LOG_FORMAT(Templates, Debug, "Template overload mismatch: need argument at position {} but only {} types provided", 
				                 explicit_idx, explicit_types.size());
				return std::nullopt;
			}
			// Use toTemplateArgument() to preserve full type info including references
			template_args.push_back(toTemplateArgument(explicit_types[explicit_idx]));
			++explicit_idx;
		}
	}

	// CHECK REQUIRES CLAUSE CONSTRAINT BEFORE INSTANTIATION
	FLASH_LOG(Templates, Debug, "try_instantiate_template_explicit: Checking requires clause for '", template_name, "', has_requires_clause=", template_func.has_requires_clause());
	if (template_func.has_requires_clause()) {
		const RequiresClauseNode& requires_clause = 
			template_func.requires_clause()->as<RequiresClauseNode>();
		
		// Get template parameter names for evaluation
		std::vector<std::string_view> eval_param_names;
		for (const auto& tparam_node : template_params) {
			if (tparam_node.is<TemplateParameterNode>()) {
				eval_param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
			}
		}
		
		// Create a copy of explicit_types with template template arg flags properly set
		std::vector<TemplateTypeArg> constraint_eval_args;
		size_t constraint_idx = 0;
		for (size_t i = 0; i < template_params.size() && constraint_idx < explicit_types.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) continue;
			const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
			
			if (param.kind() == TemplateParameterKind::Template) {
				// Template template parameter - mark the arg accordingly
				TemplateTypeArg arg = explicit_types[constraint_idx];
				arg.is_template_template_arg = true;
				// Get the template name from the TypeInfo
				if (arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
					arg.template_name_handle = gTypeInfo[arg.type_index].name();
				}
				constraint_eval_args.push_back(arg);
				++constraint_idx;
			} else if (param.is_variadic()) {
				// Variadic parameter pack - consume all remaining
				for (size_t j = constraint_idx; j < explicit_types.size(); ++j) {
					constraint_eval_args.push_back(explicit_types[j]);
				}
				constraint_idx = explicit_types.size();
			} else {
				// Regular type parameter
				constraint_eval_args.push_back(explicit_types[constraint_idx]);
				++constraint_idx;
			}
		}
		
		FLASH_LOG(Templates, Debug, "  Evaluating constraint with ", constraint_eval_args.size(), " template args and ", eval_param_names.size(), " param names");
		
		// Evaluate the constraint with the template arguments
		auto constraint_result = evaluateConstraint(
			requires_clause.constraint_expr(), constraint_eval_args, eval_param_names);
		
		FLASH_LOG(Templates, Debug, "  Constraint evaluation result: satisfied=", constraint_result.satisfied);
		
		if (!constraint_result.satisfied) {
			// Constraint not satisfied - report detailed error
			std::string args_str;
			for (size_t j = 0; j < constraint_eval_args.size(); ++j) {
				if (j > 0) args_str += ", ";
				args_str += constraint_eval_args[j].toString();
			}
			
			FLASH_LOG(Parser, Error, "constraint not satisfied for template function '", template_name, "'");
			FLASH_LOG(Parser, Error, "  ", constraint_result.error_message);
			if (!constraint_result.failed_requirement.empty()) {
				FLASH_LOG(Parser, Error, "  failed requirement: ", constraint_result.failed_requirement);
			}
			if (!constraint_result.suggestion.empty()) {
				FLASH_LOG(Parser, Error, "  suggestion: ", constraint_result.suggestion);
			}
			FLASH_LOG(Parser, Error, "  template arguments: ", args_str);
			
			// Don't create instantiation - constraint failed
			return std::nullopt;
		}
	}

	// Instantiate the template (same logic as try_instantiate_template)
	// Generate mangled name first - it now includes reference qualifiers
	std::string_view mangled_name = TemplateRegistry::mangleTemplateName(template_name, template_args);

	// Check if we already have this instantiation using the mangled name as key
	// This ensures that int, int&, and int&& are treated as distinct instantiations
	TemplateInstantiationKey key;
	key.template_name = StringTable::getOrInternStringHandle(mangled_name);  // Use mangled name for uniqueness
	// Note: We don't need to populate type_arguments since the mangled name already 
	// includes all type info including references

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		return *existing_inst;  // Return existing instantiation
	}

	const DeclarationNode& orig_decl = func_decl.decl_node();

	// Create a token for the mangled name
	Token mangled_token(Token::Type::Identifier, mangled_name,
	                    orig_decl.identifier_token().line(), orig_decl.identifier_token().column(),
	                    orig_decl.identifier_token().file_index());

	// Substitute template parameters in the return type
	const TypeSpecifierNode& orig_return_type = orig_decl.type_node().as<TypeSpecifierNode>();
	auto [substituted_return_type, substituted_return_type_index] = substitute_template_parameter(
		orig_return_type, template_params, explicit_types);
	
	// Create return type with substituted type, preserving qualifiers
	ASTNode return_type = emplace_node<TypeSpecifierNode>(
		substituted_return_type,
		substituted_return_type_index,
		get_type_size_bits(substituted_return_type),
		orig_return_type.token(),
		orig_return_type.cv_qualifier()
	);
	
	// Apply pointer levels and references from original type
	TypeSpecifierNode& return_type_ref = return_type.as<TypeSpecifierNode>();
	for (const auto& ptr_level : orig_return_type.pointer_levels()) {
		return_type_ref.add_pointer_level(ptr_level.cv_qualifier);
	}
	if (orig_return_type.is_reference() || orig_return_type.is_rvalue_reference()) {
		return_type_ref.set_reference(orig_return_type.is_rvalue_reference());
	}

	auto new_decl = emplace_node<DeclarationNode>(return_type, mangled_token);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(new_decl.as<DeclarationNode>());

	// Add parameters with concrete types
	for (size_t i = 0; i < func_decl.parameter_nodes().size(); ++i) {
		const auto& param = func_decl.parameter_nodes()[i];
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();

			// Get original parameter type
			const TypeSpecifierNode& orig_param_type = param_decl.type_node().as<TypeSpecifierNode>();
			
			// Substitute template parameters in the type
			auto [substituted_type, substituted_type_index] = substitute_template_parameter(
				orig_param_type, template_params, explicit_types);
			
			// Create new type specifier with substituted type
			ASTNode param_type = emplace_node<TypeSpecifierNode>(
				substituted_type,
				substituted_type_index,
				get_type_size_bits(substituted_type),
				orig_param_type.token(),
				orig_param_type.cv_qualifier()
			);
			
			// Apply pointer levels and references from original type
			TypeSpecifierNode& param_type_ref = param_type.as<TypeSpecifierNode>();
			for (const auto& ptr_level : orig_param_type.pointer_levels()) {
				param_type_ref.add_pointer_level(ptr_level.cv_qualifier);
			}
			if (orig_param_type.is_reference() || orig_param_type.is_rvalue_reference()) {
				param_type_ref.set_reference(orig_param_type.is_rvalue_reference());
			}

			auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_decl.identifier_token());
			new_func_ref.add_parameter_node(new_param_decl);
		}
	}

	// Handle the function body
	// Check if the template has a body position stored for re-parsing
	if (func_decl.has_template_body_position()) {
		// Re-parse the function body with template parameters substituted
		
		// Temporarily add the concrete types to the type system with template parameter names
		// Using RAII scope guard (Phase 6) for automatic cleanup
		FlashCpp::TemplateParameterScope template_scope;
		std::vector<std::string_view> param_names;
		param_names.reserve(template_params.size());
		for (const auto& tparam_node : template_params) {
			if (tparam_node.is<TemplateParameterNode>()) {
				param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
			}
		}
		
		for (size_t i = 0; i < param_names.size() && i < template_args.size(); ++i) {
			std::string_view param_name = param_names[i];
			Type concrete_type = template_args[i].type_value;

			auto& type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(param_name), concrete_type, gTypeInfo.size(), getTypeSizeFromTemplateArgument(template_args[i]));
			
			// Preserve reference qualifiers from template arguments
			// This ensures that when T=int&, the type T is properly marked as a reference
			if (template_args[i].type_specifier.has_value()) {
				const auto& ts = *template_args[i].type_specifier;
				type_info.is_reference_ = ts.is_reference();
				type_info.is_rvalue_reference_ = ts.is_rvalue_reference();
			}
			
			gTypesByName.emplace(type_info.name(), &type_info);
			template_scope.addParameter(&type_info);  // RAII cleanup on all return paths
		}

		// Save current position
		SaveHandle current_pos = save_token_position();
		
		// Save current parsing context (will be overwritten during template body parsing)
		const FunctionDeclarationNode* saved_current_function = current_function_;

		// Restore to the function body start (lexer only - keep AST nodes from previous instantiations)
		restore_lexer_position_only(func_decl.template_body_position());

		// Set up parsing context for the function
		gSymbolTable.enter_scope(ScopeType::Function);
		current_function_ = &new_func_ref;

		// Add parameters to symbol table
		for (const auto& param : new_func_ref.parameter_nodes()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				gSymbolTable.insert(param_decl.identifier_token().value(), param);
			}
		}

		// Set up template parameter substitutions for type parameters
		// This enables variable templates inside the function body to work correctly:
		// e.g., __is_ratio_v<_R1> where _R1 should be substituted with ratio<1,2>
		std::vector<TemplateParamSubstitution> saved_template_param_substitutions = std::move(template_param_substitutions_);
		template_param_substitutions_.clear();
		for (size_t i = 0; i < template_params.size() && i < explicit_types.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) continue;
			const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
			const TemplateTypeArg& arg = explicit_types[i];
			
			if (param.kind() == TemplateParameterKind::NonType && arg.is_value) {
				// Non-type parameter - store value for substitution
				TemplateParamSubstitution subst;
				subst.param_name = param.name();
				subst.is_value_param = true;
				subst.value = arg.value;
				subst.value_type = arg.base_type;
				template_param_substitutions_.push_back(subst);
				
				FLASH_LOG(Templates, Debug, "Registered non-type template parameter '", 
				          param.name(), "' with value ", arg.value, " for function template body");
			} else if (param.kind() == TemplateParameterKind::Type && !arg.is_value) {
				// Type parameter - store type for substitution
				TemplateParamSubstitution subst;
				subst.param_name = param.name();
				subst.is_value_param = false;
				subst.is_type_param = true;
				subst.substituted_type = arg;
				template_param_substitutions_.push_back(subst);
				
				FLASH_LOG(Templates, Debug, "Registered type template parameter '", 
				          param.name(), "' with type ", arg.toString(), " for function template body");
			}
		}

		// Parse the function body
		auto block_result = parse_block();
		
		// Restore the template parameter substitutions
		template_param_substitutions_ = std::move(saved_template_param_substitutions);
		
		if (!block_result.is_error() && block_result.node().has_value()) {
			// After parsing, we need to substitute template parameters in the body
			// This is essential for features like fold expressions that need AST transformation
			// Convert template_args to TemplateArgument format for substitution
			std::vector<TemplateArgument> converted_template_args;
			converted_template_args.reserve(template_args.size());
			for (const auto& arg : template_args) {
				if (arg.kind == TemplateArgument::Kind::Type) {
					converted_template_args.push_back(TemplateArgument::makeType(arg.type_value));
				} else if (arg.kind == TemplateArgument::Kind::Value) {
					converted_template_args.push_back(TemplateArgument::makeValue(arg.int_value, arg.value_type));
				}
			}
		
			ASTNode substituted_body = substituteTemplateParameters(
				*block_result.node(),
				template_params,
				converted_template_args
			);
		
			new_func_ref.set_definition(substituted_body);
		}
		
		// Clean up context
		current_function_ = nullptr;
		gSymbolTable.exit_scope();

		// Restore original position (lexer only - keep AST nodes we created)
		restore_lexer_position_only(current_pos);
		discard_saved_token(current_pos);
		
		// Restore parsing context
		current_function_ = saved_current_function;

		// template_scope RAII guard automatically removes temporary type infos
	} else {
		// Copy the function body if it exists (for non-template or already-parsed bodies)
		auto orig_body = func_decl.get_definition();
		if (orig_body.has_value()) {
			new_func_ref.set_definition(orig_body.value());
		}
	}

	// Copy function specifiers from original template
	new_func_ref.set_is_constexpr(func_decl.is_constexpr());
	new_func_ref.set_is_consteval(func_decl.is_consteval());
	new_func_ref.set_is_constinit(func_decl.is_constinit());
	new_func_ref.set_noexcept(func_decl.is_noexcept());
	new_func_ref.set_is_variadic(func_decl.is_variadic());
	new_func_ref.set_linkage(func_decl.linkage());
	new_func_ref.set_calling_convention(func_decl.calling_convention());

	// Compute and set the proper mangled name (Itanium/MSVC) for code generation
	compute_and_set_mangled_name(new_func_ref);
	
	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	// Add to symbol table at GLOBAL scope using the simple template name (e.g., identity_int)
	// Template instantiations should be globally visible, not scoped to where they're called
	// The simple name is used for template-specific lookups, while the computed mangled name
	// (from compute_and_set_mangled_name above) is used for code generation and linking
	gSymbolTable.insertGlobal(mangled_token.value(), new_func_node);

	// Add to top-level AST so it gets visited by the code generator
	ast_nodes_.push_back(new_func_node);

	return new_func_node;
}

// Try to instantiate a function template with the given argument types
// Returns the instantiated function declaration node if successful
std::optional<ASTNode> Parser::try_instantiate_template(std::string_view template_name, const std::vector<TypeSpecifierNode>& arg_types) {
	PROFILE_TEMPLATE_INSTANTIATION(std::string(template_name) + "_func");
	
	static int recursion_depth = 0;
	recursion_depth++;
	
	if (recursion_depth > 10) {
		FLASH_LOG(Templates, Error, "try_instantiate_template recursion depth exceeded 10! Possible infinite loop for template '", template_name, "'");
		recursion_depth--;
		return std::nullopt;
	}

	// Look up ALL templates with this name (for SFINAE support with same-name overloads)
	const std::vector<ASTNode>* all_templates = gTemplateRegistry.lookupAllTemplates(template_name);
	
	// If not found, try namespace-qualified lookup.
	// When inside a namespace (e.g., std) and looking up "__detail::__or_fn",
	// we need to also try "std::__detail::__or_fn" since templates are registered
	// with their fully-qualified names.
	if (!all_templates || all_templates->empty()) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		if (!current_handle.isGlobal()) {
			// Build the fully-qualified name by prepending the current namespace path
			StringHandle template_handle = StringTable::getOrInternStringHandle(template_name);
			StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, template_handle);
			std::string_view qualified_name_view = StringTable::getStringView(qualified_handle);
			// Note: qualified_name_view points to StringTable storage, which remains
			// valid for the duration of this function. The lookup only needs the
			// string_view temporarily, so no std::string allocation needed.
			
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Template '{}' not found, trying qualified name '{}'",
				recursion_depth, template_name, qualified_name_view);
			
			all_templates = gTemplateRegistry.lookupAllTemplates(qualified_name_view);
		}
	}
	
	// If still not found, check if we're inside a struct and look for inherited template functions
	if ((!all_templates || all_templates->empty()) && !struct_parsing_context_stack_.empty()) {
		// Get the current struct context
		const auto& current_struct_context = struct_parsing_context_stack_.back();
		StringHandle current_struct_name = StringTable::getOrInternStringHandle(current_struct_context.struct_name);
		
		FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Template '{}' not found, checking inherited templates from struct '{}'",
			recursion_depth, template_name, current_struct_context.struct_name);
		
		all_templates = lookup_inherited_template(current_struct_name, template_name);
		
		if (all_templates && !all_templates->empty()) {
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Found {} inherited template overload(s) for '{}'",
				recursion_depth, all_templates->size(), template_name);
		}
	}
	
	if (!all_templates || all_templates->empty()) {
		// This is expected for regular (non-template) functions - the caller will fall back
		// to creating a forward declaration. Only log at Debug level to avoid noise.
		FLASH_LOG(Templates, Debug, "[depth=", recursion_depth, "]: Template '", template_name, "' not found in registry");
		recursion_depth--;
		return std::nullopt;
	}

	FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Found {} template overload(s) for '{}'", 
		recursion_depth, all_templates->size(), template_name);

	// Try each template overload in order
	// For SFINAE: If instantiation fails due to substitution errors, silently skip to next overload
	for (size_t overload_idx = 0; overload_idx < all_templates->size(); ++overload_idx) {
		const ASTNode& template_node = (*all_templates)[overload_idx];
		
		if (!template_node.is<TemplateFunctionDeclarationNode>()) {
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Skipping overload {} - not a function template", 
				recursion_depth, overload_idx);
			continue;
		}

		FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Trying template overload {} for '{}'", 
			recursion_depth, overload_idx, template_name);

		// Enable SFINAE context for this instantiation attempt
		bool prev_sfinae_context = in_sfinae_context_;
		in_sfinae_context_ = true;

		// Try to instantiate this specific template
		std::optional<ASTNode> result = try_instantiate_single_template(
			template_node, template_name, arg_types, recursion_depth);

		// Restore SFINAE context
		in_sfinae_context_ = prev_sfinae_context;

		if (result.has_value()) {
			// Success! Return this instantiation
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Successfully instantiated overload {} for '{}'", 
				recursion_depth, overload_idx, template_name);
			recursion_depth--;
			return result;
		}

		// Instantiation failed - try next overload (SFINAE)
		FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Overload {} failed substitution, trying next", 
			recursion_depth, overload_idx);
	}

	// All overloads failed
	FLASH_LOG_FORMAT(Templates, Error, "[depth={}]: All {} template overload(s) failed for '{}'", 
		recursion_depth, all_templates->size(), template_name);
	recursion_depth--;
	return std::nullopt;
}

// Helper function: Try to instantiate a specific template node
// This contains the core instantiation logic extracted from try_instantiate_template
// Returns nullopt if instantiation fails (for SFINAE)
std::optional<ASTNode> Parser::try_instantiate_single_template(
	const ASTNode& template_node, 
	std::string_view template_name, 
	const std::vector<TypeSpecifierNode>& arg_types,
	int& recursion_depth)
{
	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

	// Step 1: Deduce template arguments from function call arguments
	// For now, we support simple type parameter deduction
	// We deduce template parameters in order from function arguments
	// TODO: Add support for non-type parameters and more complex deduction

	// Check if we have only variadic parameters - they can be empty
	bool all_variadic = true;
	bool has_variadic_pack = false;
	for (const auto& template_param_node : template_params) {
		const TemplateParameterNode& param = template_param_node.as<TemplateParameterNode>();
		if (!param.is_variadic()) {
			all_variadic = false;
		} else {
			has_variadic_pack = true;
		}
	}

	if (arg_types.empty() && !all_variadic) {
		return std::nullopt;  // No arguments to deduce from
	}

	// SFINAE: Check function parameter count against call argument count
	// For non-variadic templates, argument count must be <= parameter count (some may have defaults)
	// and >= count of parameters without default values
	// For variadic templates, argument count must be >= non-pack parameter count
	size_t func_param_count = func_decl.parameter_nodes().size();
	if (!has_variadic_pack) {
		if (arg_types.size() > func_param_count) {
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: SFINAE: argument count {} > parameter count {} for non-variadic template '{}'",
				recursion_depth, arg_types.size(), func_param_count, template_name);
			return std::nullopt;
		}
		// Count required parameters (those without default values)
		size_t required_params = 0;
		for (const auto& param : func_decl.parameter_nodes()) {
			if (param.is<DeclarationNode>() && !param.as<DeclarationNode>().has_default_value()) {
				required_params++;
			}
		}
		if (arg_types.size() < required_params) {
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: SFINAE: argument count {} < required parameter count {} for non-variadic template '{}'",
				recursion_depth, arg_types.size(), required_params, template_name);
			return std::nullopt;
		}
	} else {
		// Variadic: count non-pack parameters (all params except the pack expansion)
		size_t non_pack_params = func_param_count > 0 ? func_param_count - 1 : 0;
		if (arg_types.size() < non_pack_params) {
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: SFINAE: argument count {} < non-pack parameter count {} for variadic template '{}'",
				recursion_depth, arg_types.size(), non_pack_params, template_name);
			return std::nullopt;
		}
	}

	// Build template argument list
	std::vector<TemplateArgument> template_args;
	std::vector<Type> deduced_type_args;  // For types extracted from instantiated names

	// Deduce template parameters in order from function arguments
	// For template<typename T, typename U> T func(T a, U b):
	//   - T is deduced from first argument
	//   - U is deduced from second argument
	size_t arg_index = 0;
	for (const auto& template_param_node : template_params) {
		const TemplateParameterNode& param = template_param_node.as<TemplateParameterNode>();

		if (param.kind() == TemplateParameterKind::Template) {
			// Template template parameter - deduce from argument type
			if (arg_index < arg_types.size()) {
				const TypeSpecifierNode& arg_type = arg_types[arg_index];
				
				// Template template parameters can only be deduced from struct types
				if (arg_type.type() == Type::Struct) {
					// Get the struct name (e.g., "Vector_int")
					TypeIndex type_index = arg_type.type_index();
					if (type_index < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_index];
						
						// Phase 6: Use TypeInfo::isTemplateInstantiation() to check if this is a template instantiation
						// and baseTemplateName() to get the template name without parsing
						if (type_info.isTemplateInstantiation()) {
							// Get the base template name directly from TypeInfo metadata
							StringHandle inner_template_name = type_info.baseTemplateName();
							
							// Check if this template exists
							auto template_check = gTemplateRegistry.lookupTemplate(inner_template_name);
							if (template_check.has_value()) {
								template_args.push_back(TemplateArgument::makeTemplate(inner_template_name));
								
								// For hash-based naming, type arguments can be retrieved from TypeInfo::templateArgs()
								// instead of parsing the name string
								const auto& stored_args = type_info.templateArgs();
								for (const auto& stored_arg : stored_args) {
									if (!stored_arg.is_value) {
										deduced_type_args.push_back(stored_arg.base_type);
									}
								}
								
								arg_index++;
							} else {
								FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Template '", inner_template_name, "' not found");

								return std::nullopt;
							}
						} else {
							// Not a template instantiation - cannot deduce template template parameter
							std::string_view type_name = StringTable::getStringView(type_info.name());
							FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Type '", type_name, "' is not a template instantiation");

							return std::nullopt;
						}
					} else {
						FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Invalid type index ", static_cast<int>(type_index));

						return std::nullopt;
					}
				} else {
					FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Template template parameter requires struct argument, got type ", static_cast<int>(arg_type.type()));

					return std::nullopt;
				}
			} else {
				FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Not enough arguments to deduce template template parameter");

				return std::nullopt;
			}
		} else if (param.kind() == TemplateParameterKind::Type) {
			// Type parameter - check if it's variadic (parameter pack)
			if (param.is_variadic()) {
				// Deduce all remaining argument types for this parameter pack
				while (arg_index < arg_types.size()) {
					// Store full TypeSpecifierNode to preserve reference info for perfect forwarding
					template_args.push_back(TemplateArgument::makeTypeSpecifier(arg_types[arg_index]));
					arg_index++;
				}
				
				// Note: If no arguments remain, the pack is empty (which is valid)
			} else {
				// Non-variadic type parameter
				if (!deduced_type_args.empty()) {
					Type deduced_type = deduced_type_args[0];
					template_args.push_back(TemplateArgument::makeType(deduced_type));
					deduced_type_args.erase(deduced_type_args.begin());
				} else if (arg_index < arg_types.size()) {
					// Store full TypeSpecifierNode to preserve reference info for perfect forwarding
					template_args.push_back(TemplateArgument::makeTypeSpecifier(arg_types[arg_index]));
					arg_index++;
				} else {
					// Not enough arguments to deduce all template parameters
					// Fall back to first argument for remaining parameters
					// Store full TypeSpecifierNode to preserve reference info
					template_args.push_back(TemplateArgument::makeTypeSpecifier(arg_types[0]));
				}
			}
		} else {
			// Non-type parameter - check if it has a default value
			if (param.has_default()) {
				// Use the default value for non-type parameters
				// The default value is an expression that will be evaluated during instantiation
				// For now, we skip it in deduction and let the instantiation phase use the default
				FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Non-type parameter '{}' has default, skipping deduction",
					recursion_depth, param.name());
				// Don't add anything to template_args - the instantiation will use the default
				continue;
			}
			// No default value and can't deduce - fail
			FLASH_LOG(Templates, Error, "[depth=", recursion_depth, "]: Non-type parameter not supported in deduction");

			return std::nullopt;
		}
	}

	// Step 2: Check if we already have this instantiation
	TemplateInstantiationKey key;
	key.template_name = StringTable::getOrInternStringHandle(template_name);
	for (const auto& arg : template_args) {
		if (arg.kind == TemplateArgument::Kind::Type) {
			key.type_arguments.push_back(arg.type_value);
		} else if (arg.kind == TemplateArgument::Kind::Template) {
			key.template_arguments.push_back(arg.template_name);
		} else {
			key.value_arguments.push_back(arg.int_value);
		}
	}

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		FLASH_LOG(Templates, Debug, "[depth=", recursion_depth, "]: Found existing instantiation, returning it");
		PROFILE_TEMPLATE_CACHE_HIT(std::string(template_name) + "_func");
		return *existing_inst;  // Return existing instantiation
	}
	PROFILE_TEMPLATE_CACHE_MISS(std::string(template_name) + "_func");

	// Step 3: Instantiate the template
	// For Phase 2, we'll create a simplified instantiation
	// We'll just use the original function with a mangled name
	// Full AST cloning and substitution will be implemented later

	// Generate mangled name for the instantiation
	std::string_view mangled_name = TemplateRegistry::mangleTemplateName(template_name, template_args);

	// For now, we'll create a simple wrapper that references the original function
	// This is a temporary solution - proper instantiation requires:
	// 1. Cloning the entire AST subtree
	// 2. Substituting all template parameter references
	// 3. Type checking the instantiated code

	// Get the original function's declaration
	const DeclarationNode& orig_decl = func_decl.decl_node();

	// Convert template_args to TemplateTypeArg format for substitution
	std::vector<TemplateTypeArg> template_args_as_type_args;
	for (const auto& arg : template_args) {
		if (arg.kind == TemplateArgument::Kind::Type) {
			TemplateTypeArg type_arg;
			
			// If we have a full type_specifier, use it to preserve all type information
			// This is critical for perfect forwarding (T&& parameters)
			if (arg.type_specifier.has_value()) {
				const TypeSpecifierNode& type_spec = arg.type_specifier.value();
				type_arg.base_type = type_spec.type();
				type_arg.type_index = type_spec.type_index();
				type_arg.is_reference = type_spec.is_lvalue_reference();
				type_arg.is_rvalue_reference = type_spec.is_rvalue_reference();
				type_arg.pointer_depth = type_spec.pointer_depth();
				type_arg.cv_qualifier = type_spec.cv_qualifier();
			} else {
				// Fallback to legacy behavior for backward compatibility
				type_arg.base_type = arg.type_value;
				type_arg.type_index = 0;  // Simple types don't have an index
			}
			
			template_args_as_type_args.push_back(type_arg);
		} else if (arg.kind == TemplateArgument::Kind::Template) {
			// Handle template template parameters (e.g., Op in template<template<...> class Op>)
			// Store the template name so constraint evaluation can resolve Op<Args...>
			TemplateTypeArg type_arg;
			type_arg.is_template_template_arg = true;
			type_arg.template_name_handle = arg.template_name;
			// Try to find the template in the registry to get its type_index
			auto template_opt = gTemplateRegistry.lookupTemplate(arg.template_name);
			if (template_opt.has_value()) {
				// Found the template - store a reference to it
				auto type_handle = arg.template_name;
				auto type_it = gTypesByName.find(type_handle);
				if (type_it != gTypesByName.end()) {
					type_arg.type_index = type_it->second->type_index_;
				}
			}
			template_args_as_type_args.push_back(type_arg);
		}
		// Note: Value arguments aren't used in type substitution
	}

	// Check for explicit specialization before instantiating the primary template.
	// This handles cases like: template<> int identity<int>(int val) { return val + 1; }
	// being called as identity(5) where T=int is deduced from the argument.
	auto specialization_opt = gTemplateRegistry.lookupSpecialization(template_name, template_args_as_type_args);
	if (specialization_opt.has_value()) {
		FLASH_LOG(Templates, Debug, "[depth=", recursion_depth, "]: Found explicit specialization for deduced args of '", template_name, "'");
		return *specialization_opt;
	}

	// CHECK REQUIRES CLAUSE CONSTRAINT BEFORE INSTANTIATION
	FLASH_LOG(Templates, Debug, "Checking requires clause for template function '", template_name, "', has_requires_clause=", template_func.has_requires_clause());
	if (template_func.has_requires_clause()) {
		const RequiresClauseNode& requires_clause = 
			template_func.requires_clause()->as<RequiresClauseNode>();
		
		// Get template parameter names for evaluation
		std::vector<std::string_view> eval_param_names;
		for (const auto& tparam_node : template_params) {
			if (tparam_node.is<TemplateParameterNode>()) {
				eval_param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
			}
		}
		
		FLASH_LOG(Templates, Debug, "  Evaluating constraint with ", template_args_as_type_args.size(), " template args and ", eval_param_names.size(), " param names");
		
		// Evaluate the constraint with the template arguments
		auto constraint_result = evaluateConstraint(
			requires_clause.constraint_expr(), template_args_as_type_args, eval_param_names);
		
		FLASH_LOG(Templates, Debug, "  Constraint evaluation result: satisfied=", constraint_result.satisfied);
		
		if (!constraint_result.satisfied) {
			// Constraint not satisfied - report detailed error
			// Build template arguments string
			std::string args_str;
			for (size_t i = 0; i < template_args_as_type_args.size(); ++i) {
				if (i > 0) args_str += ", ";
				args_str += template_args_as_type_args[i].toString();
			}
			
			FLASH_LOG(Parser, Error, "constraint not satisfied for template function '", template_name, "'");
			FLASH_LOG(Parser, Error, "  ", constraint_result.error_message);
			if (!constraint_result.failed_requirement.empty()) {
				FLASH_LOG(Parser, Error, "  failed requirement: ", constraint_result.failed_requirement);
			}
			if (!constraint_result.suggestion.empty()) {
				FLASH_LOG(Parser, Error, "  suggestion: ", constraint_result.suggestion);
			}
			FLASH_LOG(Parser, Error, "  template arguments: ", args_str);
			
			// Don't create instantiation - constraint failed

			return std::nullopt;
		}
	}

	// Save the mangled name - we'll set it on the function node after creation
	// Do NOT use the mangled name as the identifier token!
	std::string_view saved_mangled_name = mangled_name;

	// Create return type - re-parse declaration if available (for SFINAE)
	const TypeSpecifierNode& orig_return_type = orig_decl.type_node().as<TypeSpecifierNode>();
	
	ASTNode return_type;
	Token func_name_token = orig_decl.identifier_token();
	
	// Check if we have a saved declaration position for re-parsing (SFINAE support)
	// Re-parse if we have a saved position AND the return type appears template-dependent
	bool should_reparse = func_decl.has_template_declaration_position();
	
	FLASH_LOG_FORMAT(Templates, Debug, "Checking re-parse for template function: has_position={}, return_type={}, type_index={}",
		should_reparse, static_cast<int>(orig_return_type.type()), orig_return_type.type_index());
	
	// Only re-parse if the return type is a placeholder for template-dependent types
	if (should_reparse) {
		if (orig_return_type.type() == Type::Void) {
			// Void return type - re-parse
			FLASH_LOG(Templates, Debug, "Return type is void - will re-parse");
			should_reparse = true;
		} else if (orig_return_type.type() == Type::UserDefined) {
			if (orig_return_type.type_index() == 0) {
				// UserDefined with type_index=0 is a placeholder (points to void)
				FLASH_LOG(Templates, Debug, "Return type is UserDefined placeholder (void) - will re-parse");
				should_reparse = true;
			} else if (orig_return_type.type_index() < gTypeInfo.size()) {
				const TypeInfo& orig_type_info = gTypeInfo[orig_return_type.type_index()];
				std::string_view type_name = StringTable::getStringView(orig_type_info.name());
				FLASH_LOG_FORMAT(Templates, Debug, "Return type name: '{}'", type_name);
				// Re-parse if type contains _unknown (legacy template-dependent marker)
				// OR if type name contains template parameter markers like _T or ::type (typename member access)
				// This is more robust than just checking for _unknown
				bool has_unknown = type_name.find("_unknown") != std::string::npos;
				bool has_template_param = type_name.find("_T") != std::string::npos || 
				                          type_name.find("::type") != std::string::npos;
				should_reparse = has_unknown || has_template_param;
				if (should_reparse) {
					FLASH_LOG(Templates, Debug, "Return type appears template-dependent - will re-parse");
				}
			} else {
				should_reparse = false;
			}
		} else {
			// Other types don't need re-parsing
			should_reparse = false;
		}
	}
	
	FLASH_LOG_FORMAT(Templates, Debug, "Should re-parse: {}", should_reparse);
	
	if (should_reparse) {
		FLASH_LOG_FORMAT(Templates, Debug, "Re-parsing function declaration for SFINAE validation, in_sfinae_context={}", in_sfinae_context_);
		
		// Save current position
		SaveHandle current_pos = save_token_position();
		
		// Restore to the declaration start
		restore_lexer_position_only(func_decl.template_declaration_position());
		
		// Add template parameters to the type system temporarily
		FlashCpp::TemplateParameterScope template_scope;
		std::vector<std::string_view> param_names;
		for (const auto& tparam_node : template_params) {
			if (tparam_node.is<TemplateParameterNode>()) {
				param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
			}
		}
		
		for (size_t i = 0; i < param_names.size() && i < template_args_as_type_args.size(); ++i) {
			std::string_view param_name = param_names[i];
			const TemplateTypeArg& arg = template_args_as_type_args[i];
			
			// Add this template parameter -> concrete type mapping
			auto& type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(param_name), arg.base_type, gTypeInfo.size(), 0);	// Placeholder size
			// Set type_size_ so parse_type_specifier treats this as a typedef and uses the base_type
			// This ensures that when "T" is parsed, it resolves to the concrete type (e.g., int)
			// instead of staying as UserDefined, which would cause toString() to return "unknown"
			// Only call get_type_size_bits for basic types (Void through MemberObjectPointer)
			if (arg.base_type >= Type::Void && arg.base_type <= Type::MemberObjectPointer) {
				type_info.type_size_ = static_cast<unsigned char>(get_type_size_bits(arg.base_type));
			} else {
				// For Struct, UserDefined, and other non-basic types, use type_index to get size
				if (arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
					type_info.type_size_ = gTypeInfo[arg.type_index].type_size_;
				} else {
					type_info.type_size_ = 0;  // Will be resolved later
				}
			}
			gTypesByName.emplace(type_info.name(), &type_info);
			template_scope.addParameter(&type_info);
		}
		
		// Re-parse the return type with template parameters in scope
		auto return_type_result = parse_type_specifier();
		
		FLASH_LOG(Parser, Debug, "Template instantiation: parsed return type, is_error=", return_type_result.is_error(), ", has_node=", return_type_result.node().has_value(), ", current_token=", current_token_.value(), ", token_type=", (int)current_token_.type());
		if (return_type_result.node().has_value() && return_type_result.node()->is<TypeSpecifierNode>()) {
			auto& rt = return_type_result.node()->as<TypeSpecifierNode>();
			
			// Check if there are reference qualifiers after the type specifier
			bool is_punctuator_or_operator = current_token_.type() == Token::Type::Punctuator || current_token_.type() == Token::Type::Operator;
			bool is_ampamp = current_token_.value() == "&&";
			bool is_amp = current_token_.value() == "&";
			
			if (is_punctuator_or_operator && is_ampamp) {
				advance();  // Consume &&
				rt.set_reference(true);  // Set rvalue reference
			} else if (is_punctuator_or_operator && is_amp) {
				advance();  // Consume &
				rt.set_lvalue_reference(true);  // Set lvalue reference
			}
		}
		
		// Restore position
		restore_lexer_position_only(current_pos);
		
		if (return_type_result.is_error()) {
			// SFINAE: Return type parsing failed - this is a substitution failure
			FLASH_LOG_FORMAT(Templates, Debug, "SFINAE: Return type parsing failed: {}", return_type_result.error_message());
			return std::nullopt;  // Substitution failure - try next overload
		}
		
		if (!return_type_result.node().has_value()) {
			FLASH_LOG(Templates, Debug, "SFINAE: Return type parsing returned no node");
			return std::nullopt;
		}
		
		return_type = *return_type_result.node();
		
		// SFINAE: Validate that the parsed type actually exists in the type system
		// This catches cases like "typename enable_if<false>::type" where parsing succeeds
		// but the type doesn't actually have a ::type member
		//
		// NOTE: This validation is limited - it can detect simple cases where the type
		// name contains "_unknown" (template-dependent placeholder), but cannot evaluate
		// complex constant expressions like "is_int<T>::value" in template arguments.
		// Full SFINAE support would require implementing constant expression evaluation
		// during template instantiation.
		if (return_type.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_spec = return_type.as<TypeSpecifierNode>();
			
			if (type_spec.type() == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
				
				// Check for placeholder/unknown types that indicate failed resolution
				if (StringTable::getStringView(type_info.name()).find("_unknown") != std::string::npos) {
					FLASH_LOG_FORMAT(Templates, Debug, "SFINAE: Return type contains unresolved template: {}", StringTable::getStringView(type_info.name()));
					return std::nullopt;  // Substitution failure
				}
			}
		}
		
		// Now we need to re-parse the function name after the return type
		// Parse type_and_name to get both
		restore_lexer_position_only(func_decl.template_declaration_position());
		
		// Add template parameters back
		FlashCpp::TemplateParameterScope template_scope2;
		for (size_t i = 0; i < param_names.size() && i < template_args_as_type_args.size(); ++i) {
			std::string_view param_name = param_names[i];
			const TemplateTypeArg& arg = template_args_as_type_args[i];
			auto& type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(param_name), arg.base_type, gTypeInfo.size(), 0); // Placeholder size
			// Set type_size_ so parse_type_specifier treats this as a typedef
			// Only call get_type_size_bits for basic types (Void through MemberObjectPointer)
			if (arg.base_type >= Type::Void && arg.base_type <= Type::MemberObjectPointer) {
				type_info.type_size_ = static_cast<unsigned char>(get_type_size_bits(arg.base_type));
			} else {
				// For Struct, UserDefined, and other non-basic types, use type_index to get size
				if (arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
					type_info.type_size_ = gTypeInfo[arg.type_index].type_size_;
				} else {
					type_info.type_size_ = 0;  // Will be resolved later
				}
			}
			gTypesByName.emplace(type_info.name(), &type_info);
			template_scope2.addParameter(&type_info);
		}
		
		auto type_and_name_result = parse_type_and_name();
		restore_lexer_position_only(current_pos);
		
		if (type_and_name_result.is_error() || !type_and_name_result.node().has_value()) {
			FLASH_LOG(Templates, Debug, "SFINAE: Function name parsing failed");
			return std::nullopt;
		}
		
		// Extract the function name token from the parsed result
		if (type_and_name_result.node()->is<DeclarationNode>()) {
			func_name_token = type_and_name_result.node()->as<DeclarationNode>().identifier_token();
		}
		
	} else {
		// Fallback: Use simple substitution (old behavior)
		auto [return_type_enum, return_type_index] = substitute_template_parameter(
			orig_return_type, template_params, template_args_as_type_args
		);
		
		FLASH_LOG(Parser, Debug, "substitute_template_parameter returned: type=", (int)return_type_enum, ", type_index=", return_type_index);
		if (return_type_index > 0 && return_type_index < gTypeInfo.size()) {
			FLASH_LOG(Parser, Debug, "  type_index points to: '", StringTable::getStringView(gTypeInfo[return_type_index].name()), "'");
		}
		
		TypeSpecifierNode new_return_type(
			return_type_enum,
			TypeQualifier::None,
			get_type_size_bits(return_type_enum),
			Token(),
			orig_return_type.cv_qualifier()  // Preserve const/volatile qualifiers (CVQualifier)
		);
		new_return_type.set_type_index(return_type_index);
		
		FLASH_LOG(Parser, Debug, "Template fallback: created return type with type=", (int)return_type_enum, ", type_index=", return_type_index);
		
		// Preserve reference qualifiers from original return type
		if (orig_return_type.is_reference()) {
			if (orig_return_type.is_rvalue_reference()) {
				new_return_type.set_reference(true);  // true = rvalue reference
			} else {
				new_return_type.set_lvalue_reference(true);
			}
		}
		
		// Preserve pointer levels
		for (const auto& ptr_level : orig_return_type.pointer_levels()) {
			new_return_type.add_pointer_level(ptr_level.cv_qualifier);
		}
		
		return_type = emplace_node<TypeSpecifierNode>(new_return_type);
	}

	// Resolve dependent qualified aliases like Helper_T::type after substituting template arguments
	auto resolve_dependent_member_alias = [&](ASTNode& type_node) {
		if (!type_node.is<TypeSpecifierNode>()) return;
		auto& ts = type_node.as<TypeSpecifierNode>();
		if (ts.type() != Type::UserDefined) return;
		TypeIndex idx = ts.type_index();
		if (idx >= gTypeInfo.size()) return;
		
		std::string_view type_name = StringTable::getStringView(gTypeInfo[idx].name());
		
		// Fast path: check alias registry for the exact dependent name
		if (auto direct_alias = gTemplateRegistry.lookup_alias_template(std::string(type_name)); 
		    direct_alias.has_value() && direct_alias->is<TemplateAliasNode>()) {
			const auto& alias_node = direct_alias->as<TemplateAliasNode>();
			if (alias_node.target_type().is<TypeSpecifierNode>()) {
				type_node = emplace_node<TypeSpecifierNode>(alias_node.target_type().as<TypeSpecifierNode>());
				FLASH_LOG(Templates, Debug, "Resolved dependent alias directly: ", type_name);
				return;
			}
		}
		
		auto sep_pos = type_name.find("::");
		if (sep_pos == std::string_view::npos) return;
		
		std::string base_part(type_name.substr(0, sep_pos));
		std::string_view member_part = type_name.substr(sep_pos + 2);
		auto build_resolved_handle = [](std::string_view base, std::string_view member) {
			StringBuilder sb;
			return StringTable::getOrInternStringHandle(sb.append(base).append("::").append(member).commit());
		};
		FLASH_LOG(Templates, Debug, "resolve_dependent_member_alias: type_name=", type_name,
		          " base_part=", base_part, " member_part=", member_part,
		          " template_args=", template_args_as_type_args.size());
		
		// Substitute template parameter names with concrete argument strings
		for (size_t i = 0; i < template_params.size() && i < template_args_as_type_args.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) continue;
			const auto& tparam = template_params[i].as<TemplateParameterNode>();
			std::string_view tname = tparam.name();
			auto pos = base_part.find(tname);
			if (pos != std::string::npos) {
				base_part.replace(pos, tname.size(), template_args_as_type_args[i].toString());
			}
		}
		
		StringHandle resolved_handle = build_resolved_handle(base_part, member_part);
		FLASH_LOG(Templates, Debug, "resolve_dependent_member_alias: resolved_name=",
		          StringTable::getStringView(resolved_handle));
		auto type_it = gTypesByName.find(resolved_handle);
		
		if (type_it == gTypesByName.end()) {
			// Try instantiating the base template to register member aliases
			// The base_part contains a mangled name like "enable_if_void_int"
			// We need to find the actual template name, which could be "enable_if" not just "enable"
			std::string_view base_template_name = extract_base_template_name(base_part);
			
			// Only try to instantiate if we found a class template (not a function template)
			if (!base_template_name.empty()) {
				auto template_opt = gTemplateRegistry.lookupTemplate(base_template_name);
				if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
					try_instantiate_class_template(base_template_name, template_args_as_type_args);
					
					std::string_view instantiated_base = get_instantiated_class_name(base_template_name, template_args_as_type_args);
					resolved_handle = build_resolved_handle(instantiated_base, member_part);
					type_it = gTypesByName.find(resolved_handle);
					
					// Fallback: also try using the primary template name (uninstantiated) to find a registered alias
					if (type_it == gTypesByName.end()) {
						StringHandle primary_handle = build_resolved_handle(base_template_name, member_part);
						type_it = gTypesByName.find(primary_handle);
					}
					FLASH_LOG(Templates, Debug, "resolve_dependent_member_alias: after instantiation lookup '",
					          StringTable::getStringView(resolved_handle), "' found=", (type_it != gTypesByName.end()));
				}
			}
		}
		
		if (type_it == gTypesByName.end()) {
			// Fallback: check alias templates registry
			auto alias_opt = gTemplateRegistry.lookup_alias_template(StringTable::getStringView(resolved_handle));
			if (alias_opt.has_value() && alias_opt->is<TemplateAliasNode>()) {
				const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
				if (alias_node.target_type().is<TypeSpecifierNode>()) {
					const auto& alias_ts = alias_node.target_type().as<TypeSpecifierNode>();
					type_node = emplace_node<TypeSpecifierNode>(alias_ts);
					FLASH_LOG(Templates, Debug, "Resolved dependent alias via registry '", type_name, "' -> ", alias_node.alias_name());
					return;
				}
			}
		} else {
			const TypeInfo* resolved_info = type_it->second;
			TypeSpecifierNode resolved_spec(
				resolved_info->type_,
				TypeQualifier::None,
				get_type_size_bits(resolved_info->type_),
				Token()
			);
			resolved_spec.set_type_index(resolved_info->type_index_);
			type_node = emplace_node<TypeSpecifierNode>(resolved_spec);
			FLASH_LOG(Templates, Debug, "Resolved dependent alias '", type_name, "' to type=", static_cast<int>(resolved_info->type_),
			          ", index=", resolved_info->type_index_);
		}
	};
	
	resolve_dependent_member_alias(return_type);
	if (return_type.is<TypeSpecifierNode>()) {
		const auto& rt = return_type.as<TypeSpecifierNode>();
		FLASH_LOG(Templates, Debug, "Template instantiation: final return type after alias resolve: type=",
		          static_cast<int>(rt.type()), " index=", rt.type_index());
	}
	
	// Use the original function name token for the declaration, not the mangled name
	auto new_decl = emplace_node<DeclarationNode>(return_type, func_name_token);
	
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(new_decl.as<DeclarationNode>());
	
	// Parse the template_name to extract namespace and function name
	// template_name might be like "ns::sum" or just "sum"
	std::vector<std::string_view> namespace_path;
	std::string_view function_name_only;
	
	size_t last_colon = template_name.rfind("::");
	if (last_colon != std::string_view::npos) {
		// Has namespace - split it
		std::string_view namespace_part = template_name.substr(0, last_colon);
		function_name_only = template_name.substr(last_colon + 2);
		
		// Parse namespace parts (could be nested like "a::b::c")
		size_t start = 0;
		while (start < namespace_part.size()) {
			size_t end = namespace_part.find("::", start);
			if (end == std::string_view::npos) {
				end = namespace_part.size();
			}
			if (end > start) {
				namespace_path.push_back(namespace_part.substr(start, end - start));
			}
			start = (end == namespace_part.size()) ? end : end + 2;
		}
	} else {
		// No namespace
		function_name_only = template_name;
	}

	// Add parameters with substituted types
	// Note: We compute the mangled name AFTER adding parameters, since the mangled name
	// includes parameter types in its encoding
	auto saved_outer_pack_param_info = std::move(pack_param_info_);
	pack_param_info_.clear();
	size_t arg_type_index = 0;  // Track which argument type we're using
	for (size_t i = 0; i < func_decl.parameter_nodes().size(); ++i) {
		const auto& param = func_decl.parameter_nodes()[i];
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			
			// Check if this is a parameter pack
			if (param_decl.is_parameter_pack()) {
				// Track how many elements this pack expands to
				size_t pack_start_index = arg_type_index;
				
				// Check if the original parameter type is an rvalue reference (for perfect forwarding)
				const TypeSpecifierNode& orig_param_type = param_decl.type_node().as<TypeSpecifierNode>();
				bool is_forwarding_reference = orig_param_type.is_rvalue_reference();
				
				// Expand the parameter pack into multiple parameters
				// Use all remaining argument types for this pack
				while (arg_type_index < arg_types.size()) {
					const TypeSpecifierNode& arg_type = arg_types[arg_type_index];
					
					// Create a new parameter with the concrete type
					ASTNode param_type = emplace_node<TypeSpecifierNode>(
						arg_type.type(),
						arg_type.qualifier(),
						arg_type.size_in_bits(),
						Token()
					);
					param_type.as<TypeSpecifierNode>().set_type_index(arg_type.type_index());
				
					// If the original parameter was a forwarding reference (T&&), apply reference collapsing
					// Reference collapsing rules:
					//   T& && → T&    (lvalue reference wins)
					//   T&& && → T&&  (both rvalue → rvalue)
					//   T && → T&&    (non-reference + && → rvalue reference)
					if (is_forwarding_reference) {
						if (arg_type.is_lvalue_reference()) {
							// Deduced type is lvalue reference (e.g., int&)
							// Applying && gives int& && which collapses to int&
							param_type.as<TypeSpecifierNode>().set_lvalue_reference(true);
						} else if (arg_type.is_rvalue_reference()) {
							// Deduced type is rvalue reference (e.g., int&&)
							// Applying && gives int&& && which collapses to int&&
							param_type.as<TypeSpecifierNode>().set_reference(true);  // rvalue reference
						} else {
							// Deduced type is non-reference (e.g., int from literal)
							// Applying && gives int&&
							param_type.as<TypeSpecifierNode>().set_reference(true);  // rvalue reference
						}
					}
				
					// Copy pointer levels and CV qualifiers
					for (const auto& ptr_level : arg_type.pointer_levels()) {
						param_type.as<TypeSpecifierNode>().add_pointer_level(ptr_level.cv_qualifier);
					}
					
					// Create parameter name: base_name + pack-relative index (e.g., args_0, args_1, ...)
					// Use pack-relative index so fold expression expansion can use 0-based indices
					StringBuilder param_name_builder;
					param_name_builder.append(param_decl.identifier_token().value());
					param_name_builder.append('_');
					param_name_builder.append(arg_type_index - pack_start_index);
					std::string_view param_name = param_name_builder.commit();
					
					Token param_token(Token::Type::Identifier, 
									 param_name,
									 param_decl.identifier_token().line(),
									 param_decl.identifier_token().column(),
									 param_decl.identifier_token().file_index());
				
					auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_token);
					new_func_ref.add_parameter_node(new_param_decl);
					
					arg_type_index++;
				}
				
				// Record the pack expansion size for use during body re-parsing
				size_t pack_size = arg_type_index - pack_start_index;
				// Store pack info for expansion during body re-parsing
				pack_param_info_.push_back({param_decl.identifier_token().value(), pack_start_index, pack_size});
				
			} else {
				// Regular parameter - substitute template parameters in the parameter type
				const TypeSpecifierNode& orig_param_type = param_decl.type_node().as<TypeSpecifierNode>();
				auto [subst_type, subst_type_index] = substitute_template_parameter(
					orig_param_type, template_params, template_args_as_type_args
				);

				ASTNode param_type = emplace_node<TypeSpecifierNode>(
					subst_type,
					TypeQualifier::None,
					get_type_size_bits(subst_type),
					Token(),
					orig_param_type.cv_qualifier()
				);
				param_type.as<TypeSpecifierNode>().set_type_index(subst_type_index);

				// Preserve pointer levels from the original declaration
				for (const auto& ptr_level : orig_param_type.pointer_levels()) {
					param_type.as<TypeSpecifierNode>().add_pointer_level(ptr_level.cv_qualifier);
				}

				// Handle forwarding references using the deduced argument type (if available)
				if (orig_param_type.is_rvalue_reference() && arg_type_index < arg_types.size()) {
					const TypeSpecifierNode& arg_type = arg_types[arg_type_index];
					if (arg_type.is_lvalue_reference()) {
						param_type.as<TypeSpecifierNode>().set_lvalue_reference(true);
					} else if (arg_type.is_rvalue_reference()) {
						param_type.as<TypeSpecifierNode>().set_reference(true);  // rvalue reference
					} else if (arg_type.is_reference()) {
						param_type.as<TypeSpecifierNode>().set_reference(arg_type.is_rvalue_reference());
					} else {
						param_type.as<TypeSpecifierNode>().set_reference(true);  // T && → T&&
					}
				} else if (orig_param_type.is_lvalue_reference()) {
					param_type.as<TypeSpecifierNode>().set_lvalue_reference(true);
				} else if (orig_param_type.is_rvalue_reference()) {
					param_type.as<TypeSpecifierNode>().set_reference(true);
				}

				auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_decl.identifier_token());
				new_func_ref.add_parameter_node(new_param_decl);

				if (arg_type_index < arg_types.size()) {
					arg_type_index++;
				}
			}
		}
	}

	// Compute the proper C++ ABI mangled name using NameMangling
	// We need to pass the function name, return type, parameter types, and namespace path
	// This MUST be done AFTER adding parameters since the mangled name encodes parameter types
	NameMangling::MangledName proper_mangled_name = NameMangling::generateMangledNameFromNode(new_func_ref, namespace_path);
	new_func_ref.set_mangled_name(proper_mangled_name.view());

	// Handle the function body
	// Check if the template has a body position stored for re-parsing
	if (func_decl.has_template_body_position()) {
		FLASH_LOG(Templates, Debug, "Template has body position, re-parsing function body");
		// Re-parse the function body with template parameters substituted
		const std::vector<ASTNode>& func_template_params = template_func.template_parameters();
		
		// Temporarily add the concrete types to the type system with template parameter names
		// Using RAII scope guard (Phase 6) for automatic cleanup
		FlashCpp::TemplateParameterScope template_scope;
		std::vector<std::string_view> param_names;
		for (const auto& tparam_node : func_template_params) {
			if (tparam_node.is<TemplateParameterNode>()) {
				param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
			}
		}
		
		for (size_t i = 0; i < param_names.size() && i < template_args.size(); ++i) {
			std::string_view param_name = param_names[i];
			Type concrete_type = template_args[i].type_value;

			auto& type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(param_name), concrete_type, gTypeInfo.size(), getTypeSizeFromTemplateArgument(template_args[i]));
			gTypesByName.emplace(type_info.name(), &type_info);
			template_scope.addParameter(&type_info);  // RAII cleanup on all return paths
		}

		// Save current position
		SaveHandle current_pos = save_token_position();
		
		// Save current parsing context (will be overwritten during template body parsing)
		const FunctionDeclarationNode* saved_current_function = current_function_;

		// Restore to the function body start (lexer only - keep AST nodes from previous instantiations)
		restore_lexer_position_only(func_decl.template_body_position());

		// Set up parsing context for the function
		gSymbolTable.enter_scope(ScopeType::Function);
		current_function_ = &new_func_ref;

		// Add parameters to symbol table
		for (const auto& param : new_func_ref.parameter_nodes()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				gSymbolTable.insert(param_decl.identifier_token().value(), param);
			}
		}

		// Set up pack parameter info for pack expansion during body re-parsing
		// Pack expansion in function calls (rest...) uses pack_param_info_ to expand
		// the pack name to rest_0, rest_1, etc. without adding the original name to scope
		// (adding to scope would break fold expressions which need the name unresolved)
		bool saved_has_parameter_packs = has_parameter_packs_;
		auto saved_pack_param_info = std::move(pack_param_info_);
		if (!saved_pack_param_info.empty()) {
			has_parameter_packs_ = true;
			pack_param_info_ = saved_pack_param_info;
		}

		// Set up template parameter substitutions for type parameters
		// This enables variable templates inside the function body to work correctly:
		// e.g., __is_ratio_v<_R1> where _R1 should be substituted with ratio<1,2>
		std::vector<TemplateParamSubstitution> saved_template_param_substitutions = std::move(template_param_substitutions_);
		template_param_substitutions_.clear();
		for (size_t i = 0; i < func_template_params.size() && i < template_args.size(); ++i) {
			if (!func_template_params[i].is<TemplateParameterNode>()) continue;
			const TemplateParameterNode& param = func_template_params[i].as<TemplateParameterNode>();
			const TemplateArgument& arg = template_args[i];
			
			if (arg.kind == TemplateArgument::Kind::Value) {
				// Non-type parameter - store value for substitution
				TemplateParamSubstitution subst;
				subst.param_name = param.name();
				subst.is_value_param = true;
				subst.value = arg.int_value;
				subst.value_type = arg.value_type;
				template_param_substitutions_.push_back(subst);
				
				FLASH_LOG(Templates, Debug, "Registered non-type template parameter '", 
				          param.name(), "' with value ", arg.int_value, " for function template body (deduced)");
			} else if (arg.kind == TemplateArgument::Kind::Type) {
				// Type parameter - convert TemplateArgument to TemplateTypeArg
				TemplateParamSubstitution subst;
				subst.param_name = param.name();
				subst.is_value_param = false;
				subst.is_type_param = true;
				// Build TemplateTypeArg from TemplateArgument
				subst.substituted_type.base_type = arg.type_value;
				subst.substituted_type.type_index = arg.type_index;
				subst.substituted_type.is_value = false;
				subst.substituted_type.is_dependent = false;  // These are concrete types
				if (arg.type_specifier.has_value()) {
					subst.substituted_type.is_reference = arg.type_specifier->is_lvalue_reference();
					subst.substituted_type.is_rvalue_reference = arg.type_specifier->is_rvalue_reference();
					subst.substituted_type.pointer_depth = arg.type_specifier->pointer_levels().size();
				}
				template_param_substitutions_.push_back(subst);
				
				FLASH_LOG(Templates, Debug, "Registered type template parameter '", 
				          param.name(), "' with type ", subst.substituted_type.toString(), " for function template body (deduced)");
			}
		}

		// Parse the function body
		auto block_result = parse_block();
		
		// Restore the template parameter substitutions
		template_param_substitutions_ = std::move(saved_template_param_substitutions);

		// Restore pack parameter info
		has_parameter_packs_ = saved_has_parameter_packs;
		pack_param_info_ = std::move(saved_outer_pack_param_info);
		
		if (!block_result.is_error() && block_result.node().has_value()) {
			// After parsing, we need to substitute template parameters in the body
			// This is essential for features like fold expressions that need AST transformation
			// Convert template_args to TemplateArgument format for substitution
			std::vector<TemplateArgument> converted_template_args;
			for (const auto& arg : template_args) {
				if (arg.kind == TemplateArgument::Kind::Type) {
					converted_template_args.push_back(TemplateArgument::makeType(arg.type_value));
				} else if (arg.kind == TemplateArgument::Kind::Value) {
					converted_template_args.push_back(TemplateArgument::makeValue(arg.int_value, arg.value_type));
				}
			}
		
			ASTNode substituted_body = substituteTemplateParameters(
				*block_result.node(),
				template_params,
				converted_template_args
			);
		
			new_func_ref.set_definition(substituted_body);
		}
		
		// Clean up context
		current_function_ = nullptr;
		gSymbolTable.exit_scope();

		// Restore original position (lexer only - keep AST nodes we created)
		restore_lexer_position_only(current_pos);
		discard_saved_token(current_pos);
		
		// Restore parsing context
		current_function_ = saved_current_function;

		// template_scope RAII guard automatically removes temporary type infos
	} else {
		// Fallback: copy the function body pointer directly (old behavior)
		auto orig_body = func_decl.get_definition();
		if (orig_body.has_value()) {
			new_func_ref.set_definition(orig_body.value());
		}

		// Restore outer pack parameter info (must happen on both branches)
		pack_param_info_ = std::move(saved_outer_pack_param_info);
	}

	// Analyze the function body to determine if it should be inline-always
	// This applies to both paths: re-parsed bodies and copied bodies
	const auto& func_definition = new_func_ref.get_definition();
	
	// If the function has no body, it MUST be inline-always
	// This happens when template bodies have unparseable statements that were skipped
	if (!func_definition.has_value()) {
		new_func_ref.set_inline_always(true);
		FLASH_LOG(Templates, Debug, "Marked template instantiation as inline_always (no body): ", 
		          new_func_ref.decl_node().identifier_token().value());
	} else if (func_definition->is<BlockNode>()) {
		const BlockNode& block = func_definition->as<BlockNode>();
		const auto& statements = block.get_statements();
		
		FLASH_LOG(Templates, Debug, "Analyzing template instantiation '", 
		          new_func_ref.decl_node().identifier_token().value(), "' for pure expression, statements=", statements.size());
		
		// Check if this is a pure expression function
		const bool is_pure_expr = std::invoke([&statements]()-> bool {
			bool is_pure_expr = true;	// assume true
			// Might be more than one statement: using declaration + return for example
			// This is still a pure expression if the return is a cast
			bool has_pure_return = false;
			
			statements.visit([&](const ASTNode& stmt) {
				if (stmt.is<TypedefDeclarationNode>()) {
					// Typedef statements are okay
				} else if (stmt.is<ReturnStatementNode>()) {
					const ReturnStatementNode& ret_stmt = stmt.as<ReturnStatementNode>();
					const auto& expr_opt = ret_stmt.expression();
					
					if (expr_opt.has_value() && expr_opt->is<ExpressionNode>()) {
						const ExpressionNode& expr = expr_opt->as<ExpressionNode>();
						
						// Check if the expression is a pure cast or simple identifier
						std::visit([&](const auto& e) {
							using T = std::decay_t<decltype(e)>;
							if constexpr (std::is_same_v<T, StaticCastNode> ||
											std::is_same_v<T, ReinterpretCastNode> ||
											std::is_same_v<T, ConstCastNode> ||
											std::is_same_v<T, IdentifierNode>) {
								has_pure_return = true;
							}
						}, expr);
					}
				} else {
					is_pure_expr = false;
				}
			});
			is_pure_expr &= static_cast<int>(has_pure_return);
			return is_pure_expr;
		});
		
		new_func_ref.set_inline_always(is_pure_expr);

		if (is_pure_expr) {
			FLASH_LOG(Templates, Debug, "Marked template instantiation as inline_always (pure expression): ", 
			          new_func_ref.decl_node().identifier_token().value());
		} else {
			// Function has computation/side effects - should generate normal calls
			// Explicitly set inline_always to false
			FLASH_LOG(Templates, Debug, "Template instantiation has computation/side effects (not inlining): ", 
			          new_func_ref.decl_node().identifier_token().value());
		}
	}

	// Mangled name was already computed and set above - don't recompute it!
	// The mangled name is proper_mangled_name and was already set on the function node
	
	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	// Add to symbol table at GLOBAL scope (not current scope)
	// Template instantiations should be globally visible, not scoped to where they're called
	// Use insertGlobal() to add to global scope without modifying the scope stack
	// Register with the human-readable template-specific name for template lookups
	gSymbolTable.insertGlobal(saved_mangled_name, new_func_node);

	// Add to top-level AST so it gets visited by the code generator
	ast_nodes_.push_back(new_func_node);


	return new_func_node;
}

// Get the mangled name for an instantiated class template using hash-based naming
// Example: Container<int> -> Container$a1b2c3d4 (hash-based, unambiguous)
std::string_view Parser::get_instantiated_class_name(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) {
	// Use hash-based naming to avoid underscore ambiguity
	// Old: "is_arithmetic_int" - could be is_arithmetic<int> or is_arithmetic + "_int" type!
	// New: "is_arithmetic$a1b2c3d4" - unambiguous hash-based name
	return FlashCpp::generateInstantiatedNameFromArgs(template_name, template_args);
}

// Helper function to instantiate base class template and register it in the AST
// This consolidates the duplicated code for instantiating base class templates
// Returns the instantiated name, or empty string_view if not a template
std::string_view Parser::instantiate_and_register_base_template(
	std::string_view& base_class_name, 
	const std::vector<TemplateTypeArg>& template_args) {
	
	// First check if the base class is a template alias (like bool_constant)
	auto alias_entry = gTemplateRegistry.lookup_alias_template(base_class_name);
	if (alias_entry.has_value()) {
		FLASH_LOG(Parser, Debug, "Base class '", base_class_name, "' is a template alias - resolving");
		
		const TemplateAliasNode& alias_node = alias_entry->as<TemplateAliasNode>();
		
		if (alias_node.is_deferred()) {
			// Deferred template alias - need to substitute template arguments
			const auto& param_names = alias_node.template_param_names();
			const auto& target_template_args = alias_node.target_template_args();
			std::vector<TemplateTypeArg> substituted_args;
			
			// For each argument in the target template
			for (size_t i = 0; i < target_template_args.size(); ++i) {
				const ASTNode& arg_node = target_template_args[i];
				
				if (arg_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& arg_type = arg_node.as<TypeSpecifierNode>();
					
					// Check if this arg references a parameter of the alias template
					bool is_alias_param = false;
					size_t alias_param_idx = 0;
					
					Token arg_token = arg_type.token();
					if (arg_token.type() == Token::Type::Identifier) {
						std::string_view arg_token_value = arg_token.value();
						for (size_t j = 0; j < param_names.size(); ++j) {
							if (arg_token_value == param_names[j].view()) {
								is_alias_param = true;
								alias_param_idx = j;
								break;
							}
						}
					}
					
					if (is_alias_param && alias_param_idx < template_args.size()) {
						substituted_args.push_back(template_args[alias_param_idx]);
					} else {
						substituted_args.push_back(TemplateTypeArg(arg_type));
					}
				}
			}
			
			// Now recursively instantiate the target template
			// The target might itself be a template alias (chain of aliases)
			std::string_view target_name(alias_node.target_template_name());
			std::string_view instantiated_name = instantiate_and_register_base_template(target_name, substituted_args);
			if (!instantiated_name.empty()) {
				base_class_name = instantiated_name;
				return instantiated_name;
			}
		}
	}
	
	// Check if the base class is a template class
	auto template_entry = gTemplateRegistry.lookupTemplate(base_class_name);
	if (template_entry) {
		// Try to instantiate the base template
		auto instantiated_base = try_instantiate_class_template(base_class_name, template_args);
		
		// If instantiation returned a struct node, add it to the AST so it gets visited during codegen
		// and get the actual instantiated name from the struct (which includes default arguments)
		if (instantiated_base.has_value() && instantiated_base->is<StructDeclarationNode>()) {
			ast_nodes_.push_back(*instantiated_base);
			// Get the actual instantiated name from the struct node (includes default args)
			StringHandle name_handle = instantiated_base->as<StructDeclarationNode>().name();
			std::string_view instantiated_name = StringTable::getStringView(name_handle);
			base_class_name = instantiated_name;
			return instantiated_name;
		}
		
		// If instantiation returned nullopt (already instantiated), look up the existing type
		// We need to fill in default arguments to find the correct name
		auto primary_template_opt = gTemplateRegistry.lookupTemplate(base_class_name);
		if (primary_template_opt.has_value() && primary_template_opt->is<TemplateClassDeclarationNode>()) {
			const TemplateClassDeclarationNode& primary_template = primary_template_opt->as<TemplateClassDeclarationNode>();
			const std::vector<ASTNode>& primary_params = primary_template.template_parameters();
			
			// Fill in defaults for missing arguments
			std::vector<TemplateTypeArg> filled_args = template_args;
			for (size_t i = filled_args.size(); i < primary_params.size(); ++i) {
				if (!primary_params[i].is<TemplateParameterNode>()) continue;
				
				const TemplateParameterNode& param = primary_params[i].as<TemplateParameterNode>();
				if (param.is_variadic()) continue;
				if (!param.has_default()) break;
				
				const ASTNode& default_node = param.default_value();
				if (param.kind() == TemplateParameterKind::Type && default_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
					filled_args.emplace_back(default_type);
					FLASH_LOG(Templates, Debug, "Filled in default type argument for param ", i);
				}
			}
			
			// Generate name with filled-in defaults
			std::string_view instantiated_name = get_instantiated_class_name(base_class_name, filled_args);
			base_class_name = instantiated_name;
			return instantiated_name;
		}
		
		// Fallback: use basic name without defaults
		std::string_view instantiated_name = get_instantiated_class_name(base_class_name, template_args);
		base_class_name = instantiated_name;
		return instantiated_name;
	}
	return std::string_view();
}

// Helper function to substitute template parameters in an expression
// This recursively traverses the expression tree and replaces constructor calls with template parameter types
ASTNode Parser::substitute_template_params_in_expression(
	const ASTNode& expr,
	const std::unordered_map<TypeIndex, TemplateTypeArg>& type_substitution_map,
	const std::unordered_map<std::string_view, int64_t>& nontype_substitution_map) {
	
	// ASTNode wraps types via std::any, check if it contains an ExpressionNode
	if (!expr.is<ExpressionNode>()) {
		FLASH_LOG(Templates, Debug, "substitute_template_params_in_expression: not an ExpressionNode");
		return expr; // Return as-is if not an expression
	}
	
	const ExpressionNode& expr_variant = expr.as<ExpressionNode>();
	FLASH_LOG(Templates, Debug, "substitute_template_params_in_expression: processing expression, variant index=", expr_variant.index());
	
	// Handle sizeof expressions
	if (std::holds_alternative<SizeofExprNode>(expr_variant)) {
		const SizeofExprNode& sizeof_node = std::get<SizeofExprNode>(expr_variant);
		
		// If sizeof has a type operand, check if it needs substitution
		if (sizeof_node.is_type() && sizeof_node.type_or_expr().is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_node = sizeof_node.type_or_expr().as<TypeSpecifierNode>();
			
			FLASH_LOG(Templates, Debug, "sizeof substitution: checking type_index=", type_node.type_index(), 
			          " type=", static_cast<int>(type_node.type()));
			
			// First, try to find by type_index
			auto it = type_substitution_map.find(type_node.type_index());
			if (it != type_substitution_map.end()) {
				FLASH_LOG(Templates, Debug, "sizeof substitution: FOUND match by type_index, substituting with ", it->second.toString());
				
				// Create a new type node with the substituted type
				const TemplateTypeArg& arg = it->second;
				TypeSpecifierNode new_type(
					arg.base_type,
					TypeQualifier::None,
					get_type_size_bits(arg.base_type),
					sizeof_node.sizeof_token()
				);
				new_type.set_type_index(arg.type_index);
				
				// Apply cv-qualifiers, references, and pointers from template argument
				if (arg.is_rvalue_reference) {
					new_type.set_reference(true);
				} else if (arg.is_reference) {
					new_type.set_lvalue_reference(true);
				}
				for (size_t p = 0; p < arg.pointer_depth; ++p) {
					new_type.add_pointer_level(CVQualifier::None);
				}
				
				// Create new sizeof with substituted type
				auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
				SizeofExprNode new_sizeof(new_type_node, sizeof_node.sizeof_token());
				return emplace_node<ExpressionNode>(new_sizeof);
			}
			
			// If not found by type_index, try to find by matching type name with any substitution value
			// This handles the case where template parameter type_indices don't match due to
			// multiple template parameters with the same name in different templates
			if (type_node.type() == Type::UserDefined && type_node.type_index() < gTypeInfo.size()) {
				std::string_view type_name = StringTable::getStringView(gTypeInfo[type_node.type_index()].name());
				FLASH_LOG(Templates, Debug, "sizeof substitution: checking by name: ", type_name);
				
				// Search substitution map for any entry where the key type_index has the same name
				for (const auto& [key_type_index, arg] : type_substitution_map) {
					if (key_type_index < gTypeInfo.size()) {
						std::string_view param_name = StringTable::getStringView(gTypeInfo[key_type_index].name());
						if (param_name == type_name) {
							FLASH_LOG(Templates, Debug, "sizeof substitution: FOUND match by name, substituting with ", arg.toString());
							
							// Create a new type node with the substituted type
							TypeSpecifierNode new_type(
								arg.base_type,
								TypeQualifier::None,
								get_type_size_bits(arg.base_type),
								sizeof_node.sizeof_token()
							);
							new_type.set_type_index(arg.type_index);
							
							// Apply cv-qualifiers, references, and pointers from template argument
							if (arg.is_rvalue_reference) {
								new_type.set_reference(true);
							} else if (arg.is_reference) {
								new_type.set_lvalue_reference(true);
							}
							for (size_t p = 0; p < arg.pointer_depth; ++p) {
								new_type.add_pointer_level(CVQualifier::None);
							}
							
							// Create new sizeof with substituted type
							auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
							SizeofExprNode new_sizeof(new_type_node, sizeof_node.sizeof_token());
							return emplace_node<ExpressionNode>(new_sizeof);
						}
					}
				}
			}
			
			FLASH_LOG(Templates, Debug, "sizeof substitution: NO match found");
		} else if (!sizeof_node.is_type()) {
			// If sizeof has an expression operand, recursively substitute
			auto new_operand = substitute_template_params_in_expression(
				sizeof_node.type_or_expr(), type_substitution_map, nontype_substitution_map);
			SizeofExprNode new_sizeof = SizeofExprNode::from_expression(new_operand, sizeof_node.sizeof_token());
			return emplace_node<ExpressionNode>(new_sizeof);
		}
	}
	
	// Handle identifiers that might be non-type template parameters
	if (std::holds_alternative<IdentifierNode>(expr_variant)) {
		const IdentifierNode& id_node = std::get<IdentifierNode>(expr_variant);
		std::string_view id_name = id_node.name();
		
		// Check if this identifier is a non-type template parameter
		auto it = nontype_substitution_map.find(id_name);
		if (it != nontype_substitution_map.end()) {
			// Replace the identifier with a numeric literal
			int64_t value = it->second;
			// Create a persistent string for the token value using StringBuilder
			std::string_view val_str = StringBuilder().append(value).commit();
			Token value_token(Token::Type::Literal, val_str, 0, 0, 0);
			return emplace_node<ExpressionNode>(
				NumericLiteralNode(value_token, static_cast<unsigned long long>(value), Type::Int, TypeQualifier::None, 32));
		}
	}
	
	// Handle constructor call: T(value) -> ConcreteType(value)
	if (std::holds_alternative<ConstructorCallNode>(expr_variant)) {
		const ConstructorCallNode& ctor = std::get<ConstructorCallNode>(expr_variant);
		const TypeSpecifierNode& ctor_type = ctor.type_node().as<TypeSpecifierNode>();
		
		// Check if this constructor type is in our substitution map
		// For variable templates with cleaned-up template parameters, the constructor
		// might have type_index=0 or some other invalid value. So we check if there's
		// exactly one entry in the map and assume any UserDefined constructor is for that type.
		if (ctor_type.type() == Type::UserDefined) {
			// If we have exactly one type substitution and this is a UserDefined constructor,
			// assume it's for the template parameter
			if (type_substitution_map.size() == 1) {
				const TemplateTypeArg& arg = type_substitution_map.begin()->second;
				
				// Create a new type specifier with the concrete type
				TypeSpecifierNode new_type(
					arg.base_type,
					TypeQualifier::None,
					get_type_size_bits(arg.base_type),
					ctor.called_from()
				);
				
				// Recursively substitute in arguments
				ChunkedVector<ASTNode> new_args;
				for (size_t i = 0; i < ctor.arguments().size(); ++i) {
					new_args.push_back(substitute_template_params_in_expression(ctor.arguments()[i], type_substitution_map, nontype_substitution_map));
				}
				
				// Create new constructor call with substituted type
				auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
				ConstructorCallNode new_ctor(new_type_node, std::move(new_args), ctor.called_from());
				return emplace_node<ExpressionNode>(new_ctor);
			}
		}
		
		// Not a template parameter constructor - recursively substitute in arguments
		ChunkedVector<ASTNode> new_args;
		for (size_t i = 0; i < ctor.arguments().size(); ++i) {
			new_args.push_back(substitute_template_params_in_expression(ctor.arguments()[i], type_substitution_map, nontype_substitution_map));
		}
		ConstructorCallNode new_ctor(ctor.type_node(), std::move(new_args), ctor.called_from());
		return emplace_node<ExpressionNode>(new_ctor);
	}
	
	// Handle binary operators - recursively substitute in both operands
	if (std::holds_alternative<BinaryOperatorNode>(expr_variant)) {
		const BinaryOperatorNode& binop = std::get<BinaryOperatorNode>(expr_variant);
		auto new_left = substitute_template_params_in_expression(
			binop.get_lhs(), type_substitution_map, nontype_substitution_map);
		auto new_right = substitute_template_params_in_expression(
			binop.get_rhs(), type_substitution_map, nontype_substitution_map);
		
		BinaryOperatorNode new_binop(
			binop.get_token(),
			new_left,
			new_right
		);
		return emplace_node<ExpressionNode>(new_binop);
	}
	
	// Handle unary operators - recursively substitute in operand
	if (std::holds_alternative<UnaryOperatorNode>(expr_variant)) {
		const UnaryOperatorNode& unop = std::get<UnaryOperatorNode>(expr_variant);
		
		// Special case: sizeof with a type operand that needs substitution
		// For example: sizeof(T) where T is a template parameter
		if (unop.op() == "sizeof" && unop.get_operand().is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_node = unop.get_operand().as<TypeSpecifierNode>();
			
			FLASH_LOG(Templates, Debug, "sizeof substitution: checking type_index=", type_node.type_index(), 
			          " type=", static_cast<int>(type_node.type()));
			
			// Check if this type needs substitution
			auto it = type_substitution_map.find(type_node.type_index());
			if (it != type_substitution_map.end()) {
				FLASH_LOG(Templates, Debug, "sizeof substitution: FOUND match, substituting with ", it->second.toString());
				
				// Create a new type node with the substituted type
				const TemplateTypeArg& arg = it->second;
				TypeSpecifierNode new_type(
					arg.base_type,
					TypeQualifier::None,
					get_type_size_bits(arg.base_type),
					unop.get_token()
				);
				// Apply cv-qualifiers, references, and pointers from template argument
				if (arg.is_rvalue_reference) {
					new_type.set_reference(true);
				} else if (arg.is_reference) {
					new_type.set_lvalue_reference(true);
				}
				for (size_t p = 0; p < arg.pointer_depth; ++p) {
					new_type.add_pointer_level(CVQualifier::None);
				}
				
				// Create new sizeof with substituted type
				auto new_type_node = emplace_node<TypeSpecifierNode>(new_type);
				UnaryOperatorNode new_unop(
					unop.get_token(),
					new_type_node,
					unop.is_prefix()
				);
				return emplace_node<ExpressionNode>(new_unop);
			} else {
				FLASH_LOG(Templates, Debug, "sizeof substitution: NO match found in map");
			}
		}
		
		// General case: recursively substitute in operand
		auto new_operand = substitute_template_params_in_expression(
			unop.get_operand(), type_substitution_map, nontype_substitution_map);
		
		UnaryOperatorNode new_unop(
			unop.get_token(),
			new_operand,
			unop.is_prefix()
		);
		return emplace_node<ExpressionNode>(new_unop);
	}
	
	// Handle qualified identifiers (e.g., SomeTemplate<T>::member)
	// Phase 3: For variable templates that reference class template static members,
	// substitution is intentionally deferred to try_instantiate_variable_template() because:
	// 1. The namespace component contains the mangled name with template parameters
	// 2. We don't have enough context here to re-parse and instantiate the template
	// 3. The type_substitution_map only contains type indices, not the full template arguments
	// The actual template instantiation happens in try_instantiate_variable_template() which has
	// access to concrete template arguments and can trigger proper specialization pattern matching.
	
	// For all expression types (including QualifiedIdentifierNode), return as-is
	return expr;
}

// Try to instantiate a class template with explicit template arguments
// Returns the instantiated StructDeclarationNode if successful
// Try to instantiate a variable template with the given template arguments
// Returns the instantiated variable declaration node or nullopt if already instantiated
std::optional<ASTNode> Parser::try_instantiate_variable_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) {
	// First, try to find a partial specialization that matches the template arguments
	// For example, is_reference_v<int&> should match is_reference_v<T&>
	// Pattern names are: template_name_R (lvalue ref), template_name_RR (rvalue ref), template_name_P (pointer)
	
	// Extract simple name from template_name (remove namespace prefix if present)
	std::string_view simple_template_name = template_name;
	size_t last_colon_pos = template_name.rfind("::");
	if (last_colon_pos != std::string_view::npos) {
		simple_template_name = template_name.substr(last_colon_pos + 2);
	}
	
	FLASH_LOG(Templates, Debug, "try_instantiate_variable_template: template_name='", template_name, 
		"' simple_name='", simple_template_name, "' args.size()=", template_args.size());
	
	// Check if any template argument is dependent (e.g., _Tp placeholder)
	// If so, we cannot instantiate - this happens when we're inside a template body
	for (size_t i = 0; i < template_args.size(); ++i) {
		const auto& arg = template_args[i];
		if (arg.is_dependent) {
			FLASH_LOG(Templates, Debug, "Skipping variable template '", template_name, 
			          "' instantiation - arg[", i, "] is dependent: ", arg.toString());
			return std::nullopt;
		}
	}
	
	for (const auto& original_arg : template_args) {
		// Check if this argument corresponds to a template parameter that has been substituted
		// This happens when variable templates are used inside function template bodies
		// e.g., __is_ratio_v<_R1> where _R1 has been substituted with ratio<1,2>
		TemplateTypeArg arg = original_arg;  // Make a copy that we can modify
		if ((arg.base_type == Type::UserDefined || arg.base_type == Type::Struct) && 
		    arg.type_index < gTypeInfo.size()) {
			std::string_view type_name = StringTable::getStringView(gTypeInfo[arg.type_index].name());
			// Check if this is a template parameter with a substitution
			for (const auto& subst : template_param_substitutions_) {
				if (subst.is_type_param && subst.param_name == type_name) {
					// Found! Use the substituted type instead
					FLASH_LOG(Templates, Debug, "Substituting template parameter '", type_name, 
					          "' with concrete type ", subst.substituted_type.toString());
					arg = subst.substituted_type;
					break;
				}
			}
		}
		
		FLASH_LOG(Templates, Debug, "  arg: is_reference=", arg.is_reference, 
			" is_rvalue_reference=", arg.is_rvalue_reference, 
			" pointer_depth=", arg.pointer_depth,
			" toString='", arg.toString(), "'");
		StringBuilder pattern_builder;
		// Try with simple name first (how specializations are typically registered)
		pattern_builder.append(simple_template_name);
		pattern_builder.append("_");
		
		// Include base type name for user-defined types to match partial specs
		// e.g., for __is_ratio_v<ratio<1,2>>, look for pattern "__is_ratio_v_ratio"
		if (arg.base_type == Type::UserDefined || arg.base_type == Type::Struct || arg.base_type == Type::Enum) {
			if (arg.type_index < gTypeInfo.size()) {
				const TypeInfo& arg_type_info = gTypeInfo[arg.type_index];
				// Get the simple name (without namespace) for pattern matching
				std::string_view type_name = StringTable::getStringView(arg_type_info.name());
				// Strip namespace prefix if present
				size_t last_colon = type_name.rfind("::");
				if (last_colon != std::string_view::npos) {
					type_name = type_name.substr(last_colon + 2);
				}
				
				// Phase 6: Use TypeInfo::isTemplateInstantiation() to check if this is a template instantiation
				// and baseTemplateName() to get the base name without parsing $
				if (arg_type_info.isTemplateInstantiation()) {
					type_name = StringTable::getStringView(arg_type_info.baseTemplateName());
				}
				
				pattern_builder.append(type_name);
			}
		}
		
		// Build pattern suffix based on type qualifiers
		if (arg.is_reference) {
			pattern_builder.append("R");  // lvalue reference
		} else if (arg.is_rvalue_reference) {
			pattern_builder.append("RR");  // rvalue reference
		}
		for (size_t i = 0; i < arg.pointer_depth; ++i) {
			pattern_builder.append("P");  // pointer
		}
		
		// Always try to look up partial specialization
		std::string_view pattern_key = pattern_builder.commit();
		auto spec_opt = gTemplateRegistry.lookupVariableTemplate(pattern_key);
		
		// Also try with qualified name if simple name lookup failed
		StringBuilder qualified_pattern_builder;
		if (!spec_opt.has_value() && template_name != simple_template_name) {
			qualified_pattern_builder.append(template_name);
			qualified_pattern_builder.append("_");
			if (arg.base_type == Type::UserDefined || arg.base_type == Type::Struct || arg.base_type == Type::Enum) {
				if (arg.type_index < gTypeInfo.size()) {
					const TypeInfo& arg_type_info = gTypeInfo[arg.type_index];
					std::string_view type_name = StringTable::getStringView(arg_type_info.name());
					size_t last_colon = type_name.rfind("::");
					if (last_colon != std::string_view::npos) {
						type_name = type_name.substr(last_colon + 2);
					}
					
					// Phase 6: Use TypeInfo::isTemplateInstantiation() instead of parsing $
					if (arg_type_info.isTemplateInstantiation()) {
						type_name = StringTable::getStringView(arg_type_info.baseTemplateName());
					}
					
					qualified_pattern_builder.append(type_name);
				}
			}
			if (arg.is_reference) {
				qualified_pattern_builder.append("R");
			} else if (arg.is_rvalue_reference) {
				qualified_pattern_builder.append("RR");
			}
			for (size_t i = 0; i < arg.pointer_depth; ++i) {
				qualified_pattern_builder.append("P");
			}
			std::string_view qualified_pattern_key = qualified_pattern_builder.commit();
			spec_opt = gTemplateRegistry.lookupVariableTemplate(qualified_pattern_key);
		} else {
			qualified_pattern_builder.reset();
		}
			
		if (spec_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "Found variable template partial specialization: ", pattern_key);
			// Use the specialization instead of the primary template
			if (spec_opt->is<TemplateVariableDeclarationNode>()) {
				const TemplateVariableDeclarationNode& spec_template = spec_opt->as<TemplateVariableDeclarationNode>();
				const VariableDeclarationNode& spec_var_decl = spec_template.variable_decl_node();
				
				// Get original token info from the specialization for better error reporting
				const Token& orig_token = spec_var_decl.declaration().identifier_token();
				
				// Generate unique name for this instantiation using hash-based naming
				std::string_view persistent_name = FlashCpp::generateInstantiatedNameFromArgs(simple_template_name, template_args);
				
				// Check if already instantiated
				if (gSymbolTable.lookup(persistent_name).has_value()) {
					return gSymbolTable.lookup(persistent_name);
				}
				
				// Create instantiated variable using the specialization's initializer
				// Use original token's line/column/file info for better diagnostics
				Token inst_token(Token::Type::Identifier, persistent_name, orig_token.line(), orig_token.column(), orig_token.file_index());
				TypeSpecifierNode bool_type(Type::Bool, TypeQualifier::None, 8, orig_token);  // 8 bits = 1 byte
				auto decl_node = emplace_node<DeclarationNode>(
					emplace_node<TypeSpecifierNode>(bool_type),
					inst_token
				);
				
				// Create the initializer expression - use 'true' for specializations that match reference types
				Token true_token(Token::Type::Keyword, "true"sv, orig_token.line(), orig_token.column(), orig_token.file_index());
				auto true_expr = emplace_node<ExpressionNode>(BoolLiteralNode(true_token, true));
				
				auto var_decl_node = emplace_node<VariableDeclarationNode>(
					decl_node,
					true_expr,  // Use 'true' as the initializer for reference specializations
					StorageClass::None
				);
				var_decl_node.as<VariableDeclarationNode>().set_is_constexpr(true);
				
				// Register in symbol table - use insertGlobal because we might be called during function parsing
				gSymbolTable.insertGlobal(persistent_name, var_decl_node);
				
				// Add to AST nodes for code generation - insert at beginning so it's generated before functions that use it
				ast_nodes_.insert(ast_nodes_.begin(), var_decl_node);
				
				return var_decl_node;
			}
		}
	}
	
	// No partial specialization found - use the primary template
	auto template_opt = gTemplateRegistry.lookupVariableTemplate(template_name);
	if (!template_opt.has_value()) {
		FLASH_LOG(Templates, Error, "Variable template '", template_name, "' not found");
		return std::nullopt;
	}
	
	if (!template_opt->is<TemplateVariableDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Expected TemplateVariableDeclarationNode");
		return std::nullopt;
	}
	
	const TemplateVariableDeclarationNode& var_template = template_opt->as<TemplateVariableDeclarationNode>();
	
	// Generate unique name for the instantiation using hash-based naming
	// This ensures consistent naming with class template instantiations
	std::string_view persistent_name = FlashCpp::generateInstantiatedNameFromArgs(simple_template_name, template_args);
	
	// Check if already instantiated
	if (gSymbolTable.lookup(persistent_name).has_value()) {
		return gSymbolTable.lookup(persistent_name);
	}
	
	// Perform template substitution
	const std::vector<ASTNode>& template_params = var_template.template_parameters();
	if (template_args.size() != template_params.size()) {
		FLASH_LOG(Templates, Error, "Template argument count mismatch: expected ", template_params.size(), 
		          ", got ", template_args.size());
		return std::nullopt;
	}
	
	// Get the original variable declaration
	const VariableDeclarationNode& orig_var_decl = var_template.variable_decl_node();
	const DeclarationNode& orig_decl = orig_var_decl.declaration();
	const TypeSpecifierNode& orig_type = orig_decl.type_node().as<TypeSpecifierNode>();
	
	// Build a map from template parameter type_index to concrete type for substitution
	std::unordered_map<TypeIndex, TemplateTypeArg> type_substitution_map;
	// Build a map from non-type template parameter name to value for substitution
	std::unordered_map<std::string_view, int64_t> nontype_substitution_map;
	
	// Substitute template parameter with concrete type
	// For now, assume simple case where the type is just the template parameter
	TypeSpecifierNode substituted_type = orig_type;
	
	// Build substitution maps for all template parameters
	for (size_t i = 0; i < template_params.size(); ++i) {
		if (!template_params[i].is<TemplateParameterNode>()) continue;
		
		const auto& tparam = template_params[i].as<TemplateParameterNode>();
		
		if (tparam.kind() == TemplateParameterKind::Type) {
			// For type template parameters, look up the type_index in gTypeInfo
			// The template parameter name was registered as a type during parsing
			const TemplateTypeArg& arg = template_args[i];
			
			// Find the type_index for this template parameter by name
			// During template parsing, template parameters are added to gTypeInfo
			// We need to find the type_index that corresponds to this template parameter name
			std::string_view param_name = tparam.name();
			TypeIndex param_type_index = 0;
			bool found_param = false;
			
			// IMPORTANT: If orig_type refers to a template parameter (Type::UserDefined),
			// we should use orig_type.type_index() directly, as it's the correct type_index
			// for THIS template's parameter. Searching by name can find the wrong type_index
			// when multiple templates use the same parameter name (e.g., 'T').
			if (orig_type.type() == Type::UserDefined) {
				// Check if orig_type's type name matches this template parameter
				if (orig_type.type_index() < gTypeInfo.size()) {
					std::string_view orig_type_name = StringTable::getStringView(gTypeInfo[orig_type.type_index()].name());
					if (orig_type_name == param_name) {
						// Use the type_index from orig_type directly
						param_type_index = orig_type.type_index();
						found_param = true;
					}
				}
			}
			
			// If we didn't find it from orig_type, search in gTypeInfo
			// This is needed for initializer expression substitution
			if (!found_param) {
				// Search for the template parameter in gTypeInfo
				// Template parameters have Type::UserDefined or Type::Template
				for (TypeIndex ti = 0; ti < gTypeInfo.size(); ++ti) {
					if (gTypeInfo[ti].type_ == Type::UserDefined || gTypeInfo[ti].type_ == Type::Template) {
						if (StringTable::getStringView(gTypeInfo[ti].name()) == param_name) {
							param_type_index = ti;
							found_param = true;
							break;
						}
					}
				}
			}
			
			// Add to substitution map if we found the type_index
			if (found_param) {
				type_substitution_map[param_type_index] = arg;
				FLASH_LOG(Templates, Debug, "Added type parameter substitution: ", param_name, 
				          " (type_index=", param_type_index, ") -> ", arg.toString());
			}
			
			// Also check if the variable's return type itself is the template parameter
			// (for cases like template<typename T> T value = T();)
			if (orig_type.type() == Type::UserDefined && orig_type.type_index() == param_type_index) {
				// Use original token info for better diagnostics
				const Token& orig_token = orig_decl.identifier_token();
				substituted_type = TypeSpecifierNode(
					arg.base_type,
					TypeQualifier::None,
					get_type_size_bits(arg.base_type),
					orig_token
				);
				// Apply cv-qualifiers, references, and pointers from template argument
				if (arg.is_rvalue_reference) {
					substituted_type.set_reference(true);
				} else if (arg.is_reference) {
					substituted_type.set_lvalue_reference(true);
				}
				for (size_t p = 0; p < arg.pointer_depth; ++p) {
					substituted_type.add_pointer_level(CVQualifier::None);
				}
			} else {
				FLASH_LOG(Templates, Debug, "Type does NOT match - skipping substitution for '", template_name, "'");
			}
		} else if (tparam.kind() == TemplateParameterKind::NonType) {
			// Handle non-type template parameters
			const TemplateTypeArg& arg = template_args[i];
			if (arg.is_value) {
				// Add to non-type substitution map
				nontype_substitution_map[tparam.name()] = arg.value;
				FLASH_LOG(Templates, Debug, "Added non-type parameter substitution: ", tparam.name(), " -> ", arg.value);
			}
		}
	}
	
	// Create new declaration with substituted type and instantiated name
	// Use original token's line/column/file info for better diagnostics
	const Token& orig_token = orig_decl.identifier_token();
	Token instantiated_name_token(Token::Type::Identifier, persistent_name, orig_token.line(), orig_token.column(), orig_token.file_index());
	auto new_type_node = emplace_node<TypeSpecifierNode>(substituted_type);
	auto new_decl_node = emplace_node<DeclarationNode>(new_type_node, instantiated_name_token);
	
	// Substitute template parameters in initializer expression
	std::optional<ASTNode> new_initializer = std::nullopt;
	if (orig_var_decl.initializer().has_value()) {
		FLASH_LOG(Templates, Debug, "Substituting initializer expression for variable template");
		new_initializer = substitute_template_params_in_expression(
			orig_var_decl.initializer().value(),
			type_substitution_map,
			nontype_substitution_map
		);
		FLASH_LOG(Templates, Debug, "Initializer substitution complete");
		
		// PHASE 3 FIX: After substitution, trigger instantiation of any class templates 
		// referenced in the initializer expression. This ensures specialization pattern 
		// matching happens before codegen.
		// For example: is_pointer_v<int*> = is_pointer_impl<int*>::value
		// After substitution, we need to instantiate is_pointer_impl<int*> which should
		// match the specialization pattern is_pointer_impl<T*> and inherit from true_type.
		if (new_initializer.has_value()) {
			FLASH_LOG(Templates, Debug, "Phase 3: Checking initializer for variable template '", template_name, 
			          "', is ExpressionNode: ", new_initializer->is<ExpressionNode>());
			
			if (new_initializer->is<ExpressionNode>()) {
				const ExpressionNode& init_expr = new_initializer->as<ExpressionNode>();
				
				// Check if the initializer is a qualified identifier (e.g., Template<Args>::member)
				bool is_qual_id = std::holds_alternative<QualifiedIdentifierNode>(init_expr);
				FLASH_LOG(Templates, Debug, "Phase 3: Is QualifiedIdentifierNode: ", is_qual_id);
				
				if (is_qual_id) {
					const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(init_expr);
					
					// The struct/class name is the namespace handle's name
					// For "is_pointer_impl<int*>::value", the namespace name is "is_pointer_impl<int*>"
					NamespaceHandle ns_handle = qual_id.namespace_handle();
					FLASH_LOG(Templates, Debug, "Phase 3: Namespace handle depth: ", gNamespaceRegistry.getDepth(ns_handle));
					
					if (!ns_handle.isGlobal()) {
						// Get the struct name from the namespace handle
						std::string_view struct_name_view = gNamespaceRegistry.getName(ns_handle);
						
						FLASH_LOG(Templates, Debug, "Phase 3: Struct name from qualified ID: '", struct_name_view, "'");
						
						// The struct name might be a mangled template instantiation
						// Old format: "is_pointer_impl_T" (underscore-based)
						// New format: "is_pointer_impl$47b270a920ee3ffb" (hash-based)
						// We need to extract the template name by removing the suffix
						size_t separator_pos = struct_name_view.find('$');
						if (separator_pos == std::string_view::npos) {
							separator_pos = struct_name_view.rfind('_');
						}
						
						std::string_view template_name_to_lookup = struct_name_view;
						if (separator_pos != std::string_view::npos) {
							std::string_view suffix = struct_name_view.substr(separator_pos + 1);
							// For hash-based: always extract base name (hash is always after $)
							// For underscore-based: check if suffix looks like a template parameter
							if (struct_name_view[separator_pos] == '$' || suffix.length() == 1 || suffix == "typename") {
								template_name_to_lookup = struct_name_view.substr(0, separator_pos);
								FLASH_LOG(Templates, Debug, "Phase 3: Extracted template name: '", template_name_to_lookup, "'");
							}
						}
						
						// Try to instantiate the struct/class referenced in the qualified identifier
						// Look it up to see if it's a template
						auto inner_template_opt = gTemplateRegistry.lookupTemplate(template_name_to_lookup);
						if (inner_template_opt.has_value() && template_args.size() > 0) {
							// This is a template - try to instantiate it with the concrete arguments
							// The template arguments from the variable template should be used
							FLASH_LOG(Templates, Debug, "Phase 3: Triggering instantiation of '", template_name_to_lookup, 
							          "' with ", template_args.size(), " args from variable template initializer");
							
							auto instantiated = try_instantiate_class_template(template_name_to_lookup, template_args);
							if (instantiated.has_value() && instantiated->is<StructDeclarationNode>()) {
								// Add to AST so it gets codegen
								ast_nodes_.push_back(*instantiated);
								
								// Now update the qualified identifier to use the correct instantiated name
								// Get the instantiated class name (e.g., "is_pointer_impl_intP")
								std::string_view instantiated_name = get_instantiated_class_name(template_name_to_lookup, template_args);
								FLASH_LOG(Templates, Debug, "Phase 3: Instantiated class name: '", instantiated_name, "'");
								
								// Create a new qualified identifier with the updated namespace
								// Get the parent namespace and add the instantiated name as a child
								NamespaceHandle parent_ns = gNamespaceRegistry.getParent(ns_handle);
								StringHandle instantiated_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
								NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(parent_ns, instantiated_name_handle);
								
								// Create new qualified identifier node
								QualifiedIdentifierNode new_qual_id(new_ns_handle, qual_id.identifier_token());
								new_initializer = emplace_node<ExpressionNode>(new_qual_id);
								
								FLASH_LOG(Templates, Debug, "Phase 3: Successfully instantiated and updated qualifier in variable template initializer");
							}
						}
					}
				}
			}
		}
	}
	
	// Create instantiated variable declaration
	auto instantiated_var_decl = emplace_node<VariableDeclarationNode>(
		new_decl_node,
		new_initializer,
		orig_var_decl.storage_class()
	);
	// Mark as constexpr to match the template pattern
	instantiated_var_decl.as<VariableDeclarationNode>().set_is_constexpr(true);
	
	// Register the VariableDeclarationNode in symbol table (not just DeclarationNode)
	// This allows constexpr evaluation to find and evaluate the variable
	// IMPORTANT: Use insertGlobal because we might be called during function parsing
	// but we need to insert into global scope
	[[maybe_unused]] bool insert_result = gSymbolTable.insertGlobal(persistent_name, instantiated_var_decl);
	
	// Verify it's there
	auto verify = gSymbolTable.lookup(persistent_name);
	
	// Add to AST at the beginning so it gets code-generated before functions that use it
	// Insert after other global declarations but before function definitions
	ast_nodes_.insert(ast_nodes_.begin(), instantiated_var_decl);
	
	return instantiated_var_decl;
}

// Helper to instantiate a full template specialization (e.g., template<> struct Tuple<> {})
std::optional<ASTNode> Parser::instantiate_full_specialization(
	std::string_view template_name,
	const std::vector<TemplateTypeArg>& template_args,
	const ASTNode& spec_node
) {
	// Generate the instantiated class name
	std::string_view instantiated_name = get_instantiated_class_name(template_name, template_args);
	FLASH_LOG(Templates, Debug, "instantiate_full_specialization called for: ", instantiated_name);
	
	if (!spec_node.is<StructDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Full specialization is not a StructDeclarationNode");
		return std::nullopt;
	}
	
	const StructDeclarationNode& spec_struct = spec_node.as<StructDeclarationNode>();
	
	// Helper lambda to register type aliases with qualified names
	auto register_type_aliases = [&]() {
		for (const auto& type_alias : spec_struct.type_aliases()) {
			// Build the qualified name using StringBuilder
			StringHandle qualified_alias_name = StringTable::getOrInternStringHandle(StringBuilder()
				.append(instantiated_name)
				.append("::")
				.append(type_alias.alias_name));
			
			// Check if already registered
			if (gTypesByName.find(qualified_alias_name) != gTypesByName.end()) {
				continue;  // Already registered
			}
			
			// Get the type information from the alias
			const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();
			
			// Register the type alias globally with its qualified name
			auto& alias_type_info = gTypeInfo.emplace_back(
				qualified_alias_name,
				alias_type_spec.type(),
				alias_type_spec.type_index(),
				alias_type_spec.size_in_bits()
			);
			gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
			
			FLASH_LOG(Templates, Debug, "Registered type alias: ", StringTable::getStringView(qualified_alias_name), 
				" -> type=", static_cast<int>(alias_type_spec.type()), 
				", type_index=", alias_type_spec.type_index());
		}
	};
	
	// Check if we already have this instantiation
	auto existing_type = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_name));
	if (existing_type != gTypesByName.end()) {
		FLASH_LOG(Templates, Debug, "Full spec already instantiated: ", instantiated_name);
		
		// Even if the struct is already instantiated, we need to register type aliases
		// with qualified names if they haven't been registered yet
		register_type_aliases();
		
		return std::nullopt;  // Already instantiated
	}
	
	FLASH_LOG(Templates, Debug, "Instantiating full specialization: ", instantiated_name);
	
	// Create TypeInfo for the specialization
	TypeInfo& struct_type_info = add_struct_type(StringTable::getOrInternStringHandle(instantiated_name));
	
	// Store template instantiation metadata for O(1) lookup (Phase 6)
	struct_type_info.setTemplateInstantiationInfo(
		StringTable::getOrInternStringHandle(template_name),
		convertToTemplateArgInfo(template_args)
	);
	
	auto struct_info = std::make_unique<StructTypeInfo>(StringTable::getOrInternStringHandle(instantiated_name), spec_struct.default_access());
	struct_info->is_union = spec_struct.is_union();
	
	// Copy members from the specialization
	for (const auto& member_decl : spec_struct.members()) {
		const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
		
		Type member_type = type_spec.type();
		TypeIndex member_type_index = type_spec.type_index();
		size_t ptr_depth = type_spec.pointer_depth();
		
		size_t member_size;
		if (ptr_depth > 0 || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
			member_size = 8;
		} else {
			member_size = get_type_size_bits(member_type) / 8;
		}
		size_t member_alignment = get_type_alignment(member_type, member_size);
		
		// Phase 7B: Intern member name and use StringHandle overload
		StringHandle member_name_handle = decl.identifier_token().handle();
		struct_info->addMember(
			member_name_handle,
			member_type,
			member_type_index,
			member_size,
			member_alignment,
			member_decl.access,
			member_decl.default_initializer,
			type_spec.is_reference(),
			type_spec.is_rvalue_reference(),
			(type_spec.is_reference() || type_spec.is_rvalue_reference()) ? get_type_size_bits(member_type) : 0
		);
	}
	
	// Copy static members
	// Look up the specialization's StructTypeInfo to get static members
	// (The specialization should have been parsed and its TypeInfo registered already)
	auto spec_name_lookup = spec_struct.name();
	auto spec_type_it = gTypesByName.find(spec_name_lookup);
	if (spec_type_it != gTypesByName.end()) {
		const StructTypeInfo* spec_struct_info = spec_type_it->second->getStructInfo();
		if (spec_struct_info) {
			for (const auto& static_member : spec_struct_info->static_members) {
				FLASH_LOG(Templates, Debug, "Copying static member: ", static_member.getName());
				struct_info->static_members.push_back(static_member);
			}
		}
	}
	
	// Copy type aliases from the specialization
	// Type aliases need to be registered with qualified names (e.g., "MyType_bool::type")
	register_type_aliases();
	
	// Check if there's an explicit constructor - if not, we need to generate a default one
	bool has_constructor = false;
	for (const auto& mem_func : spec_struct.member_functions()) {
		if (mem_func.is_constructor) {
			has_constructor = true;
			
			// Handle constructor - it's a ConstructorDeclarationNode
			const ConstructorDeclarationNode& orig_ctor = mem_func.function_declaration.as<ConstructorDeclarationNode>();
			
			// Create a NEW ConstructorDeclarationNode with the instantiated struct name
			auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
				StringTable::getOrInternStringHandle(instantiated_name),  // Set correct parent struct name
				orig_ctor.name()    // Constructor name
			);
			
			// Copy parameters
			for (const auto& param : orig_ctor.parameter_nodes()) {
				new_ctor_ref.add_parameter_node(param);
			}
			
			// Copy member initializers
			for (const auto& [name, expr] : orig_ctor.member_initializers()) {
				new_ctor_ref.add_member_initializer(name, expr);
			}
			
			// Copy definition if present
			if (orig_ctor.get_definition().has_value()) {
				new_ctor_ref.set_definition(*orig_ctor.get_definition());
			}
			
			// Add the constructor to struct_info
			struct_info->addConstructor(new_ctor_node, mem_func.access);
			
			// Add to AST for code generation
			ast_nodes_.push_back(new_ctor_node);
		} else if (mem_func.is_destructor) {
			// Handle destructor - create new node with correct struct name
			const DestructorDeclarationNode& orig_dtor = mem_func.function_declaration.as<DestructorDeclarationNode>();
			
			auto [new_dtor_node, new_dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(
				StringTable::getOrInternStringHandle(instantiated_name),
				orig_dtor.name()
			);
			
			// Copy definition if present
			if (orig_dtor.get_definition().has_value()) {
				new_dtor_ref.set_definition(*orig_dtor.get_definition());
			}
			
			struct_info->addDestructor(new_dtor_node, mem_func.access, mem_func.is_virtual);
			ast_nodes_.push_back(new_dtor_node);
		} else {
			const FunctionDeclarationNode& orig_func = mem_func.function_declaration.as<FunctionDeclarationNode>();
			
			// Create a NEW FunctionDeclarationNode with the instantiated struct name
			auto new_func_node = emplace_node<FunctionDeclarationNode>(
				const_cast<DeclarationNode&>(orig_func.decl_node()),
				instantiated_name
			);
			
			// Copy all parameters and definition
			FunctionDeclarationNode& new_func = new_func_node.as<FunctionDeclarationNode>();
			for (const auto& param : orig_func.parameter_nodes()) {
				new_func.add_parameter_node(param);
			}
			if (orig_func.get_definition().has_value()) {
				new_func.set_definition(*orig_func.get_definition());
			}
			
			// Phase 7B: Intern function name and use StringHandle overload
			StringHandle func_name_handle = orig_func.decl_node().identifier_token().handle();
			struct_info->addMemberFunction(
				func_name_handle,
				new_func_node,
				mem_func.access,
				mem_func.is_virtual,
				mem_func.is_pure_virtual,
				mem_func.is_override,
				mem_func.is_final
			);
			
			// Add to AST for code generation
			ast_nodes_.push_back(new_func_node);
		}
	}
	
	// If no constructor was defined, we should synthesize a default one
	// For now, mark that we need one and it will be generated in codegen
	struct_info->needs_default_constructor = !has_constructor;
	FLASH_LOG(Templates, Debug, "Full spec has constructor: ", has_constructor ? "yes" : "no, needs default");
	
	struct_type_info.setStructInfo(std::move(struct_info));
	if (struct_type_info.getStructInfo()) {
		struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
	}
	
	return std::nullopt;  // Return nullopt since we don't need to add anything to AST
}

// Helper function to substitute non-type template parameters in initializers
// Extracted from try_instantiate_class_template to reduce function size
std::optional<ASTNode> Parser::substitute_nontype_template_param(
	std::string_view param_name,
	const std::vector<TemplateTypeArg>& args,
	const std::vector<ASTNode>& params) {
	for (size_t i = 0; i < params.size(); ++i) {
		const TemplateParameterNode& tparam = params[i].as<TemplateParameterNode>();
		if (tparam.name() == param_name && tparam.kind() == TemplateParameterKind::NonType) {
			if (i < args.size() && args[i].is_value) {
				int64_t val = args[i].value;
				Type val_type = args[i].base_type;
				StringBuilder value_str;
				value_str.append(val);
				std::string_view value_view = value_str.commit();
				Token num_token(Token::Type::Literal, value_view, 0, 0, 0);
				return emplace_node<ExpressionNode>(
					NumericLiteralNode(num_token, 
					                   static_cast<unsigned long long>(val), 
					                   val_type, 
					                   TypeQualifier::None, 
					                   get_type_size_bits(val_type))
				);
			}
		}
	}
	return std::nullopt;
}

// Helper function to fill in default template arguments before pattern matching
// This is critical for SFINAE patterns like void_t
std::optional<ASTNode> Parser::try_instantiate_class_template(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args, bool force_eager) {
	PROFILE_TEMPLATE_INSTANTIATION(std::string(template_name));
	
	// Add iteration limit to prevent infinite loops during template instantiation
	static thread_local int iteration_count = 0;
	static thread_local const int MAX_ITERATIONS = 10000;  // Safety limit
	
	iteration_count++;
	if (iteration_count > MAX_ITERATIONS) {
		FLASH_LOG(Templates, Error, "Template instantiation iteration limit exceeded (", MAX_ITERATIONS, ")! Possible infinite loop.");
		FLASH_LOG(Templates, Error, "Last template: '", template_name, "' with ", template_args.size(), " args");
		iteration_count = 0;  // Reset for next compilation
		return std::nullopt;
	}
	
	// Log entry to help debug which call sites are causing issues
	FLASH_LOG(Templates, Debug, "try_instantiate_class_template: template='", template_name, 
	          "', args=", template_args.size(), ", force_eager=", force_eager);
	
	// Early check: verify this is actually a class template before proceeding
	// This prevents errors when function templates like 'declval' are passed to this function
	{
		auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
		if (template_opt.has_value() && !template_opt->is<TemplateClassDeclarationNode>()) {
			// This is not a class template (probably a function template) - skip silently
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping try_instantiate_class_template for non-class template '{}'", template_name);
			return std::nullopt;
		}
	}
	
	// Check if any template arguments are dependent (contain template parameters)
	// If so, we cannot instantiate the template yet - it's a dependent type
	for (const auto& arg : template_args) {
		if (arg.is_dependent) {
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping instantiation of {} - template arguments are dependent", template_name);
			// Return success (nullopt) but don't actually instantiate
			// The type will be resolved during actual template instantiation
			return std::nullopt;
		}
	}
	
	// V2 Cache: Check TypeIndex-based instantiation cache for O(1) lookup
	// This uses TypeIndex instead of string keys to avoid ambiguity with type names containing underscores
	StringHandle template_name_handle = StringTable::getOrInternStringHandle(template_name);
	auto v2_key = FlashCpp::makeInstantiationKeyV2(template_name_handle, template_args);
	auto v2_cached = gTemplateRegistry.getInstantiationV2(v2_key);
	if (v2_cached.has_value()) {
		FLASH_LOG_FORMAT(Templates, Debug, "V2 cache hit for '{}' with {} args", template_name, template_args.size());
		return std::nullopt;  // Already instantiated - return nullopt to indicate success
	}
	
	// Build InstantiationKey for cycle detection
	// Note: Caching is handled by gTypesByName check later in the function
	FlashCpp::InstantiationKey inst_key = FlashCpp::InstantiationQueue::makeKey(template_name, template_args);
	
	// Create RAII guard for in-progress tracking (handles cycle detection)
	auto in_progress_guard = FlashCpp::gInstantiationQueue.makeInProgressGuard(inst_key);
	if (!in_progress_guard.isActive()) {
		FLASH_LOG_FORMAT(Templates, Warning, "InstantiationQueue: cycle detected for '{}'", template_name);
		// Don't fail - some recursive patterns are valid (e.g., CRTP)
		// Proceed without in_progress tracking
	}
	
	// Determine if we should use lazy instantiation early in the function
	// This flag controls whether static members and member functions are instantiated eagerly or on-demand
	// Can be overridden by force_eager parameter (used for explicit instantiation)
	bool use_lazy_instantiation = context_.isLazyTemplateInstantiationEnabled() && !force_eager;
	
	// Helper lambda delegates to extracted member function for non-type template parameter substitution
	auto substitute_template_param_in_initializer = [this](
		std::string_view param_name,
		const std::vector<TemplateTypeArg>& args,
		const std::vector<ASTNode>& params) -> std::optional<ASTNode> {
		return substitute_nontype_template_param(param_name, args, params);
	};
	
	// Helper lambda to substitute template parameters in member default initializers
	// Handles both TemplateParameterReferenceNode and IdentifierNode
	auto substitute_default_initializer = [&](
		const std::optional<ASTNode>& default_init,
		const std::vector<TemplateTypeArg>& args,
		const std::vector<ASTNode>& params) -> std::optional<ASTNode> {
		if (!default_init.has_value()) {
			return std::nullopt;
		}
		
		const ASTNode& init_node = default_init.value();
		if (!init_node.is<ExpressionNode>()) {
			return default_init;  // Return as-is if not an expression
		}
		
		const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
		std::string_view param_name_to_substitute;
		
		// Check if the initializer is a template parameter reference or identifier
		if (std::holds_alternative<TemplateParameterReferenceNode>(init_expr)) {
			const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(init_expr);
			param_name_to_substitute = tparam_ref.param_name().view();
		} else if (std::holds_alternative<IdentifierNode>(init_expr)) {
			const IdentifierNode& ident = std::get<IdentifierNode>(init_expr);
			param_name_to_substitute = ident.name();
		}
		
		// Try to substitute if we found a parameter name
		if (!param_name_to_substitute.empty()) {
			auto substituted = substitute_template_param_in_initializer(param_name_to_substitute, args, params);
			if (substituted.has_value()) {
				return substituted;
			}
		}
		
		return default_init;  // Return original if no substitution was performed
	};
	
	// Helper lambda to evaluate a fold expression with concrete pack values and create an AST node
	// Uses ConstExpr::evaluate_fold_expression for the actual computation
	auto evaluate_fold_expression = [this](std::string_view op, const std::vector<int64_t>& pack_values) -> std::optional<ASTNode> {
		auto result = ConstExpr::evaluate_fold_expression(op, pack_values);
		if (!result.has_value()) {
			return std::nullopt;
		}
		
		FLASH_LOG(Templates, Debug, "Evaluated fold expression to: ", *result);
		
		// Create a bool literal for && and ||, numeric for others
		if (op == "&&" || op == "||") {
			Token bool_token(Token::Type::Keyword, *result ? "true"sv : "false"sv, 0, 0, 0);
			return emplace_node<ExpressionNode>(
				BoolLiteralNode(bool_token, *result != 0)
			);
		} else {
			std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(*result)).commit();
			Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
			return emplace_node<ExpressionNode>(
				NumericLiteralNode(num_token, static_cast<unsigned long long>(*result), Type::Int, TypeQualifier::None, 64)
			);
		}
	};
	
	// Helper lambda to resolve a dependent qualified type (like wrapper_void::type)
	// to its actual type after substituting template arguments.
	// Returns a resolved TemplateTypeArg if successful, nullopt otherwise.
	auto resolve_dependent_qualified_type = [&](
		std::string_view type_name,
		const TemplateTypeArg& actual_arg) -> std::optional<TemplateTypeArg> {
		
		// Check if this is a qualified type (contains ::)
		auto double_colon_pos = type_name.find("::");
		if (double_colon_pos == std::string_view::npos) {
			return std::nullopt;
		}
		
		// Extract base template name and member name
		std::string_view base_part = type_name.substr(0, double_colon_pos);
		std::string_view member_name = type_name.substr(double_colon_pos + 2);
		
		FLASH_LOG(Templates, Debug, "Resolving dependent type: ", type_name,
		          " -> base='", base_part, "', member='", member_name, "'");
		
		// Check if base_part contains a placeholder using TypeInfo-based detection
		auto [is_dependent_placeholder, template_base_name] = isDependentTemplatePlaceholder(base_part);
		if (!is_dependent_placeholder) {
			return std::nullopt;
		}
		
		// Build the instantiated template name using hash-based naming
		std::string_view instantiated_base_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{actual_arg});
		
		// Try to instantiate the template if not already done
		std::vector<TemplateTypeArg> base_template_args = { actual_arg };
		try_instantiate_class_template(template_base_name, base_template_args);
		
		// Build the full qualified name (e.g., "wrapper_int::type")
		StringBuilder qualified_name_builder;
		qualified_name_builder.append(instantiated_base_name)
		                     .append("::")
		                     .append(member_name);
		std::string_view qualified_name = qualified_name_builder.commit();
		
		FLASH_LOG(Templates, Debug, "Looking up resolved type: ", qualified_name);
		
		// Look up the member type
		auto resolved_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_name));
		if (resolved_type_it == gTypesByName.end()) {
			return std::nullopt;
		}
		
		const TypeInfo* resolved_type_info = resolved_type_it->second;
		
		// Get the resolved type, following aliases if needed
		Type resolved_base_type = resolved_type_info->type_;
		TypeIndex resolved_type_index = resolved_type_info->type_index_;
		
		// Check if this is an alias to a concrete type
		if (resolved_type_info->type_ == Type::UserDefined && 
		    resolved_type_index != resolved_type_info->type_index_ && 
		    resolved_type_index < gTypeInfo.size()) {
			// Follow the alias
			const TypeInfo& aliased_type = gTypeInfo[resolved_type_index];
			resolved_base_type = aliased_type.type_;
			resolved_type_index = aliased_type.type_index_;
		}
		
		TemplateTypeArg resolved_arg;
		resolved_arg.base_type = resolved_base_type;
		resolved_arg.type_index = resolved_type_index;
		
		FLASH_LOG(Templates, Debug, "Resolved dependent type to: type=", 
		          static_cast<int>(resolved_base_type), ", index=", resolved_type_index);
		
		return resolved_arg;
	};
	
	// 1) Full/Exact specialization lookup
	// If there is an exact specialization registered for (template_name, template_args),
	// it always wins over partial specializations and the primary template.
	// Note: This also handles empty template args (e.g., template<> struct Tuple<> {})
	{
		auto exact_spec = gTemplateRegistry.lookupExactSpecialization(template_name, template_args);
		if (exact_spec.has_value()) {
			FLASH_LOG(Templates, Debug, "Found exact specialization for ", template_name, " with ", template_args.size(), " args");
			// Instantiate the exact specialization
			return instantiate_full_specialization(template_name, template_args, *exact_spec);
		}
	}
	
	// Generate the instantiated class name first
	auto instantiated_name = StringTable::getOrInternStringHandle(get_instantiated_class_name(template_name, template_args));

	// Check if we already have this instantiation
	auto existing_type = gTypesByName.find(instantiated_name);
	if (existing_type != gTypesByName.end()) {
		PROFILE_TEMPLATE_CACHE_HIT(std::string(template_name));
		return std::nullopt;
	}
	PROFILE_TEMPLATE_CACHE_MISS(std::string(template_name));
	
	// Fill in default template arguments BEFORE pattern matching (void_t SFINAE fix)
	// This is critical for patterns like: template<typename T, typename = void> struct has_type;
	// with specialization: template<typename T> struct has_type<T, void_t<typename T::type>>;
	// When has_type<WithType> is instantiated, we need to fill in the default (void) before pattern matching.
	std::vector<TemplateTypeArg> filled_args_for_pattern_match = template_args;
	{
		auto primary_template_opt = gTemplateRegistry.lookupTemplate(template_name);
		if (primary_template_opt.has_value() && primary_template_opt->is<TemplateClassDeclarationNode>()) {
			const TemplateClassDeclarationNode& primary_template = primary_template_opt->as<TemplateClassDeclarationNode>();
			const std::vector<ASTNode>& primary_params = primary_template.template_parameters();
			
			// Fill in defaults for missing arguments
			for (size_t i = filled_args_for_pattern_match.size(); i < primary_params.size(); ++i) {
				if (!primary_params[i].is<TemplateParameterNode>()) continue;
				
				const TemplateParameterNode& param = primary_params[i].as<TemplateParameterNode>();
				
				// Skip variadic parameters
				if (param.is_variadic()) continue;
				
				// Check if parameter has a default
				if (!param.has_default()) break;  // No default = can't fill in
				
				// Get the default value
				const ASTNode& default_node = param.default_value();
				
				if (param.kind() == TemplateParameterKind::Type && default_node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
					
					// Simple case: default is void
					if (default_type.type() == Type::Void) {
						TemplateTypeArg void_arg;
						void_arg.base_type = Type::Void;
						void_arg.type_index = 0;
						filled_args_for_pattern_match.push_back(void_arg);
						FLASH_LOG(Templates, Debug, "Filled in default argument for param ", i, ": void");
						continue;
					}
					
					// Check if default is an alias template like void_t
					// by looking at the token value
					Token default_token = default_type.token();
					std::string_view alias_name = default_token.value();
					
					// Look up if this is an alias template
					auto alias_opt = gTemplateRegistry.lookup_alias_template(alias_name);
					if (alias_opt.has_value()) {
						const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
						
						// Check if the alias target type is void (like void_t)
						const ASTNode& target_type = alias_node.target_type();
						if (target_type.is<TypeSpecifierNode>()) {
							const TypeSpecifierNode& alias_type_spec = target_type.as<TypeSpecifierNode>();
							if (alias_type_spec.type() == Type::Void) {
								// void_t-like alias: fill in void here, SFINAE check happens in pattern matching
								TemplateTypeArg void_arg;
								void_arg.base_type = Type::Void;
								void_arg.type_index = 0;
								filled_args_for_pattern_match.push_back(void_arg);
								FLASH_LOG(Templates, Debug, "Filled in void_t alias default for param ", i, ": void");
								continue;
							}
						}
					}
					
					// Check if this is a dependent qualified type (like wrapper<T>::type)
					// that needs resolution based on already-filled template arguments
					if (default_type.type() == Type::UserDefined && default_type.type_index() > 0 && 
					    default_type.type_index() < gTypeInfo.size()) {
						const TypeInfo& default_type_info = gTypeInfo[default_type.type_index()];
						std::string_view default_type_name = StringTable::getStringView(default_type_info.name());
						
						// Try to resolve using each filled argument
						for (size_t arg_idx = 0; arg_idx < filled_args_for_pattern_match.size(); ++arg_idx) {
							auto resolved = resolve_dependent_qualified_type(default_type_name, filled_args_for_pattern_match[arg_idx]);
							if (resolved.has_value()) {
								filled_args_for_pattern_match.push_back(*resolved);
								goto next_param;
							}
						}
					}
					
					// For other default types, use the type as-is
					filled_args_for_pattern_match.push_back(TemplateTypeArg(default_type));
					FLASH_LOG(Templates, Debug, "Filled in default type argument for param ", i);
					
					next_param:;
				} else if (param.kind() == TemplateParameterKind::NonType && default_node.is<ExpressionNode>()) {
					// Handle non-type template parameter defaults like is_arithmetic<T>::value
					const ExpressionNode& expr = default_node.as<ExpressionNode>();
					
					if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
						const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(expr);
						
						// Handle dependent static member access like is_arithmetic_void::value
						if (!qual_id.namespace_handle().isGlobal()) {
							std::string_view type_name = gNamespaceRegistry.getName(qual_id.namespace_handle());
							std::string_view member_name = qual_id.name();
							
							// Check for dependent placeholder using TypeInfo-based detection
							auto [is_dependent_placeholder, template_base_name] = isDependentTemplatePlaceholder(type_name);
							if (is_dependent_placeholder && !filled_args_for_pattern_match.empty()) {
								// Build the instantiated template name using hash-based naming
								std::string_view inst_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{filled_args_for_pattern_match[0]});
								
								FLASH_LOG(Templates, Debug, "Resolving dependent qualified identifier (pattern match): ", 
								          type_name, "::", member_name, " -> ", inst_name, "::", member_name);
								
								// Try to instantiate the template
								try_instantiate_class_template(template_base_name, std::vector<TemplateTypeArg>{filled_args_for_pattern_match[0]});
								
								// Look up the instantiated type
								auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
								if (type_it != gTypesByName.end()) {
									const TypeInfo* type_info = type_it->second;
									if (type_info->getStructInfo()) {
										const StructTypeInfo* struct_info = type_info->getStructInfo();
										// Find the static member
										for (const auto& static_member : struct_info->static_members) {
											if (StringTable::getStringView(static_member.getName()) == member_name) {
												// Evaluate the static member's initializer
												if (static_member.initializer.has_value()) {
													const ASTNode& init_node = *static_member.initializer;
													if (init_node.is<ExpressionNode>()) {
														const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
														if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
															bool val = std::get<BoolLiteralNode>(init_expr).value();
															TemplateTypeArg arg(val ? 1LL : 0LL);
															filled_args_for_pattern_match.push_back(arg);
															FLASH_LOG(Templates, Debug, "Resolved static member '", member_name, "' to ", val);
														} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
															const NumericLiteralNode& lit = std::get<NumericLiteralNode>(init_expr);
															const auto& val = lit.value();
															if (std::holds_alternative<unsigned long long>(val)) {
																TemplateTypeArg arg(static_cast<int64_t>(std::get<unsigned long long>(val)));
																filled_args_for_pattern_match.push_back(arg);
															}
														}
													}
												}
												break;
											}
										}
									}
								}
							}
						}
					} else if (std::holds_alternative<NumericLiteralNode>(expr)) {
						const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
						const auto& val = lit.value();
						if (std::holds_alternative<unsigned long long>(val)) {
							filled_args_for_pattern_match.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
						}
					} else if (std::holds_alternative<BoolLiteralNode>(expr)) {
						const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr);
						filled_args_for_pattern_match.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL));
					} else if (std::holds_alternative<SizeofExprNode>(expr)) {
						// Handle sizeof(T) as a default value
						const SizeofExprNode& sizeof_node = std::get<SizeofExprNode>(expr);
						if (sizeof_node.is_type()) {
							// sizeof(type) - evaluate the type size
							const ASTNode& type_node = sizeof_node.type_or_expr();
							if (type_node.is<TypeSpecifierNode>()) {
								TypeSpecifierNode type_spec = type_node.as<TypeSpecifierNode>();
								
								// Check if this is a template parameter that needs substitution
								bool found_substitution = false;
								std::string_view type_name;
								
								// Try to get the type name from the token first (most reliable for template params)
								if (type_spec.token().type() == Token::Type::Identifier) {
									type_name = type_spec.token().value();
								} else if (type_spec.type() == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
									// Fall back to gTypeInfo for fully resolved types
									const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
									type_name = StringTable::getStringView(type_info.name());
								}
								
								if (!type_name.empty()) {
									// Check if this is one of the template parameters we've already filled
									for (size_t j = 0; j < primary_params.size() && j < filled_args_for_pattern_match.size(); ++j) {
										if (primary_params[j].is<TemplateParameterNode>()) {
											const TemplateParameterNode& prev_param = primary_params[j].as<TemplateParameterNode>();
											if (prev_param.name() == type_name) {
												// Found the matching template parameter - use its filled value
												const TemplateTypeArg& filled_arg = filled_args_for_pattern_match[j];
												if (filled_arg.base_type != Type::Invalid) {
													// Calculate the size of the filled type
													int size_in_bytes = get_type_size_bits(filled_arg.base_type) / 8;
													if (size_in_bytes == 0)
													{
														switch (filled_arg.base_type) {
															case Type::Struct:
															case Type::UserDefined:
																// For struct types, we need to look up the size from TypeInfo
																if (filled_arg.type_index < gTypeInfo.size()) {
																	const TypeInfo& ti = gTypeInfo[filled_arg.type_index];
																	if (ti.isStruct()) {
																		const StructTypeInfo* si = ti.getStructInfo();
																		if (si) {
																			size_in_bytes = si->total_size;
																		}
																	}
																}
																break;
															default:
																size_in_bytes = 8; // Default to 64 bits if unknown
																break;
														}
													}

													if (size_in_bytes > 0) {
														filled_args_for_pattern_match.push_back(TemplateTypeArg(static_cast<int64_t>(size_in_bytes)));
														FLASH_LOG(Templates, Debug, "Filled in sizeof(", type_name, ") default: ", size_in_bytes, " bytes");
														found_substitution = true;
														break;
													}
												}
											}
										}
									}
								}
								
								if (!found_substitution) {
									// Direct type (not a template parameter)
									int size_in_bits = type_spec.size_in_bits();
									int size_in_bytes = (size_in_bits + 7) / 8;  // Round up to bytes
									filled_args_for_pattern_match.push_back(TemplateTypeArg(static_cast<int64_t>(size_in_bytes)));
									FLASH_LOG(Templates, Debug, "Filled in sizeof default: ", size_in_bytes, " bytes");
								}
							}
						}
					}
				}
			}
		}
	}
	
	// Regenerate instantiated name with filled-in defaults
	// This is needed when defaults are dependent types that get resolved (e.g., typename wrapper<T>::type -> int)
	if (filled_args_for_pattern_match.size() > template_args.size()) {
		instantiated_name = StringTable::getOrInternStringHandle(get_instantiated_class_name(template_name, filled_args_for_pattern_match));
		FLASH_LOG(Templates, Debug, "Regenerated instantiated name with defaults: ", StringTable::getStringView(instantiated_name));
		
		// Check again if we already have this instantiation (with filled-in defaults)
		auto existing_type_with_defaults = gTypesByName.find(instantiated_name);
		if (existing_type_with_defaults != gTypesByName.end()) {
			FLASH_LOG(Templates, Debug, "Found existing instantiation with filled-in defaults");
			return std::nullopt;
		}
	}
	
	// First, check if there's an exact specialization match
	// Try to match a specialization pattern and get the substitution mapping
	{
		PROFILE_TEMPLATE_SPECIALIZATION_MATCH();
		std::unordered_map<std::string, TemplateTypeArg> param_substitutions;
		FLASH_LOG(Templates, Debug, "Looking for pattern match for ", template_name, " with ", filled_args_for_pattern_match.size(), " args (after default fill-in)");
		auto pattern_match_opt = gTemplateRegistry.matchSpecializationPattern(template_name, filled_args_for_pattern_match);
		if (pattern_match_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "Found pattern match!");
			// Found a matching pattern - we need to instantiate it with concrete types
			const ASTNode& pattern_node = *pattern_match_opt;
		
		// Handle both StructDeclarationNode (top-level partial specialization) and
		// TemplateClassDeclarationNode (member template partial specialization)
		const StructDeclarationNode* pattern_struct_ptr = nullptr;
		if (pattern_node.is<StructDeclarationNode>()) {
			pattern_struct_ptr = &pattern_node.as<StructDeclarationNode>();
		} else if (pattern_node.is<TemplateClassDeclarationNode>()) {
			// Member template partial specialization - extract the inner struct
			pattern_struct_ptr = &pattern_node.as<TemplateClassDeclarationNode>().class_decl_node();
		} else {
			FLASH_LOG(Templates, Error, "Pattern node is not a StructDeclarationNode or TemplateClassDeclarationNode");
			return std::nullopt;
		}
		
		const StructDeclarationNode& pattern_struct = *pattern_struct_ptr;
		FLASH_LOG(Templates, Debug, "Pattern struct name: ", pattern_struct.name());
		
		// Register the mapping from instantiated name to pattern name
		// This allows member alias lookup to find the correct specialization
		gTemplateRegistry.register_instantiation_pattern(instantiated_name, pattern_struct.name());
		
		// Get template parameters from the pattern (partial specialization), NOT the primary template
		// The pattern stores its own template parameters (e.g., <typename First, typename... Rest>)
		std::vector<ASTNode> pattern_template_params;
		auto patterns_it = gTemplateRegistry.specialization_patterns_.find(std::string(template_name));
		if (patterns_it != gTemplateRegistry.specialization_patterns_.end()) {
			// Find the matching pattern to get its template params
			for (const auto& pattern : patterns_it->second) {
				// Handle both StructDeclarationNode and TemplateClassDeclarationNode patterns
				const StructDeclarationNode* spec_struct_ptr = nullptr;
				if (pattern.specialized_node.is<StructDeclarationNode>()) {
					spec_struct_ptr = &pattern.specialized_node.as<StructDeclarationNode>();
				} else if (pattern.specialized_node.is<TemplateClassDeclarationNode>()) {
					spec_struct_ptr = &pattern.specialized_node.as<TemplateClassDeclarationNode>().class_decl_node();
				}
				if (spec_struct_ptr && spec_struct_ptr == &pattern_struct) {
					pattern_template_params = pattern.template_params;
					break;
				}
			}
		}
		
		// Fall back to primary template params if pattern params not found
		if (pattern_template_params.empty()) {
			auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
			if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
				const TemplateClassDeclarationNode& primary_template = template_opt->as<TemplateClassDeclarationNode>();
				pattern_template_params = std::vector<ASTNode>(primary_template.template_parameters().begin(),
				                                               primary_template.template_parameters().end());
			}
		}
		const std::vector<ASTNode>& template_params = pattern_template_params;
		
		// Create a new struct with the instantiated name
		// Copy members from the pattern, substituting template parameters
		// For now, if members use template parameters, we substitute them
		
		// Create struct type info first
		TypeInfo& struct_type_info = add_struct_type(instantiated_name);
		
		// Store template instantiation metadata for O(1) lookup (Phase 6)
		struct_type_info.setTemplateInstantiationInfo(
			StringTable::getOrInternStringHandle(template_name),
			convertToTemplateArgInfo(template_args)
		);
		
		auto struct_info = std::make_unique<StructTypeInfo>(instantiated_name, pattern_struct.default_access());
		struct_info->is_union = pattern_struct.is_union();
		
		// Handle base classes from the pattern
		// Base classes need to be instantiated with concrete template arguments
		FLASH_LOG(Templates, Debug, "Pattern has ", pattern_struct.base_classes().size(), " base classes");
		for (const auto& pattern_base : pattern_struct.base_classes()) {
			// IMPORTANT: pattern_base.name might be a string_view pointing to freed memory!
			// Convert to string immediately to avoid issues
			std::string base_name_str;
			try {
				base_name_str = std::string(pattern_base.name);  // Convert to string to avoid string_view issues
			} catch (...) {
				FLASH_LOG(Templates, Error, "Failed to convert base class name to string!");
				continue;
			}
			
			// Check if base_name_str is valid (not empty and printable)
			if (base_name_str.empty()) {
				FLASH_LOG(Templates, Error, "Base class name is empty!");
				continue;
			}
			
			FLASH_LOG(Templates, Debug, "Processing base class: ", base_name_str);
			
			// NEW: Check if the base class IS a template parameter name (like T1, T2)
			// If so, substitute it with the corresponding template argument
			// This handles patterns like: template<typename T1, typename T2> struct __or_<T1, T2> : T1 { };
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				if (template_params[i].is<TemplateParameterNode>()) {
					const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
					if (param.name() == base_name_str) {
						// The base class is a template parameter - substitute with the corresponding argument
						const TemplateTypeArg& arg = template_args[i];
						
						// Get the concrete type name for this argument
						std::string substituted_name = arg.toString();
						FLASH_LOG(Templates, Debug, "Substituting base class template parameter '", base_name_str, 
						          "' with '", substituted_name, "'");
						base_name_str = substituted_name;
						break;
					}
				}
			}
			
			// WORKAROUND: If the base class name ends with "_unknown", it was instantiated
			// during pattern parsing with template parameters. We need to re-instantiate
			// it with the concrete template arguments.
			if (base_name_str.ends_with("_unknown")) {
				// Extract the template name (before "_unknown")
				size_t pos = base_name_str.find("_unknown");
				std::string base_template_name = base_name_str.substr(0, pos);
				
				// For partial specialization like Tuple<First, Rest...> : Tuple<Rest...>
				// The base class uses Rest... (the variadic pack), which corresponds to
				// all template args EXCEPT the first one (First)
				
				// Check if this pattern uses a variadic pack for the base
				bool base_uses_variadic_pack = false;
				size_t first_variadic_index = template_params.size();
				for (size_t i = 0; i < template_params.size(); ++i) {
					if (template_params[i].is<TemplateParameterNode>() &&
					    template_params[i].as<TemplateParameterNode>().is_variadic()) {
						first_variadic_index = i;
						base_uses_variadic_pack = true;
						break;
					}
				}
				
				std::vector<TemplateTypeArg> base_template_args;
				if (base_uses_variadic_pack && template_args.size() > first_variadic_index) {
					// Skip the non-variadic parameters (First) and use Rest...
					// For Tuple<int>: template_args = [int], first_variadic_index = 1
					// So base_template_args = [] (empty)
					// For Tuple<int, float>: template_args = [int, float], first_variadic_index = 1
					// So base_template_args = [float]
					for (size_t i = first_variadic_index; i < template_args.size(); ++i) {
						base_template_args.push_back(template_args[i]);
					}
				} else if (base_uses_variadic_pack) {
					// Empty variadic pack - base_template_args stays empty
					// For Tuple<int>: template_args = [int], first_variadic_index = 1
					// base_template_args = [] (Tuple<>)
				} else {
					// Fallback: assume single template parameter for non-variadic cases
					if (!template_args.empty()) {
						base_template_args.push_back(template_args[0]);
					}
				}
				
				FLASH_LOG(Templates, Debug, "Base class instantiation: ", base_template_name, " with ", base_template_args.size(), " args");
				
				// Instantiate the base template (may be empty specialization like Tuple<>)
				auto base_instantiated = try_instantiate_class_template(base_template_name, base_template_args);
				if (base_instantiated.has_value()) {
					// Add the base class instantiation to the AST so its constructors get generated
					ast_nodes_.push_back(*base_instantiated);
				}
				
				// Get the instantiated name
				base_name_str = std::string(get_instantiated_class_name(base_template_name, base_template_args));
				FLASH_LOG(Templates, Debug, "Base class resolved to: ", base_name_str);
			}
			
			// Convert string_view to permanent string using StringTable
			StringHandle base_class_handle = StringTable::getOrInternStringHandle(base_name_str);
			std::string_view base_class_name = StringTable::getStringView(base_class_handle);
			
			// Look up the base class type
			auto base_type_it = gTypesByName.find(base_class_handle);
			if (base_type_it != gTypesByName.end()) {
				const TypeInfo* base_type_info = base_type_it->second;
				struct_info->addBaseClass(base_class_name, base_type_info->type_index_, pattern_base.access, pattern_base.is_virtual);
			} else {
				FLASH_LOG(Templates, Error, "Base class ", base_class_name, " not found in gTypesByName");
			}
		}
		
		// Copy members from pattern
		FLASH_LOG(Templates, Debug, "Pattern struct '", pattern_struct.name(), "' has ", pattern_struct.members().size(), " members");
		for (const auto& member_decl : pattern_struct.members()) {
			const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
			FLASH_LOG(Templates, Debug, "Copying member: ", decl.identifier_token().value(), 
			          " has_initializer=", member_decl.default_initializer.has_value());
			const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
			
			// For pattern specializations, member types need substitution!
			// The pattern has T* (Type::UserDefined with ptr_depth=1)
			// We need to substitute T with the concrete type (e.g., int)
			
			// For pattern specializations, member types need substitution!
			// Use substitute_template_parameter to properly match template parameters by name
			auto [member_type, member_type_index] = substitute_template_parameter(
				type_spec, template_params, template_args);
			size_t ptr_depth = type_spec.pointer_depth();
			
			// Calculate member size accounting for pointer depth
			size_t member_size;
			if (ptr_depth > 0 || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
				// Pointers and references are always 8 bytes (64-bit)
				member_size = 8;
			} else if (member_type == Type::Struct && member_type_index != 0) {
				// For struct types, look up the actual size in gTypeInfo
				const TypeInfo* member_struct_info = nullptr;
				for (const auto& ti : gTypeInfo) {
					if (ti.type_index_ == member_type_index) {
						member_struct_info = &ti;
						break;
					}
				}
				if (member_struct_info && member_struct_info->getStructInfo()) {
					member_size = member_struct_info->getStructInfo()->total_size;
				} else {
					member_size = get_type_size_bits(member_type) / 8;
				}
			} else {
				member_size = get_type_size_bits(member_type) / 8;
			}
			// Calculate member alignment
			// For pointers and references, use 8-byte alignment (pointer alignment on x64)
			size_t member_alignment;
			if (ptr_depth > 0 || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
				member_alignment = 8;  // Pointer/reference alignment on x64
			} else if (member_type == Type::Struct && member_type_index != 0) {
				// For struct types, look up the actual alignment from gTypeInfo
				const TypeInfo* member_struct_info = nullptr;
				for (const auto& ti : gTypeInfo) {
					if (ti.type_index_ == member_type_index) {
						member_struct_info = &ti;
						break;
					}
				}
				if (member_struct_info && member_struct_info->getStructInfo()) {
					member_alignment = member_struct_info->getStructInfo()->alignment;
				} else {
					member_alignment = get_type_alignment(member_type, member_size);
				}
			} else {
				member_alignment = get_type_alignment(member_type, member_size);
			}
			
			bool is_ref_member = type_spec.is_reference();
			bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
			
			// Substitute template parameters in default member initializers
			std::optional<ASTNode> substituted_default_initializer = substitute_default_initializer(
				member_decl.default_initializer, template_args, template_params);
			
			// Phase 7B: Intern member name and use StringHandle overload
			StringHandle member_name_handle = decl.identifier_token().handle();
			struct_info->addMember(
				member_name_handle,
				member_type,
				member_type_index,
				member_size,
				member_alignment,
				member_decl.access,
				substituted_default_initializer,
				is_ref_member,
				is_rvalue_ref_member,
				(is_ref_member || is_rvalue_ref_member) ? get_type_size_bits(member_type) : 0
			);
		}
		
		// Copy member functions from pattern
		for (const StructMemberFunctionDecl& mem_func : pattern_struct.member_functions()) {
			if (mem_func.is_constructor) {
				// Handle constructor - it's a ConstructorDeclarationNode, not FunctionDeclarationNode
				struct_info->addConstructor(
					mem_func.function_declaration,
					mem_func.access
				);
			} else if (mem_func.is_destructor) {
				// Handle destructor
				struct_info->addDestructor(
					mem_func.function_declaration,
					mem_func.access,
					mem_func.is_virtual
				);
			} else {
				const FunctionDeclarationNode& orig_func = mem_func.function_declaration.as<FunctionDeclarationNode>();
				DeclarationNode& orig_decl = const_cast<DeclarationNode&>(orig_func.decl_node());
				
				// Substitute return type if it uses a template parameter
				// For partial specializations like Container<T*>, the return type T* needs substitution
				// The pattern has Type::UserDefined for T, which needs to be replaced with the concrete type
				const TypeSpecifierNode& orig_return_type = orig_decl.type_node().as<TypeSpecifierNode>();
				
				Type substituted_return_type = orig_return_type.type();
				TypeIndex substituted_return_type_index = orig_return_type.type_index();
				
				// Check if return type needs substitution (same logic as struct members)
				bool needs_substitution = (substituted_return_type == Type::UserDefined);
				if (needs_substitution && !template_args.empty()) {
					// First, check if this return type refers to a type alias defined in this struct
					// (e.g., for conversion operators like "operator value_type()" where "using value_type = T;")
					bool found_type_alias = false;
					std::string_view return_type_name = orig_return_type.token().value();
					
					for (const auto& type_alias : pattern_struct.type_aliases()) {
						StringHandle alias_name = type_alias.alias_name;
						if (StringTable::getStringView(alias_name) == return_type_name) {
							// Found a type alias that matches the return type name
							// Get what the alias resolves to
							const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();
							
							// If the alias resolves to UserDefined (a template parameter), substitute with concrete type
							if (alias_type_spec.type() == Type::UserDefined && !template_args.empty()) {
								// The alias refers to a template parameter - substitute with the first template arg
								// (This handles cases like "using value_type = T;" where T is template param 0)
								substituted_return_type = template_args[0].base_type;
								substituted_return_type_index = template_args[0].type_index;
								found_type_alias = true;
								FLASH_LOG(Templates, Debug, "Resolved type alias '", return_type_name, 
									"' in return type to type=", static_cast<int>(substituted_return_type));
							} else {
								// The alias resolves to a concrete type
								substituted_return_type = alias_type_spec.type();
								substituted_return_type_index = alias_type_spec.type_index();
								found_type_alias = true;
							}
							break;
						}
					}
					
					// If not a type alias, fall back to substituting with template_args[0]
					// (for direct template parameter return types like "T")
					if (!found_type_alias) {
						substituted_return_type = template_args[0].base_type;
						substituted_return_type_index = template_args[0].type_index;
					}
					
					// Calculate return type size for the substituted type
					// Pointers and references are always 64 bits
					int substituted_return_size_bits;
					if (orig_return_type.pointer_depth() > 0 || orig_return_type.is_reference() || orig_return_type.is_rvalue_reference()) {
						substituted_return_size_bits = 64;
					} else {
						substituted_return_size_bits = static_cast<int>(get_type_size_bits(substituted_return_type));
					}
					
					// Create a new TypeSpecifierNode with the substituted return type
					// Use the struct type constructor since we have a type_index
					TypeSpecifierNode new_return_type(
						substituted_return_type,
						substituted_return_type_index,
						substituted_return_size_bits,
						orig_return_type.token(),
						orig_return_type.cv_qualifier()
					);
					
					// Copy pointer levels and reference qualifier from the original
					new_return_type.copy_indirection_from(orig_return_type);
					
					// Update the declaration node with the new type
					orig_decl.set_type_node(emplace_node<TypeSpecifierNode>(new_return_type));
				}
				
				// Add the function to the struct info (with substituted return type if needed)
				// Phase 7B: Intern function name and use StringHandle overload
				StringHandle func_name_handle = orig_decl.identifier_token().handle();
				struct_info->addMemberFunction(
					func_name_handle,
					mem_func.function_declaration,
					mem_func.access,
					mem_func.is_virtual,
					mem_func.is_pure_virtual,
					mem_func.is_override,
					mem_func.is_final
				);
			}
		}

		struct_info->needs_default_constructor = !struct_info->hasAnyConstructor();

		// Copy deleted special member function flags from the pattern AST node
		// This is especially important for partial specializations where deleted constructors
		// are tracked in the AST node but not yet in StructTypeInfo
		FLASH_LOG(Templates, Debug, "Checking pattern AST node for deleted constructors: default=",
			pattern_struct.has_deleted_default_constructor(), ", copy=",
			pattern_struct.has_deleted_copy_constructor(), ", move=",
			pattern_struct.has_deleted_move_constructor());
		if (pattern_struct.has_deleted_default_constructor()) {
			struct_info->has_deleted_default_constructor = true;
			FLASH_LOG(Templates, Debug, "Copied has_deleted_default_constructor from pattern AST node");
		}
		if (pattern_struct.has_deleted_copy_constructor()) {
			struct_info->has_deleted_copy_constructor = true;
		}
		if (pattern_struct.has_deleted_move_constructor()) {
			struct_info->has_deleted_move_constructor = true;
		}

		// Also copy deleted constructor flags from the pattern's StructTypeInfo (if available)
		// Get the pattern's StructTypeInfo
		auto pattern_type_it = gTypesByName.find(pattern_struct.name());
		if (pattern_type_it != gTypesByName.end()) {
			const TypeInfo* pattern_type_info = pattern_type_it->second;
			const StructTypeInfo* pattern_struct_info = pattern_type_info->getStructInfo();
			if (pattern_struct_info) {
				// Copy deleted constructor flags from pattern
				if (pattern_struct_info->has_deleted_default_constructor) {
					struct_info->has_deleted_default_constructor = true;
				}
				if (pattern_struct_info->has_deleted_copy_constructor) {
					struct_info->has_deleted_copy_constructor = true;
				}
				if (pattern_struct_info->has_deleted_move_constructor) {
					struct_info->has_deleted_move_constructor = true;
				}
				if (pattern_struct_info->has_deleted_copy_assignment) {
					struct_info->has_deleted_copy_assignment = true;
				}
				if (pattern_struct_info->has_deleted_move_assignment) {
					struct_info->has_deleted_move_assignment = true;
				}
				if (pattern_struct_info->has_deleted_destructor) {
					struct_info->has_deleted_destructor = true;
				}
				FLASH_LOG(Templates, Debug, "Copied deleted constructor flags from pattern StructTypeInfo: default=",
					pattern_struct_info->has_deleted_default_constructor, ", copy=",
					pattern_struct_info->has_deleted_copy_constructor);

				FLASH_LOG(Templates, Debug, "Copying ", pattern_struct_info->static_members.size(), " static members from pattern");
				for (const auto& static_member : pattern_struct_info->static_members) {
					FLASH_LOG(Templates, Debug, "Copying static member: ", static_member.getName());
					
					// Check if initializer contains sizeof...(pack_name) and substitute with pack size
					std::optional<ASTNode> substituted_initializer = static_member.initializer;
					if (static_member.initializer.has_value() && static_member.initializer->is<ExpressionNode>()) {
						const ExpressionNode& expr = static_member.initializer->as<ExpressionNode>();
						FLASH_LOG(Templates, Debug, "Static member initializer is an expression, checking for sizeof...");
						
						// Calculate pack size for substitution
						auto calculate_pack_size = [&](std::string_view pack_name) -> std::optional<size_t> {
							FLASH_LOG(Templates, Debug, "Looking for pack: ", pack_name);
							for (size_t i = 0; i < template_params.size(); ++i) {
								const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
								FLASH_LOG(Templates, Debug, "  Checking param ", tparam.name(), " is_variadic=", tparam.is_variadic() ? "true" : "false");
								if (tparam.name() == pack_name && tparam.is_variadic()) {
									size_t non_variadic_count = 0;
									for (const auto& param : template_params) {
										if (!param.as<TemplateParameterNode>().is_variadic()) {
											non_variadic_count++;
										}
									}
									return template_args.size() - non_variadic_count;
								}
							}
							return std::nullopt;
						};
						
						// Helper to create a numeric literal from pack size
						auto make_pack_size_literal = [&](size_t pack_size) -> ASTNode {
							std::string_view pack_size_str = StringBuilder().append(pack_size).commit();
							Token num_token(Token::Type::Literal, pack_size_str, 0, 0, 0);
							return emplace_node<ExpressionNode>(
								NumericLiteralNode(num_token, static_cast<unsigned long long>(pack_size), Type::Int, TypeQualifier::None, 32)
							);
						};
						
						if (std::holds_alternative<SizeofPackNode>(expr)) {
							// Direct sizeof... expression
							const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(expr);
							if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
								substituted_initializer = make_pack_size_literal(*pack_size);
							}
						}
						else if (std::holds_alternative<StaticCastNode>(expr)) {
							// Handle static_cast<T>(sizeof...(Ts)) patterns
							const StaticCastNode& cast_node = std::get<StaticCastNode>(expr);
							if (cast_node.expr().is<ExpressionNode>()) {
								const ExpressionNode& cast_inner = cast_node.expr().as<ExpressionNode>();
								if (std::holds_alternative<SizeofPackNode>(cast_inner)) {
									const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(cast_inner);
									if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
										substituted_initializer = make_pack_size_literal(*pack_size);
									}
								}
							}
						}
						else if (std::holds_alternative<BinaryOperatorNode>(expr)) {
							// Binary expression like "1 + sizeof...(Rest)" - need to substitute sizeof...
							const BinaryOperatorNode& bin_expr = std::get<BinaryOperatorNode>(expr);
							
							// Helper to extract pack size from various expression forms
							auto try_extract_pack_size = [&](const ExpressionNode& e) -> std::optional<size_t> {
								if (std::holds_alternative<SizeofPackNode>(e)) {
									const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(e);
									return calculate_pack_size(sizeof_pack.pack_name());
								}
								// Handle static_cast<T>(sizeof...(Ts))
								if (std::holds_alternative<StaticCastNode>(e)) {
									const StaticCastNode& cast_node = std::get<StaticCastNode>(e);
									if (cast_node.expr().is<ExpressionNode>()) {
										const ExpressionNode& cast_inner = cast_node.expr().as<ExpressionNode>();
										if (std::holds_alternative<SizeofPackNode>(cast_inner)) {
											const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(cast_inner);
											return calculate_pack_size(sizeof_pack.pack_name());
										}
									}
								}
								return std::nullopt;
							};
							
							// Helper to extract numeric value from expression
							auto try_extract_numeric = [](const ExpressionNode& e) -> std::optional<unsigned long long> {
								if (std::holds_alternative<NumericLiteralNode>(e)) {
									const NumericLiteralNode& num = std::get<NumericLiteralNode>(e);
									auto val = num.value();
									return std::holds_alternative<unsigned long long>(val) 
										? std::get<unsigned long long>(val)
										: static_cast<unsigned long long>(std::get<double>(val));
								}
								return std::nullopt;
							};
							
							// Helper to evaluate a binary expression
							auto evaluate_binary = [](std::string_view op, unsigned long long lhs, unsigned long long rhs) -> unsigned long long {
								if (op == "+") return lhs + rhs;
								if (op == "-") return lhs - rhs;
								if (op == "*") return lhs * rhs;
								if (op == "/") return rhs != 0 ? lhs / rhs : 0;
								return 0;
							};
							
							// Try to evaluate the top-level binary expression
							if (bin_expr.get_lhs().is<ExpressionNode>() && bin_expr.get_rhs().is<ExpressionNode>()) {
								const ExpressionNode& lhs_expr = bin_expr.get_lhs().as<ExpressionNode>();
								const ExpressionNode& rhs_expr = bin_expr.get_rhs().as<ExpressionNode>();
								
								// Case 1: LHS is pack_size_expr, RHS is numeric
								if (auto lhs_pack = try_extract_pack_size(lhs_expr)) {
									if (auto rhs_num = try_extract_numeric(rhs_expr)) {
										unsigned long long result = evaluate_binary(bin_expr.op(), *lhs_pack, *rhs_num);
										substituted_initializer = make_pack_size_literal(static_cast<size_t>(result));
									}
								}
								// Case 2: LHS is numeric, RHS is pack_size_expr
								else if (auto lhs_num = try_extract_numeric(lhs_expr)) {
									if (auto rhs_pack = try_extract_pack_size(rhs_expr)) {
										unsigned long long result = evaluate_binary(bin_expr.op(), *lhs_num, *rhs_pack);
										substituted_initializer = make_pack_size_literal(static_cast<size_t>(result));
									}
								}
								// Case 3: LHS is nested binary expression, RHS is numeric
								// Handles patterns like (static_cast<int>(sizeof...(Ts)) * 2) + 40
								else if (std::holds_alternative<BinaryOperatorNode>(lhs_expr)) {
									const BinaryOperatorNode& nested_bin = std::get<BinaryOperatorNode>(lhs_expr);
									if (nested_bin.get_lhs().is<ExpressionNode>() && nested_bin.get_rhs().is<ExpressionNode>()) {
										const ExpressionNode& nested_lhs = nested_bin.get_lhs().as<ExpressionNode>();
										const ExpressionNode& nested_rhs = nested_bin.get_rhs().as<ExpressionNode>();
										
										std::optional<unsigned long long> nested_result;
										if (auto nlhs_pack = try_extract_pack_size(nested_lhs)) {
											if (auto nrhs_num = try_extract_numeric(nested_rhs)) {
												nested_result = evaluate_binary(nested_bin.op(), *nlhs_pack, *nrhs_num);
											}
										} else if (auto nlhs_num = try_extract_numeric(nested_lhs)) {
											if (auto nrhs_pack = try_extract_pack_size(nested_rhs)) {
												nested_result = evaluate_binary(nested_bin.op(), *nlhs_num, *nrhs_pack);
											}
										}
										
										if (nested_result) {
											if (auto rhs_num = try_extract_numeric(rhs_expr)) {
												unsigned long long result = evaluate_binary(bin_expr.op(), *nested_result, *rhs_num);
												substituted_initializer = make_pack_size_literal(static_cast<size_t>(result));
											}
										}
									}
								}
							}
						}
						// Handle template parameter reference substitution (e.g., static constexpr T value = v;)
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
							FLASH_LOG(Templates, Debug, "Static member initializer contains template parameter reference: ", tparam_ref.param_name());
							if (auto subst = substitute_template_param_in_initializer(tparam_ref.param_name().view(), template_args, template_params)) {
								substituted_initializer = subst;
								FLASH_LOG(Templates, Debug, "Substituted static member initializer template parameter '", tparam_ref.param_name(), "'");
							}
						}
						// Handle IdentifierNode that might be a template parameter
						else if (std::holds_alternative<IdentifierNode>(expr)) {
							const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
							std::string_view id_name = id_node.name();
							FLASH_LOG(Templates, Debug, "Static member initializer contains IdentifierNode: ", id_name);
							if (auto subst = substitute_template_param_in_initializer(id_name, template_args, template_params)) {
								substituted_initializer = subst;
								FLASH_LOG(Templates, Debug, "Substituted static member initializer identifier '", id_name, "' (template parameter)");
							}
						}
						// Handle FoldExpressionNode (e.g., static constexpr bool value = (Bs && ...);)
						else if (std::holds_alternative<FoldExpressionNode>(expr)) {
							const FoldExpressionNode& fold = std::get<FoldExpressionNode>(expr);
							std::string_view pack_name = fold.pack_name();
							std::string_view op = fold.op();
							FLASH_LOG(Templates, Debug, "Static member initializer contains fold expression with pack: ", pack_name, " op: ", op);
							
							// Find the parameter pack in template parameters
							std::optional<size_t> pack_param_idx;
							for (size_t p = 0; p < template_params.size(); ++p) {
								const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
								if (tparam.name() == pack_name && tparam.is_variadic()) {
									pack_param_idx = p;
									break;
								}
							}
							
							if (pack_param_idx.has_value()) {
								// Collect the values from the variadic pack arguments
								std::vector<int64_t> pack_values;
								bool all_values_found = true;
								
								// For variadic packs, arguments after non-variadic parameters are the pack values
								size_t non_variadic_count = 0;
								for (const auto& param : template_params) {
									if (!param.as<TemplateParameterNode>().is_variadic()) {
										non_variadic_count++;
									}
								}
								
								for (size_t i = non_variadic_count; i < template_args.size() && all_values_found; ++i) {
									if (template_args[i].is_value) {
										pack_values.push_back(template_args[i].value);
										FLASH_LOG(Templates, Debug, "Pack value[", i - non_variadic_count, "] = ", template_args[i].value);
									} else {
										all_values_found = false;
									}
								}
								
								if (all_values_found && !pack_values.empty()) {
									auto fold_result = evaluate_fold_expression(op, pack_values);
									if (fold_result.has_value()) {
										substituted_initializer = *fold_result;
									}
								}
							}
						}
						// Handle TernaryOperatorNode where the condition is a template parameter (e.g., IsArith ? 42 : 0)
						else if (std::holds_alternative<TernaryOperatorNode>(expr)) {
							const TernaryOperatorNode& ternary = std::get<TernaryOperatorNode>(expr);
							const ASTNode& cond_node = ternary.condition();
							
							// Check if condition is a template parameter reference or identifier
							if (cond_node.is<ExpressionNode>()) {
								const ExpressionNode& cond_expr = cond_node.as<ExpressionNode>();
								std::optional<int64_t> cond_value;
								
								if (std::holds_alternative<TemplateParameterReferenceNode>(cond_expr)) {
									const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(cond_expr);
									FLASH_LOG(Templates, Debug, "Ternary condition is template parameter: ", tparam_ref.param_name());
									
									// Look up the parameter value
									for (size_t p = 0; p < template_params.size(); ++p) {
										const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
										if (tparam.name() == tparam_ref.param_name() && tparam.kind() == TemplateParameterKind::NonType) {
											if (p < template_args.size() && template_args[p].is_value) {
												cond_value = template_args[p].value;
												FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
											}
											break;
										}
									}
								}
								else if (std::holds_alternative<IdentifierNode>(cond_expr)) {
									const IdentifierNode& id_node = std::get<IdentifierNode>(cond_expr);
									std::string_view id_name = id_node.name();
									FLASH_LOG(Templates, Debug, "Ternary condition is identifier: ", id_name);
									
									// Look up the identifier as a template parameter
									for (size_t p = 0; p < template_params.size(); ++p) {
										const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
										if (tparam.name() == id_name && tparam.kind() == TemplateParameterKind::NonType) {
											if (p < template_args.size() && template_args[p].is_value) {
												cond_value = template_args[p].value;
												FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
											}
											break;
										}
									}
								}
								
								// If we found the condition value, evaluate the ternary
								if (cond_value.has_value()) {
									const ASTNode& result_branch = (*cond_value != 0) ? ternary.true_expr() : ternary.false_expr();
									
									if (result_branch.is<ExpressionNode>()) {
										const ExpressionNode& result_expr = result_branch.as<ExpressionNode>();
										if (std::holds_alternative<NumericLiteralNode>(result_expr)) {
											const NumericLiteralNode& lit = std::get<NumericLiteralNode>(result_expr);
											const auto& val = lit.value();
											unsigned long long num_val = std::holds_alternative<unsigned long long>(val)
												? std::get<unsigned long long>(val)
												: static_cast<unsigned long long>(std::get<double>(val));
											
											// Create a new numeric literal with the evaluated result
											std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(num_val)).commit();
											Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
											substituted_initializer = emplace_node<ExpressionNode>(
												NumericLiteralNode(num_token, num_val, lit.type(), lit.qualifier(), lit.sizeInBits())
											);
											FLASH_LOG(Templates, Debug, "Evaluated ternary to: ", num_val);
										}
									}
								}
							}
						}
					}
					
					// Phase 7B: Intern static member name and use StringHandle overload
					StringHandle static_member_name_handle = StringTable::getOrInternStringHandle(StringTable::getStringView(static_member.getName()));
					struct_info->addStaticMember(
						static_member_name_handle,
						static_member.type,
						static_member.type_index,
						static_member.size,
						static_member.alignment,
						static_member.access,
						substituted_initializer,
						static_member.is_const
					);
				}
			}
		}
		
		// Also copy static members from the pattern AST node (for member template partial specializations)
		// These may not have been added to StructTypeInfo yet
		if (!pattern_struct.static_members().empty()) {
			FLASH_LOG(Templates, Debug, "Copying ", pattern_struct.static_members().size(), " static members from pattern AST node");
			for (const auto& static_member : pattern_struct.static_members()) {
				FLASH_LOG(Templates, Debug, "Copying static member from AST: ", StringTable::getStringView(static_member.name));
				
				// Check if already added from StructTypeInfo
				if (struct_info->findStaticMember(static_member.name) != nullptr) {
					continue;  // Already added
				}
				
				// Substitute type if it's a template parameter
				// Create a TypeSpecifierNode from the static member's type info to use substitute_template_parameter
				TypeSpecifierNode original_type_spec(static_member.type, TypeQualifier::None, static_member.size * 8);
				original_type_spec.set_type_index(static_member.type_index);
				
				// Use substitute_template_parameter for consistent template parameter matching
				auto [substituted_type, substituted_type_index] = substitute_template_parameter(
					original_type_spec, template_params, template_args);
				
				size_t substituted_size = get_type_size_bits(substituted_type) / 8;
				
				// Substitute template parameters in the static member initializer
				// Use ExpressionSubstitutor to handle all types of template-dependent expressions
				std::optional<ASTNode> substituted_initializer = static_member.initializer;
				if (static_member.initializer.has_value()) {
					// Build parameter substitution map and preserve parameter order
					std::unordered_map<std::string_view, TemplateTypeArg> param_map;
					std::vector<std::string_view> template_param_order;
					for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
						if (template_params[i].is<TemplateParameterNode>()) {
							const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
							param_map[param.name()] = template_args[i];
							template_param_order.push_back(param.name());
						}
					}
					
					// Use ExpressionSubstitutor to substitute template parameters in the initializer
					if (!param_map.empty()) {
						ExpressionSubstitutor substitutor(param_map, *this, template_param_order);
						substituted_initializer = substitutor.substitute(static_member.initializer.value());
						FLASH_LOG(Templates, Debug, "Substituted template parameters in static member initializer");
					}
				}
				
				struct_info->addStaticMember(
					static_member.name,
					substituted_type,
					substituted_type_index,
					substituted_size,
					static_member.alignment,
					static_member.access,
					substituted_initializer,
					static_member.is_const
				);
			}
		}
		
		// Finalize the struct layout
		bool finalize_success;
		if (!pattern_struct.base_classes().empty()) {
			finalize_success = struct_info->finalizeWithBases();
		} else {
			finalize_success = struct_info->finalize();
		}
		
		// Check for semantic errors during finalization
		if (!finalize_success) {
			// Log error and return nullopt - compilation will continue but template instantiation fails
			FLASH_LOG(Parser, Error, struct_info->getFinalizationError());
			return std::nullopt;
		}
		struct_type_info.setStructInfo(std::move(struct_info));
		if (struct_type_info.getStructInfo()) {
			struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
		}
		
		// Register type aliases from the pattern with qualified names
		// We need the pattern_args to map template parameters to template arguments
		std::vector<TemplateTypeArg> pattern_args;
		auto patterns_it_for_alias = gTemplateRegistry.specialization_patterns_.find(std::string(template_name));
		if (patterns_it_for_alias != gTemplateRegistry.specialization_patterns_.end()) {
			for (const auto& pattern : patterns_it_for_alias->second) {
				// Handle both StructDeclarationNode and TemplateClassDeclarationNode patterns
				const StructDeclarationNode* spec_struct_ptr_alias = nullptr;
				if (pattern.specialized_node.is<StructDeclarationNode>()) {
					spec_struct_ptr_alias = &pattern.specialized_node.as<StructDeclarationNode>();
				} else if (pattern.specialized_node.is<TemplateClassDeclarationNode>()) {
					spec_struct_ptr_alias = &pattern.specialized_node.as<TemplateClassDeclarationNode>().class_decl_node();
				}
				if (spec_struct_ptr_alias && spec_struct_ptr_alias == &pattern_struct) {
					pattern_args = pattern.pattern_args;
					break;
				}
			}
		}
		
		for (const auto& type_alias : pattern_struct.type_aliases()) {
			// Build the qualified name: enable_if_true_int::type
			auto qualified_alias_name = StringTable::getOrInternStringHandle(StringBuilder()
				.append(instantiated_name)
				.append("::")
				.append(type_alias.alias_name));
			
			// Check if already registered
			if (gTypesByName.find(qualified_alias_name) != gTypesByName.end()) {
				continue;  // Already registered
			}
			
			// Get the type information from the alias
			const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();
			
			// For partial specializations, we may need to substitute template parameters
			// For example, if pattern has "using type = T;" and we're instantiating with int,
			// we need to substitute T -> int
			Type substituted_type = alias_type_spec.type();
			TypeIndex substituted_type_index = alias_type_spec.type_index();
			int substituted_size = alias_type_spec.size_in_bits();
			
			// Check if the alias type is a template parameter that needs substitution
			if (alias_type_spec.type() == Type::UserDefined && !template_args.empty() && !pattern_args.empty()) {
				// The alias_type_spec.type_index() identifies which template parameter this is
				// We need to find which pattern_arg corresponds to this template parameter,
				// then map to the corresponding template_arg
				
				// For enable_if<true, T>:
				// - pattern_args = [true (is_value=true), T (is_value=false, is_dependent=true)]
				// - template_params = [T] (template parameter at index 0)
				// - template_args = [true (is_value=true), int (is_value=false)]
				// - The alias "using type = T" has T which is template_params[0]
				// - T appears at pattern_args[1]
				// - So we substitute with template_args[1] = int
				
				// Find which template parameter index this alias type corresponds to
				for (size_t param_idx = 0; param_idx < template_params.size(); ++param_idx) {
					if (template_params[param_idx].is<TemplateParameterNode>()) {
						// Find which pattern_arg position this template parameter appears at
						for (size_t pattern_idx = 0; pattern_idx < pattern_args.size() && pattern_idx < template_args.size(); ++pattern_idx) {
							const TemplateTypeArg& pattern_arg = pattern_args[pattern_idx];
							
							// Check if this pattern_arg is a template parameter (not a concrete value/type)
							if (!pattern_arg.is_value && pattern_arg.is_dependent) {
								// This is a template parameter position
								// Check if it's the parameter we're looking for
								// We can match by counting dependent parameters
								size_t dependent_param_index = 0;
								for (size_t i = 0; i < pattern_idx; ++i) {
									if (!pattern_args[i].is_value && pattern_args[i].is_dependent) {
										dependent_param_index++;
									}
								}
								
								if (dependent_param_index == param_idx) {
									// Found it! Substitute with template_args[pattern_idx]
									const TemplateTypeArg& concrete_arg = template_args[pattern_idx];
									substituted_type = concrete_arg.base_type;
									substituted_type_index = concrete_arg.type_index;
									// Only call get_type_size_bits for basic types
									if (substituted_type != Type::UserDefined) {
										substituted_size = static_cast<unsigned char>(get_type_size_bits(substituted_type));
									} else {
										// For UserDefined types, look up the size from the type registry
										substituted_size = 0;
										if (substituted_type_index < gTypeInfo.size()) {
											substituted_size = gTypeInfo[substituted_type_index].type_size_;
										}
									}
									FLASH_LOG(Templates, Debug, "Substituted template parameter '", 
										template_params[param_idx].as<TemplateParameterNode>().name(), 
										"' at pattern position ", pattern_idx, " with type=", static_cast<int>(substituted_type));
									goto substitution_done;
								}
							}
						}
					}
				}
				substitution_done:;
			}
			
			// Register the type alias globally with its qualified name
			auto& alias_type_info = gTypeInfo.emplace_back(
				qualified_alias_name,
				substituted_type,
				substituted_type_index,
				substituted_size
			);
			gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
			
			FLASH_LOG(Templates, Debug, "Registered type alias from pattern: ", qualified_alias_name, 
				" -> type=", static_cast<int>(substituted_type), 
				", type_index=", substituted_type_index);
		}
		
		// Create an AST node for the instantiated struct so member functions can be code-generated
		auto instantiated_struct = emplace_node<StructDeclarationNode>(
			instantiated_name,
			false  // is_class
		);
		StructDeclarationNode& instantiated_struct_ref = instantiated_struct.as<StructDeclarationNode>();
		
		// Copy data members
		for (const auto& member_decl : pattern_struct.members()) {
			instantiated_struct_ref.add_member(
				member_decl.declaration,
				member_decl.access,
				member_decl.default_initializer
			);
		}
		
		// Copy member functions to AST node WITH CORRECT PARENT STRUCT NAME
		// This is critical - we need to create new FunctionDeclarationNodes with instantiated_name as parent
		for (const StructMemberFunctionDecl& mem_func : pattern_struct.member_functions()) {
			if (mem_func.is_constructor) {
				// Handle constructor - it's a ConstructorDeclarationNode
				const ConstructorDeclarationNode& orig_ctor = mem_func.function_declaration.as<ConstructorDeclarationNode>();
				
				// Create a NEW ConstructorDeclarationNode with the instantiated struct name
				auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
					instantiated_name,  // Set correct parent struct name
					orig_ctor.name()    // Constructor name (same as template name)
				);
				
				// Copy parameters
				for (const auto& param : orig_ctor.parameter_nodes()) {
					new_ctor_ref.add_parameter_node(param);
				}
				
				// Copy member initializers
				for (const auto& [name, expr] : orig_ctor.member_initializers()) {
					new_ctor_ref.add_member_initializer(name, expr);
				}
				
				// Copy definition if present
				if (orig_ctor.get_definition().has_value()) {
					new_ctor_ref.set_definition(*orig_ctor.get_definition());
				}
				
				instantiated_struct_ref.add_constructor(new_ctor_node, mem_func.access);
			} else if (mem_func.is_destructor) {
				// Handle destructor
				instantiated_struct_ref.add_destructor(mem_func.function_declaration, mem_func.access, mem_func.is_virtual);
			} else {
				const FunctionDeclarationNode& orig_func = mem_func.function_declaration.as<FunctionDeclarationNode>();
				
				// Create a NEW FunctionDeclarationNode with the instantiated struct name
				// This will set is_member_function_ = true and parent_struct_name_ correctly
				auto new_func_node = emplace_node<FunctionDeclarationNode>(
					const_cast<DeclarationNode&>(orig_func.decl_node()),  // Reuse declaration
					instantiated_name  // Set correct parent struct name
				);
				
				// Copy all parameters and definition
				FunctionDeclarationNode& new_func = new_func_node.as<FunctionDeclarationNode>();
				for (const auto& param : orig_func.parameter_nodes()) {
					new_func.add_parameter_node(param);
				}
				if (orig_func.get_definition().has_value()) {
					FLASH_LOG(Templates, Debug, "Copying function definition to new function");
					new_func.set_definition(*orig_func.get_definition());
				} else {
					FLASH_LOG(Templates, Debug, "Original function has NO definition - may need delayed parsing");
				}
				
				instantiated_struct_ref.add_member_function(
					new_func_node,
					mem_func.access
				);
			}
		}
		
		// Re-evaluate deferred static_asserts with substituted template parameters
		FLASH_LOG(Templates, Debug, "Checking ", pattern_struct.deferred_static_asserts().size(), 
		          " deferred static_asserts for instantiation");
		
		for (const auto& deferred_assert : pattern_struct.deferred_static_asserts()) {
			FLASH_LOG(Templates, Debug, "Re-evaluating deferred static_assert during template instantiation");
			
// Build template parameter name to type mapping for substitution
			std::unordered_map<std::string_view, TemplateTypeArg> param_map;
			std::vector<std::string_view> template_param_order;
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
				// param.name() already returns string_view
				param_map[param.name()] = template_args[i];
				template_param_order.push_back(param.name());
			}
			
			// Create substitution context with template parameter mappings
			ExpressionSubstitutor substitutor(param_map, *this, template_param_order);
			
			// Substitute template parameters in the condition expression
			ASTNode substituted_expr = substitutor.substitute(deferred_assert.condition_expr);
			
			// Evaluate the substituted expression
			ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
			eval_ctx.parser = this;
			eval_ctx.struct_node = &instantiated_struct_ref;
			
			auto eval_result = ConstExpr::Evaluator::evaluate(substituted_expr, eval_ctx);
			
			if (!eval_result.success()) {
				std::string error_msg = "static_assert failed during template instantiation: " + 
				                       eval_result.error_message;
				std::string_view message_view = StringTable::getStringView(deferred_assert.message);
				if (!message_view.empty()) {
					error_msg += " - " + std::string(message_view);
				}
				FLASH_LOG(Templates, Error, error_msg);
				// Don't return error - continue with other static_asserts
				// This matches the behavior of most compilers which report all failures
				continue;
			}
			
			// Check if the assertion failed
			if (!eval_result.as_bool()) {
				std::string error_msg = "static_assert failed during template instantiation";
				std::string_view message_view = StringTable::getStringView(deferred_assert.message);
				if (!message_view.empty()) {
					error_msg += ": " + std::string(message_view);
				}
				FLASH_LOG(Templates, Error, error_msg);
				// Don't return error - continue with other static_asserts
				continue;
			}
		
			FLASH_LOG(Templates, Debug, "Deferred static_assert passed during template instantiation");
		}
		
		// Mark instantiation complete with the type index
		FlashCpp::gInstantiationQueue.markComplete(inst_key, struct_type_info.type_index_);
		in_progress_guard.dismiss();  // Don't remove from in_progress in destructor
		
		// Register in V2 cache for O(1) lookup on future instantiations
		gTemplateRegistry.registerInstantiationV2(v2_key, instantiated_struct);
		
		return instantiated_struct;  // Return the struct node for code generation
		}
	}

	// No specialization found - use the primary template
	ASTNode template_node;
	{
		PROFILE_TEMPLATE_LOOKUP();
		auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
		if (!template_opt.has_value()) {
			FLASH_LOG(Templates, Error, "No primary template found for '", template_name, "', returning nullopt");
			return std::nullopt;  // No template with this name
		}
		template_node = *template_opt;
	}
	
	if (!template_node.is<TemplateClassDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Template node is not a TemplateClassDeclarationNode for '", template_name, "', returning nullopt");
		return std::nullopt;  // Not a class template
	}

	const TemplateClassDeclarationNode& template_class = template_node.as<TemplateClassDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_class.template_parameters();
	const StructDeclarationNode& class_decl = template_class.class_decl_node();

	// Count non-variadic parameters
	size_t non_variadic_param_count = 0;
	bool has_parameter_pack = false;
	
	for (size_t i = 0; i < template_params.size(); ++i) {
		const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
		if (param.is_variadic()) {
			has_parameter_pack = true;
		} else {
			non_variadic_param_count++;
		}
	}
	
	// Verify we have the right number of template arguments
	// For variadic templates: args.size() >= non_variadic_param_count
	// For non-variadic templates: args.size() <= template_params.size()
	if (has_parameter_pack) {
		// With parameter pack, we need at least the non-variadic parameters
		if (template_args.size() < non_variadic_param_count) {
			FLASH_LOG(Templates, Error, "Too few arguments for variadic template (got ", template_args.size(), 
			          ", need at least ", non_variadic_param_count, ")");
			return std::nullopt;
		}
		// The rest of the arguments go into the parameter pack
	} else {
		// Non-variadic template: allow fewer arguments if remaining parameters have defaults
		if (template_args.size() > template_params.size()) {
			return std::nullopt;  // Too many template arguments
		}
	}
	
	// Create a mutable copy of template_args to fill in defaults
	std::vector<TemplateTypeArg> filled_template_args(template_args.begin(), template_args.end());
	
	// Fill in default arguments for missing parameters
	for (size_t i = filled_template_args.size(); i < template_params.size(); ++i) {
		const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
		// Skip variadic parameters - they're allowed to be empty
		if (param.is_variadic()) {
			continue;
		}
		
		if (!param.has_default()) {
			FLASH_LOG(Templates, Error, "Template '", template_name, "': Param ", i, " has no default (got ", 
			          template_args.size(), " args, need ", template_params.size(), "), returning nullopt");
			return std::nullopt;  // Missing required template argument
		}
		
		// Use the default value
		if (param.kind() == TemplateParameterKind::Type) {
			// For type parameters with defaults, extract the type
			const ASTNode& default_node = param.default_value();
			if (default_node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
				
				// Check if this is a dependent qualified type (like wrapper<T>::type)
				// that needs resolution based on already-filled template arguments
				bool resolved = false;
				if (default_type.type() == Type::UserDefined && default_type.type_index() > 0 && 
				    default_type.type_index() < gTypeInfo.size()) {
					const TypeInfo& default_type_info = gTypeInfo[default_type.type_index()];
					std::string_view default_type_name = StringTable::getStringView(default_type_info.name());
					
					// Try to resolve using each filled argument
					for (size_t arg_idx = 0; arg_idx < filled_template_args.size(); ++arg_idx) {
						auto resolved_type = resolve_dependent_qualified_type(default_type_name, filled_template_args[arg_idx]);
						if (resolved_type.has_value()) {
							filled_template_args.push_back(*resolved_type);
							resolved = true;
							break;
						}
					}
				}
				
				if (!resolved) {
					filled_template_args.push_back(TemplateTypeArg(default_type));
				}
			}
		} else if (param.kind() == TemplateParameterKind::NonType) {
			// For non-type parameters with defaults, evaluate the expression
			const ASTNode& default_node = param.default_value();
			FLASH_LOG(Templates, Debug, "Processing non-type param default, is_expression=", default_node.is<ExpressionNode>());
			
			// Build parameter substitution map for already-filled template arguments
			// This allows the default expression to reference earlier template parameters
			std::unordered_map<std::string_view, TemplateTypeArg> param_map;
			for (size_t j = 0; j < i && j < template_params.size(); ++j) {
				if (template_params[j].is<TemplateParameterNode>()) {
					const TemplateParameterNode& earlier_param = template_params[j].as<TemplateParameterNode>();
					param_map[earlier_param.name()] = filled_template_args[j];
					FLASH_LOG(Templates, Debug, "Added param '", earlier_param.name(), "' to substitution map for default evaluation");
				}
			}
			
			// Substitute template parameters in the default expression
			ASTNode substituted_default_node = default_node;
			if (!param_map.empty() && default_node.is<ExpressionNode>()) {
				ExpressionSubstitutor substitutor(param_map, *this);
				substituted_default_node = substitutor.substitute(default_node);
				FLASH_LOG(Templates, Debug, "Substituted template parameters in non-type default expression");
			}
			
			if (substituted_default_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = substituted_default_node.as<ExpressionNode>();
				FLASH_LOG(Templates, Debug, "Expression node type index: ", expr.index());
				if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
					const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(expr);
					FLASH_LOG(Templates, Debug, "Processing QualifiedIdentifierNode for non-type default");
					
					// Handle dependent static member access like is_arithmetic_void::value or is_arithmetic__Tp::value
					// namespace handle name = template instantiation name (e.g., is_arithmetic_void or is_arithmetic__Tp)
					// name() = member name (e.g., value)
					if (!qual_id.namespace_handle().isGlobal()) {
						std::string_view type_name = gNamespaceRegistry.getName(qual_id.namespace_handle());
						std::string_view member_name = qual_id.name();
						FLASH_LOG(Templates, Debug, "Non-global qualified id: type='", type_name, "', member='", member_name, "'");
						
						// Check if type_name contains a template parameter placeholder
						// Use TypeInfo-based detection for template instantiation placeholders
						auto [is_dependent, template_base_name] = isDependentTemplatePlaceholder(type_name);
						
						// Additional check: if not detected as template instantiation, check for param-like suffixes
						if (!is_dependent && !filled_template_args.empty()) {
							// Check if type_name ends with what looks like a template parameter
							// Mangling: template_name + "_" + param_name
							// For param "_Tp", this becomes: template_name + "_" + "_Tp" = template_name + "__Tp"
							size_t last_underscore = type_name.rfind('_');
							FLASH_LOG(Templates, Debug, "Checking for dependent param in type='", type_name, "', last_underscore=", last_underscore);
							if (last_underscore != std::string_view::npos && last_underscore > 0) {
								std::string_view suffix = type_name.substr(last_underscore + 1);
								FLASH_LOG(Templates, Debug, "Suffix='", suffix, "'");
								
								// Check if suffix looks like a template parameter
								// Template parameters typically start with uppercase (Tp, T, U) or underscore (_Tp)
								bool looks_like_param = false;
								if (!suffix.empty() && (std::isupper(static_cast<unsigned char>(suffix[0])) || 
								                        suffix[0] == '_')) {
									looks_like_param = true;
								}
								
								// Special case: if suffix is empty and the character before last_underscore is also '_',
								// then the param starts with '_'. Try splitting earlier.
								if (suffix.empty() && last_underscore > 0 && type_name[last_underscore - 1] == '_') {
									// Double underscore: template_name + "__" + rest_of_param
									// Try finding the underscore before the double underscore
									size_t prev_underscore = type_name.rfind('_', last_underscore - 1);
									if (prev_underscore != std::string_view::npos) {
										template_base_name = type_name.substr(0, prev_underscore);
										is_dependent = true;
										FLASH_LOG(Templates, Debug, "Double underscore detected, template_base_name='", template_base_name, "'");
									}
								} else if (looks_like_param) {
									// Check if there's a double underscore pattern (param starts with _)
									// Pattern: "template__Param" where Param starts with _ 
									// We split at position of second _, giving suffix without leading _
									// Check if previous char is also underscore
									if (last_underscore > 0 && type_name[last_underscore - 1] == '_') {
										// Yes, double underscore. Template name ends before the first of the two underscores
										template_base_name = type_name.substr(0, last_underscore - 1);
										is_dependent = true;
									} else {
										// Single underscore separator
										template_base_name = type_name.substr(0, last_underscore);
										is_dependent = true;
									}
								}
								
								// Already set is_dependent=true above, just log
								if (!template_base_name.empty()) {
									FLASH_LOG(Templates, Debug, "Looks like template param! template_base_name='", template_base_name, "'");
								}
							}
						}
						
						if (is_dependent) {
							
							// Build the instantiated template name using hash-based naming
							std::string_view inst_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
							
							FLASH_LOG(Templates, Debug, "Resolving dependent qualified identifier: ", 
							          type_name, "::", member_name, " -> ", inst_name, "::", member_name);
							
							// Try to instantiate the template
							try_instantiate_class_template(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
							
							// Look up the instantiated type
							auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
							if (type_it != gTypesByName.end()) {
								const TypeInfo* type_info = type_it->second;
								if (type_info->getStructInfo()) {
									const StructTypeInfo* struct_info = type_info->getStructInfo();
									// Find the static member
									for (const auto& static_member : struct_info->static_members) {
										if (StringTable::getStringView(static_member.getName()) == member_name) {
											// Evaluate the static member's initializer
											if (static_member.initializer.has_value()) {
												const ASTNode& init_node = *static_member.initializer;
												if (init_node.is<ExpressionNode>()) {
													const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
													if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
														bool val = std::get<BoolLiteralNode>(init_expr).value();
														filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL));
														FLASH_LOG(Templates, Debug, "Resolved static member '", member_name, "' to ", val);
													} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
														const NumericLiteralNode& lit = std::get<NumericLiteralNode>(init_expr);
														const auto& val = lit.value();
														if (std::holds_alternative<unsigned long long>(val)) {
															filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
															FLASH_LOG(Templates, Debug, "Resolved static member '", member_name, "' to numeric value");
														}
													}
												}
											}
											break;
										}
									}
								}
							}
						}
					}
				}
				if (std::holds_alternative<NumericLiteralNode>(expr)) {
					const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
					const auto& val = lit.value();
					if (std::holds_alternative<unsigned long long>(val)) {
						int64_t int_val = static_cast<int64_t>(std::get<unsigned long long>(val));
						filled_template_args.push_back(TemplateTypeArg(int_val));
					} else if (std::holds_alternative<double>(val)) {
						int64_t int_val = static_cast<int64_t>(std::get<double>(val));
						filled_template_args.push_back(TemplateTypeArg(int_val));
					}
				} else if (std::holds_alternative<BoolLiteralNode>(expr)) {
					// Handle boolean literals
					const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr);
					filled_template_args.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL));
				} else if (std::holds_alternative<MemberAccessNode>(expr)) {
					// Handle dependent expressions like is_arithmetic<T>::value
					const MemberAccessNode& member_access = std::get<MemberAccessNode>(expr);
					std::string_view member_name = member_access.member_name();
					
					FLASH_LOG(Templates, Debug, "Processing MemberAccess for non-type default: member='", member_name, "'");
					
					// Check if the object is a type/template instantiation
					ASTNode object_node = member_access.object();
					if (object_node.is<ExpressionNode>()) {
						const ExpressionNode& obj_expr = object_node.as<ExpressionNode>();
						
						// The object might be an IdentifierNode referencing a template
						if (std::holds_alternative<IdentifierNode>(obj_expr)) {
							const IdentifierNode& obj_id = std::get<IdentifierNode>(obj_expr);
							std::string_view obj_name = obj_id.name();
							
							FLASH_LOG(Templates, Debug, "MemberAccess object is IdentifierNode: '", obj_name, "'");
							
							// Check if this identifier has template arguments stored separately
							// For now, look for a type that was parsed as a dependent template instantiation
							// The type name might be stored like "is_arithmetic$hash" for is_arithmetic<T>
							
							// Try looking up as a dependent template instantiation
							// Build the instantiated name using filled_template_args
							if (!filled_template_args.empty()) {
								// Use hash-based naming instead of underscore-based
								std::string_view inst_name = get_instantiated_class_name(obj_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
								
								FLASH_LOG(Templates, Debug, "Looking up instantiated type: '", inst_name, "'");
								
								// Try to instantiate the template
								try_instantiate_class_template(obj_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
								
								// Look up the instantiated type
								auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
								if (type_it != gTypesByName.end()) {
									const TypeInfo* type_info = type_it->second;
									if (type_info->getStructInfo()) {
										const StructTypeInfo* struct_info = type_info->getStructInfo();
										// Find the static member
										for (const auto& static_member : struct_info->static_members) {
											if (StringTable::getStringView(static_member.getName()) == member_name) {
												// Evaluate the static member's initializer
												if (static_member.initializer.has_value()) {
													const ASTNode& init_node = *static_member.initializer;
													if (init_node.is<ExpressionNode>()) {
														const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
														if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
															bool val = std::get<BoolLiteralNode>(init_expr).value();
															filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL));
															FLASH_LOG(Templates, Debug, "Resolved static member '", member_name, "' to ", val);
														} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
															const NumericLiteralNode& lit = std::get<NumericLiteralNode>(init_expr);
															const auto& val = lit.value();
															if (std::holds_alternative<unsigned long long>(val)) {
																filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
															}
														}
													}
												}
												break;
											}
										}
									}
								}
							}
						}
					}
				} else if (std::holds_alternative<SizeofExprNode>(expr)) {
					// Handle sizeof(T) as a default value
					const SizeofExprNode& sizeof_node = std::get<SizeofExprNode>(expr);
					if (sizeof_node.is_type()) {
						// sizeof(type) - evaluate the type size
						const ASTNode& type_node = sizeof_node.type_or_expr();
						if (type_node.is<TypeSpecifierNode>()) {
							TypeSpecifierNode type_spec = type_node.as<TypeSpecifierNode>();
							
							// Check if this is a template parameter that needs substitution
							bool found_substitution = false;
							std::string_view sizeof_type_name;
							
							// Try to get the type name from the token first (most reliable for template params)
							if (type_spec.token().type() == Token::Type::Identifier) {
								sizeof_type_name = type_spec.token().value();
							} else if (type_spec.type() == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
								// Fall back to gTypeInfo for fully resolved types
								const TypeInfo& sizeof_type_info = gTypeInfo[type_spec.type_index()];
								sizeof_type_name = StringTable::getStringView(sizeof_type_info.name());
							}
							
							if (!sizeof_type_name.empty()) {
								// Check if this is one of the template parameters we've already filled
								for (size_t j = 0; j < template_params.size() && j < filled_template_args.size(); ++j) {
									if (template_params[j].is<TemplateParameterNode>()) {
										const TemplateParameterNode& prev_param = template_params[j].as<TemplateParameterNode>();
										if (prev_param.name() == sizeof_type_name) {
											// Found the matching template parameter - use its filled value
											const TemplateTypeArg& filled_arg = filled_template_args[j];
											if (filled_arg.base_type != Type::Invalid) {
												// Calculate the size of the filled type
												int size_in_bytes = 0;
												switch (filled_arg.base_type) {
													case Type::Bool:
													case Type::Char:
													case Type::UnsignedChar:
														size_in_bytes = 1;
														break;
													case Type::Short:
													case Type::UnsignedShort:
														size_in_bytes = 2;
														break;
													case Type::Int:
													case Type::UnsignedInt:
													case Type::Float:
														size_in_bytes = 4;
														break;
													case Type::Long:
													case Type::UnsignedLong:
													case Type::LongLong:
													case Type::UnsignedLongLong:
													case Type::Double:
														size_in_bytes = 8;
														break;
													case Type::Struct:
														// For struct types, we need to look up the actual size
														if (filled_arg.type_index < gTypeInfo.size()) {
															const TypeInfo& struct_type = gTypeInfo[filled_arg.type_index];
															if (struct_type.getStructInfo()) {
																size_in_bytes = struct_type.getStructInfo()->total_size;
															}
														}
														break;
													default:
														break;
												}
												if (size_in_bytes > 0) {
													filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(size_in_bytes)));
													FLASH_LOG(Templates, Debug, "Filled in sizeof(", sizeof_type_name, ") default for instantiation: ", size_in_bytes, " bytes");
													found_substitution = true;
													break;
												}
											}
										}
									}
								}
							}
							
							if (!found_substitution) {
								// Direct type (not a template parameter)
								int size_in_bits = type_spec.size_in_bits();
								int size_in_bytes = (size_in_bits + 7) / 8;  // Round up to bytes
								filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(size_in_bytes)));
								FLASH_LOG(Templates, Debug, "Filled in sizeof default for instantiation: ", size_in_bytes, " bytes");
							}
						}
					}
				}
			}
		}
	}
	
	// Use the filled template args for the rest of the function
	const std::vector<TemplateTypeArg>& template_args_to_use = filled_template_args;

	// Build substitution maps for dependent template entities (used by deferred bases and decltype bases)
	std::unordered_map<std::string_view, TemplateTypeArg> name_substitution_map;
	std::unordered_map<StringHandle, std::vector<TemplateTypeArg>, TransparentStringHash, std::equal_to<>> pack_substitution_map;
	std::vector<std::string_view> template_param_order;
	bool substitution_maps_initialized = false;
	auto ensure_substitution_maps = [&]() {
		if (substitution_maps_initialized) {
			return;
		}
		
		size_t arg_index = 0;
		for (size_t i = 0; i < template_params.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) continue;
			
			const auto& tparam = template_params[i].as<TemplateParameterNode>();
			if (tparam.kind() != TemplateParameterKind::Type) continue;
			
			std::string_view param_name = tparam.name();
			template_param_order.push_back(param_name);
			
			// Check if this is a variadic pack parameter
			if (tparam.is_variadic()) {
				// Collect remaining arguments as a pack
				std::vector<TemplateTypeArg> pack_args;
				for (size_t j = arg_index; j < template_args_to_use.size(); ++j) {
					pack_args.push_back(template_args_to_use[j]);
				}
				// Intern the pack name as StringHandle
				StringHandle pack_handle = StringTable::getOrInternStringHandle(param_name);
				pack_substitution_map[pack_handle] = pack_args;
				FLASH_LOG(Templates, Debug, "Added pack substitution: ", param_name, " -> ", pack_args.size(), " arguments");
				// All remaining args consumed
				break;
			} else {
				// Regular scalar parameter
				if (arg_index < template_args_to_use.size()) {
					name_substitution_map[param_name] = template_args_to_use[arg_index];
					FLASH_LOG(Templates, Debug, "Added substitution: ", param_name, " -> base_type=", (int)template_args_to_use[arg_index].base_type, " type_index=", template_args_to_use[arg_index].type_index);
					arg_index++;
				}
			}
		}
		
		substitution_maps_initialized = true;
	};

	// Generate the instantiated class name (again, with filled args)
	instantiated_name = StringTable::getOrInternStringHandle(get_instantiated_class_name(template_name, template_args_to_use));

	// Check if we already have this instantiation (after filling defaults)
	existing_type = gTypesByName.find(instantiated_name);
	if (existing_type != gTypesByName.end()) {
		FLASH_LOG(Templates, Debug, "Type already exists, returning nullopt");
		// Already instantiated, return the existing struct node
		// We need to find the struct node in the AST
		// For now, just return nullopt and let the type lookup handle it
		return std::nullopt;
	}

	// Create a new struct type for the instantiation (but don't create AST node for template instantiations)
	TypeInfo& struct_type_info = add_struct_type(instantiated_name);
	
	// Store template instantiation metadata for O(1) lookup (Phase 6)
	// This allows us to check if a type is a template instantiation without parsing the name
	// IMPORTANT: Use base template name without namespace prefix
	std::string_view unqualified_template_name = template_name;
	if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
		unqualified_template_name = template_name.substr(last_colon + 2);
	}
	struct_type_info.setTemplateInstantiationInfo(
		StringTable::getOrInternStringHandle(unqualified_template_name),
		convertToTemplateArgInfo(template_args_to_use)
	);
	
	// Create StructTypeInfo
	auto struct_info = std::make_unique<StructTypeInfo>(instantiated_name, AccessSpecifier::Public);
	struct_info->is_union = class_decl.is_union();

	// Handle base classes from the primary template
	// Base classes need to be instantiated with concrete template arguments
	FLASH_LOG(Templates, Debug, "Primary template has ", class_decl.base_classes().size(), " base classes");
	for (const auto& base : class_decl.base_classes()) {
		std::string_view base_class_name = base.name;
		FLASH_LOG(Templates, Debug, "Processing primary template base class: ", base_class_name);
		
		// Check if this base class is deferred (a template parameter)
		if (base.is_deferred) {
			FLASH_LOG(Templates, Debug, "Base class '", base_class_name, "' is a template parameter - resolving with concrete type");
			
			// Find the template parameter by name and substitute with the concrete type
			size_t arg_index = 0;
			bool found = false;
			for (size_t i = 0; i < template_params.size(); ++i) {
				if (!template_params[i].is<TemplateParameterNode>()) continue;
				
				const auto& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.kind() != TemplateParameterKind::Type) continue;
				
				std::string_view param_name = tparam.name();
				
				if (param_name == base_class_name) {
					// Found the template parameter - get the concrete type
					if (arg_index < template_args_to_use.size()) {
						const TemplateTypeArg& concrete_arg = template_args_to_use[arg_index];
						
						// Validate that the concrete type is a struct/class
						if (concrete_arg.type_index >= gTypeInfo.size()) {
							FLASH_LOG(Templates, Error, "Template argument for base class has invalid type_index: ", concrete_arg.type_index);
							break;
						}
						
						const TypeInfo& concrete_type = gTypeInfo[concrete_arg.type_index];
						if (concrete_type.type_ != Type::Struct) {
							FLASH_LOG(Templates, Error, "Template argument '", concrete_type.name_, "' for base class must be a struct/class type");
							// Could return error here, but for now just log and skip
							break;
						}
						
						// Check if the concrete type is final
						if (concrete_type.struct_info_ && concrete_type.struct_info_->is_final) {
							FLASH_LOG(Templates, Error, "Cannot inherit from final class '", concrete_type.name_, "'");
							// Could return error here, but for now just log and skip
							break;
						}
						
						// Add the resolved base class
						struct_info->addBaseClass(StringTable::getStringView(concrete_type.name_), concrete_arg.type_index, base.access, base.is_virtual);
						FLASH_LOG(Templates, Debug, "Resolved template parameter base '", base_class_name, "' to concrete type '", StringTable::getStringView(concrete_type.name_), "' with type_index=", concrete_arg.type_index);
						found = true;
					}
					break;
				}
				
				// Track regular parameters to match indices
				if (!tparam.is_variadic()) {
					arg_index++;
				}
			}
			
			if (!found) {
				FLASH_LOG(Templates, Warning, "Could not resolve template parameter base class: ", base_class_name);
			}
		} else {
			// Regular (non-deferred) base class
			// Look up the base class type (may need to resolve type aliases)
			auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base_class_name));
			if (base_type_it != gTypesByName.end()) {
				const TypeInfo* base_type_info = base_type_it->second;
				struct_info->addBaseClass(base_class_name, base_type_info->type_index_, base.access, base.is_virtual);
				FLASH_LOG(Templates, Debug, "Added base class: ", base_class_name, " with type_index=", base_type_info->type_index_);
			} else {
				FLASH_LOG(Templates, Warning, "Base class ", base_class_name, " not found in gTypesByName");
			}
		}
	}

	// Handle deferred template base classes (with dependent template arguments)
	FLASH_LOG_FORMAT(Templates, Debug, "Template '{}' has {} deferred template base classes", 
	                 StringTable::getStringView(class_decl.name()), class_decl.deferred_template_base_classes().size());
	if (!class_decl.deferred_template_base_classes().empty()) {
		ensure_substitution_maps();
		auto identifier_matches = [](std::string_view haystack, std::string_view needle) {
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
		
		for (const auto& deferred_base : class_decl.deferred_template_base_classes()) {
			FLASH_LOG_FORMAT(Templates, Debug, "Processing deferred template base '{}' with {} template args",
			                 StringTable::getStringView(deferred_base.base_template_name), deferred_base.template_arguments.size());
			std::vector<TemplateTypeArg> resolved_args;
			bool unresolved_arg = false;
			for (const auto& arg_info : deferred_base.template_arguments) {
				// Pack expansion handling
				if (arg_info.is_pack) {
					// If the argument node references a template parameter pack, expand it
					if (arg_info.node.is<ExpressionNode>()) {
						const ExpressionNode& expr = arg_info.node.as<ExpressionNode>();
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							StringHandle pack_name = std::get<TemplateParameterReferenceNode>(expr).param_name();
							auto pack_it = pack_substitution_map.find(pack_name);
							if (pack_it != pack_substitution_map.end()) {
								resolved_args.insert(resolved_args.end(), pack_it->second.begin(), pack_it->second.end());
								continue;
							} else if (!template_args_to_use.empty()) {
								resolved_args.insert(resolved_args.end(), template_args_to_use.begin(), template_args_to_use.end());
								continue;
							}
						}
						// Also handle IdentifierNode - it may represent a pack parameter that wasn't converted to TemplateParameterReferenceNode
						else if (std::holds_alternative<IdentifierNode>(expr)) {
							const IdentifierNode& id = std::get<IdentifierNode>(expr);
							StringHandle pack_name = StringTable::getOrInternStringHandle(id.name());
							auto pack_it = pack_substitution_map.find(pack_name);
							if (pack_it != pack_substitution_map.end()) {
								resolved_args.insert(resolved_args.end(), pack_it->second.begin(), pack_it->second.end());
								continue;
							} else if (!template_args_to_use.empty()) {
								resolved_args.insert(resolved_args.end(), template_args_to_use.begin(), template_args_to_use.end());
								continue;
							}
						}
					} else if (arg_info.node.is<TypeSpecifierNode>()) {
						const TypeSpecifierNode& type_spec = arg_info.node.as<TypeSpecifierNode>();
						TypeIndex idx = type_spec.type_index();
						if (idx < gTypeInfo.size()) {
							StringHandle pack_name = gTypeInfo[idx].name_;
							auto pack_it = pack_substitution_map.find(pack_name);
							if (pack_it != pack_substitution_map.end()) {
								resolved_args.insert(resolved_args.end(), pack_it->second.begin(), pack_it->second.end());
								continue;
							} else if (!template_args_to_use.empty()) {
								resolved_args.insert(resolved_args.end(), template_args_to_use.begin(), template_args_to_use.end());
								continue;
							}
						}
					}
				}
				
				// Resolve dependent type arguments
				if (arg_info.node.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_spec = arg_info.node.as<TypeSpecifierNode>();
					Type resolved_type = type_spec.type();
					TypeIndex resolved_index = type_spec.type_index();
					bool resolved = false;
					
					if ((resolved_type == Type::UserDefined || resolved_type == Type::Struct) && resolved_index < gTypeInfo.size()) {
						std::string_view type_name = StringTable::getStringView(gTypeInfo[resolved_index].name());
						auto subst_it = name_substitution_map.find(type_name);
						if (subst_it != name_substitution_map.end()) {
							TemplateTypeArg subst = subst_it->second;
							subst.pointer_depth = type_spec.pointer_depth();
							subst.is_reference = type_spec.is_reference();
							subst.is_rvalue_reference = type_spec.is_rvalue_reference();
							subst.cv_qualifier = type_spec.cv_qualifier();
							resolved_args.push_back(subst);
							resolved = true;
						} else {
							// Check if this is a template class that needs to be instantiated with substituted args
							// For example: is_integral<T> where T needs to be substituted with int
							auto template_entry = gTemplateRegistry.lookupTemplate(type_name);
							if (template_entry.has_value()) {
								// This is a template class - try to instantiate it with our template args
								// The template args for the nested template should be our current template args
								// (e.g., is_integral<T> with T=int should become is_integral_int)
								FLASH_LOG(Templates, Debug, "Nested template lookup found '", type_name, 
								          "', attempting instantiation with ", template_args_to_use.size(), " args");
								auto instantiated = try_instantiate_class_template(type_name, template_args_to_use);
								if (instantiated.has_value() && instantiated->is<StructDeclarationNode>()) {
									ast_nodes_.push_back(*instantiated);
								}
								std::string_view inst_name = get_instantiated_class_name(type_name, template_args_to_use);
								auto inst_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
								if (inst_it != gTypesByName.end()) {
									TemplateTypeArg inst_arg;
									inst_arg.base_type = Type::Struct;
									inst_arg.type_index = inst_it->second->type_index_;
									inst_arg.pointer_depth = type_spec.pointer_depth();
									inst_arg.is_reference = type_spec.is_reference();
									inst_arg.is_rvalue_reference = type_spec.is_rvalue_reference();
									inst_arg.cv_qualifier = type_spec.cv_qualifier();
									resolved_args.push_back(inst_arg);
									resolved = true;
									FLASH_LOG_FORMAT(Templates, Debug, "Resolved nested template '{}' to '{}'", type_name, inst_name);
								}
							}
							
							if (!resolved) {
								for (const auto& subst_entry : name_substitution_map) {
									if (identifier_matches(type_name, subst_entry.first)) {
										TemplateTypeArg subst = subst_entry.second;
										subst.pointer_depth = type_spec.pointer_depth();
										subst.is_reference = type_spec.is_reference();
										subst.is_rvalue_reference = type_spec.is_rvalue_reference();
										subst.cv_qualifier = type_spec.cv_qualifier();
										resolved_args.push_back(subst);
										resolved = true;
										break;
									}
								}
							}
						}
					}
					
					// Fallback: use the type specifier as-is
					if (!resolved) {
						resolved_args.emplace_back(type_spec);
						resolved_args.back().is_pack = arg_info.is_pack;
					}
					continue;
				}
				
				if (arg_info.node.is<ExpressionNode>()) {
					const ExpressionNode& expr = arg_info.node.as<ExpressionNode>();
					
					// Handle TemplateParameterReferenceNode - substitute template parameter with actual type
					if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
						const auto& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
						std::string_view param_name = tparam_ref.param_name().view();
						auto subst_it = name_substitution_map.find(param_name);
						if (subst_it != name_substitution_map.end()) {
							TemplateTypeArg subst_arg = subst_it->second;
							subst_arg.is_pack = arg_info.is_pack;
							resolved_args.push_back(subst_arg);
							FLASH_LOG_FORMAT(Templates, Debug, "Substituted template parameter '{}' with type_index {} in deferred base",
							                 param_name, subst_it->second.type_index);
							continue;
						} else {
							FLASH_LOG_FORMAT(Templates, Debug, "Template parameter '{}' not found in substitution map", param_name);
							// Template parameter not found in substitution - this is an unresolved dependency
							unresolved_arg = true;
							break;
						}
					}
					
					// Special handling for TypeTraitExprNode - need to substitute template parameters
					if (std::holds_alternative<TypeTraitExprNode>(expr)) {
						const TypeTraitExprNode& trait_expr = std::get<TypeTraitExprNode>(expr);
						
						// Create a substituted version of the type trait
						if (trait_expr.has_type()) {
							const TypeSpecifierNode& type_spec = trait_expr.type_node().as<TypeSpecifierNode>();
							
							// Check if the type needs substitution
							Type base_type = type_spec.type();
							TypeIndex type_idx = type_spec.type_index();
							[[maybe_unused]] bool substituted = false;
							TypeSpecifierNode substituted_type_spec = type_spec;
							
							if ((base_type == Type::UserDefined || base_type == Type::Struct) && type_idx < gTypeInfo.size()) {
								std::string_view type_name = StringTable::getStringView(gTypeInfo[type_idx].name());
								auto subst_it = name_substitution_map.find(type_name);
								if (subst_it != name_substitution_map.end()) {
									// Substitute the type
									const TemplateTypeArg& subst = subst_it->second;
									substituted_type_spec = TypeSpecifierNode(
										subst.base_type,
										subst.type_index,
										0,  // size will be looked up
										Token(),
										type_spec.cv_qualifier()
									);
									substituted = true;
									FLASH_LOG_FORMAT(Templates, Debug, "Substituted type '{}' with type_index {} for type trait evaluation",
										type_name, subst.type_index);
								}
							}
							
							// Create substituted type trait node and evaluate
							ASTNode subst_type_node = emplace_node<TypeSpecifierNode>(substituted_type_spec);
							ASTNode subst_trait_node = emplace_node<ExpressionNode>(
								TypeTraitExprNode(trait_expr.kind(), subst_type_node, trait_expr.trait_token()));
							
							if (auto value = try_evaluate_constant_expression(subst_trait_node)) {
								TemplateTypeArg val_arg(value->value, value->type);
								val_arg.is_pack = arg_info.is_pack;
								resolved_args.push_back(val_arg);
								continue;
							}
						}
					} else if (std::holds_alternative<FunctionCallNode>(expr)) {
						// Handle constexpr function calls like: call_is_nt<Result>(typename Result::__invoke_type{})
						// These need template parameter substitution before evaluation
						const FunctionCallNode& func_call = std::get<FunctionCallNode>(expr);
						
						FLASH_LOG(Templates, Debug, "Processing FunctionCallNode in deferred base argument");
						
						// Check if the function has template arguments that need substitution
						bool has_dependent_template_args = false;
						std::vector<TemplateTypeArg> substituted_func_template_args;
						
						if (func_call.has_template_arguments()) {
							for (const ASTNode& targ_node : func_call.template_arguments()) {
								if (targ_node.is<ExpressionNode>()) {
									const ExpressionNode& targ_expr = targ_node.as<ExpressionNode>();
									if (std::holds_alternative<TemplateParameterReferenceNode>(targ_expr)) {
										const auto& tparam_ref = std::get<TemplateParameterReferenceNode>(targ_expr);
										std::string_view param_name = tparam_ref.param_name().view();
										auto subst_it = name_substitution_map.find(param_name);
										if (subst_it != name_substitution_map.end()) {
											substituted_func_template_args.push_back(subst_it->second);
											FLASH_LOG_FORMAT(Templates, Debug, "Substituted function template arg '{}' with type_index {}", 
											                 param_name, subst_it->second.type_index);
										} else {
											has_dependent_template_args = true;
										}
									} else if (std::holds_alternative<IdentifierNode>(targ_expr)) {
										const auto& id = std::get<IdentifierNode>(targ_expr);
										auto subst_it = name_substitution_map.find(id.name());
										if (subst_it != name_substitution_map.end()) {
											substituted_func_template_args.push_back(subst_it->second);
											FLASH_LOG_FORMAT(Templates, Debug, "Substituted function template arg identifier '{}' with type_index {}", 
											                 id.name(), subst_it->second.type_index);
										} else {
											has_dependent_template_args = true;
										}
									} else {
										// Keep the argument as-is for other expression types
										has_dependent_template_args = true;
									}
								} else if (targ_node.is<TypeSpecifierNode>()) {
									const TypeSpecifierNode& type_spec = targ_node.as<TypeSpecifierNode>();
									if (type_spec.type() == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
										std::string_view type_name = StringTable::getStringView(gTypeInfo[type_spec.type_index()].name());
										auto subst_it = name_substitution_map.find(type_name);
										if (subst_it != name_substitution_map.end()) {
											substituted_func_template_args.push_back(subst_it->second);
										} else {
											// Keep as-is
											substituted_func_template_args.emplace_back(type_spec);
										}
									} else {
										substituted_func_template_args.emplace_back(type_spec);
									}
								}
							}
						}
						
						// If we successfully substituted all template arguments, try to instantiate and call the function
						if (!has_dependent_template_args && !substituted_func_template_args.empty()) {
							std::string_view func_name = func_call.called_from().value();
							FLASH_LOG_FORMAT(Templates, Debug, "Trying to instantiate constexpr function '{}' with {} template args",
							                 func_name, substituted_func_template_args.size());
							
							// Try to instantiate the template function
							auto instantiated_func = try_instantiate_template_explicit(func_name, substituted_func_template_args);
							
							if (instantiated_func.has_value()) {
								FLASH_LOG_FORMAT(Templates, Debug, "try_instantiate_template_explicit returned node, is FunctionDeclarationNode: {}",
								                 instantiated_func->is<FunctionDeclarationNode>());
							} else {
								FLASH_LOG(Templates, Debug, "try_instantiate_template_explicit returned nullopt");
							}
							
							if (instantiated_func.has_value() && instantiated_func->is<FunctionDeclarationNode>()) {
								const FunctionDeclarationNode& func_decl = instantiated_func->as<FunctionDeclarationNode>();
								
								FLASH_LOG_FORMAT(Templates, Debug, "Instantiated function: is_constexpr={}, has_definition={}",
								                 func_decl.is_constexpr(), func_decl.get_definition().has_value());
								
								// Check if the function is constexpr
								if (func_decl.is_constexpr()) {
									// For constexpr functions that return a constant value, we can evaluate them
									// Look for a simple return statement with a constant value
									// This is a simplified constexpr evaluation - full constexpr requires an interpreter
									
									// For now, if the function body is just "return true;" or "return false;", we can evaluate it
									// This handles the common type_traits pattern
									if (func_decl.get_definition().has_value()) {
										const ASTNode& body_node = *func_decl.get_definition();
										FLASH_LOG_FORMAT(Templates, Debug, "Function body is BlockNode: {}", body_node.is<BlockNode>());
										if (body_node.is<BlockNode>()) {
											const BlockNode& block = body_node.as<BlockNode>();
											FLASH_LOG_FORMAT(Templates, Debug, "Block has {} statements", block.get_statements().size());
											if (block.get_statements().size() == 1) {
												const ASTNode& stmt = block.get_statements()[0];
												FLASH_LOG_FORMAT(Templates, Debug, "First statement is ReturnStatementNode: {}", stmt.is<ReturnStatementNode>());
												if (stmt.is<ReturnStatementNode>()) {
													const ReturnStatementNode& ret_stmt = stmt.as<ReturnStatementNode>();
													if (ret_stmt.expression().has_value()) {
														// Try to evaluate the return expression as a constant
														if (auto ret_value = try_evaluate_constant_expression(*ret_stmt.expression())) {
															FLASH_LOG_FORMAT(Templates, Debug, "Evaluated constexpr function call to value {}", ret_value->value);
															TemplateTypeArg val_arg(ret_value->value, ret_value->type);
															val_arg.is_pack = arg_info.is_pack;
															resolved_args.push_back(val_arg);
															continue;
														}
													}
												}
											}
										}
									}
								}
							}
						}
						
						// Fallback: try to evaluate the expression directly
						if (auto value = try_evaluate_constant_expression(arg_info.node)) {
							TemplateTypeArg val_arg(value->value, value->type);
							val_arg.is_pack = arg_info.is_pack;
							resolved_args.push_back(val_arg);
							continue;
						}
					} else if (std::holds_alternative<BinaryOperatorNode>(expr) || std::holds_alternative<TernaryOperatorNode>(expr)) {
						// Handle binary/ternary operator expressions like: R1<T>::num < R2<T>::num
// These need template parameter substitution before evaluation
						FLASH_LOG(Templates, Debug, "Processing BinaryOperatorNode/TernaryOperatorNode in deferred base argument");
						
						// Use ExpressionSubstitutor to substitute template parameters
						ExpressionSubstitutor substitutor(name_substitution_map, *this, template_param_order);
						ASTNode substituted_node = substitutor.substitute(arg_info.node);
						
						// Now try to evaluate the substituted expression
						if (auto value = try_evaluate_constant_expression(substituted_node)) {
							FLASH_LOG_FORMAT(Templates, Debug, "Evaluated substituted binary/ternary operator to value {}", value->value);
							TemplateTypeArg val_arg(value->value, value->type);
							val_arg.is_pack = arg_info.is_pack;
							resolved_args.push_back(val_arg);
							continue;
						} else {
							FLASH_LOG(Templates, Debug, "Failed to evaluate substituted binary/ternary operator");
						}
					} else if (std::holds_alternative<UnaryOperatorNode>(expr)) {
						// Handle unary operator expressions like: -Num<T>::num
						// These need template parameter substitution before evaluation
						FLASH_LOG(Templates, Debug, "Processing UnaryOperatorNode in deferred base argument");
						
						// Use ExpressionSubstitutor to substitute template parameters
						ExpressionSubstitutor substitutor(name_substitution_map, *this);
						ASTNode substituted_node = substitutor.substitute(arg_info.node);
						
						// Now try to evaluate the substituted expression
						if (auto value = try_evaluate_constant_expression(substituted_node)) {
							FLASH_LOG_FORMAT(Templates, Debug, "Evaluated substituted unary operator to value {}", value->value);
							TemplateTypeArg val_arg(value->value, value->type);
							val_arg.is_pack = arg_info.is_pack;
							resolved_args.push_back(val_arg);
							continue;
						} else {
							FLASH_LOG(Templates, Debug, "Failed to evaluate substituted unary operator");
						}
					} else {
						// Try to evaluate non-type template argument after substitution
						if (auto value = try_evaluate_constant_expression(arg_info.node)) {
							TemplateTypeArg val_arg(value->value, value->type);
							val_arg.is_pack = arg_info.is_pack;
							resolved_args.push_back(val_arg);
							continue;
						}
					}
				}
				
				// This is expected for dependent types in template metaprogramming
				// The template may still work correctly with the fallback path
				FLASH_LOG(Templates, Debug, "Could not resolve deferred template base argument for '",
				          StringTable::getStringView(deferred_base.base_template_name), "'; skipping base instantiation");
				unresolved_arg = true;
				break;
			}
			
			if (unresolved_arg) {
				// Cannot resolve all template arguments for the base class - skip it
				// Don't try to instantiate with wrong arguments as it will cause errors/crashes
				FLASH_LOG(Templates, Debug, "Skipping deferred base '", 
				          StringTable::getStringView(deferred_base.base_template_name), 
				          "' due to unresolved template arguments");
				continue;
			}
			
			std::string_view base_template_name = StringTable::getStringView(deferred_base.base_template_name);
			std::string_view outer_instantiated_name = instantiate_and_register_base_template(base_template_name, resolved_args);
			if (!outer_instantiated_name.empty()) {
				base_template_name = outer_instantiated_name;
			}
			
			std::string_view final_base_name = base_template_name;
			if (deferred_base.member_type.has_value()) {
				std::string_view member_name = StringTable::getStringView(*deferred_base.member_type);
				
				StringBuilder alias_builder;
				alias_builder.append(base_template_name);
				static constexpr std::string_view kScopeSeparator = "::"sv;
				alias_builder.append(kScopeSeparator);
				alias_builder.append(member_name);
				std::string_view alias_name = alias_builder.commit();
				
				auto alias_it = gTypesByName.find(StringTable::getOrInternStringHandle(alias_name));
				if (alias_it == gTypesByName.end()) {
					// Try looking up through inheritance (e.g., __or_<...>::type where type is inherited)
					const TypeInfo* inherited_alias = lookup_inherited_type_alias(base_template_name, member_name);
					if (inherited_alias == nullptr) {
						// This can happen when templates are instantiated with void/dependent arguments
						// during template metaprogramming (e.g., SFINAE). The code may still compile
						// and run correctly, so log at Debug level rather than Error.
						FLASH_LOG(Templates, Debug, "Deferred template base alias not found: ", alias_name,
						          " (this may be expected for SFINAE/dependent template arguments)");
						continue;
					}
					// The inherited_alias is a type alias - resolve it to the underlying type
					// If type_index_ is valid, use it to get the actual type name
					if (inherited_alias->type_index_ < gTypeInfo.size()) {
						const TypeInfo& underlying_type = gTypeInfo[inherited_alias->type_index_];
						final_base_name = StringTable::getStringView(underlying_type.name());
					} else {
						// Fallback: use the alias name if type_index is invalid
						final_base_name = StringTable::getStringView(inherited_alias->name());
					}
					struct_info->addBaseClass(final_base_name, inherited_alias->type_index_, deferred_base.access, deferred_base.is_virtual);
					FLASH_LOG_FORMAT(Templates, Debug, "Resolved deferred inherited member alias base to {}", final_base_name);
					continue;
				}
				
				final_base_name = alias_name;
				struct_info->addBaseClass(final_base_name, alias_it->second->type_index_, deferred_base.access, deferred_base.is_virtual);
				continue;
			}
			
			auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(final_base_name));
			if (base_type_it != gTypesByName.end()) {
				struct_info->addBaseClass(final_base_name, base_type_it->second->type_index_, deferred_base.access, deferred_base.is_virtual);
			} else {
				FLASH_LOG(Templates, Warning, "Deferred template base type not found: ", final_base_name);
			}
		}
	}

	// Handle deferred base classes (decltype bases) from the primary template
	// These need to be evaluated with concrete template arguments
	FLASH_LOG(Templates, Debug, "Primary template has ", class_decl.deferred_base_classes().size(), " deferred base classes");
	for (const auto& deferred_base : class_decl.deferred_base_classes()) {
		FLASH_LOG(Templates, Debug, "Processing deferred decltype base class");
		
		// The deferred base contains an expression that needs to be evaluated
		// with concrete template arguments to determine the actual base class
		if (!deferred_base.decltype_expression.is<TypeSpecifierNode>()) {
			// Build maps from template parameter NAME to concrete type for substitution
			// Note: We can't use type_index because template parameters are cleaned up after parsing
			ensure_substitution_maps();
			
			// Use ExpressionSubstitutor to perform template parameter substitution
			FLASH_LOG(Templates, Debug, "Using ExpressionSubstitutor to substitute template parameters in decltype expression");
			ExpressionSubstitutor substitutor(name_substitution_map, pack_substitution_map, *this, template_param_order);
			ASTNode substituted_expr = substitutor.substitute(deferred_base.decltype_expression);
			
			auto type_spec_opt = get_expression_type(substituted_expr);
			if (type_spec_opt.has_value()) {
				const TypeSpecifierNode& base_type_spec = *type_spec_opt;
				
				// Get the type information from the evaluated expression
				Type base_type = base_type_spec.type();
				TypeIndex base_type_index = base_type_spec.type_index();
				
				// Look up the base class type by its type index
				if (base_type == Type::Struct && base_type_index < gTypeInfo.size()) {
					const TypeInfo& base_type_info = gTypeInfo[base_type_index];
					std::string_view base_class_name = StringTable::getStringView(base_type_info.name());
					
					// Add the base class to the instantiated struct
					struct_info->addBaseClass(base_class_name, base_type_index, deferred_base.access, deferred_base.is_virtual);
					FLASH_LOG(Templates, Debug, "Added deferred base class: ", base_class_name, " with type_index=", base_type_index);
				} else {
					FLASH_LOG(Templates, Warning, "Deferred base class type is not a struct or invalid type_index=", base_type_index);
					FLASH_LOG(Templates, Warning, "This likely means template parameter substitution in decltype expressions is needed");
					FLASH_LOG(Templates, Warning, "For decltype(base_trait<T>()), we need to instantiate base_trait with concrete type");
				}
			} else {
				FLASH_LOG(Templates, Warning, "Could not evaluate deferred decltype base class expression");
			}
		} else if (deferred_base.decltype_expression.is<TypeSpecifierNode>()) {
			// Legacy path - if it's already a TypeSpecifierNode
			const TypeSpecifierNode& base_type_spec = deferred_base.decltype_expression.as<TypeSpecifierNode>();
			
			// Get the type information from the decltype expression
			Type base_type = base_type_spec.type();
			TypeIndex base_type_index = base_type_spec.type_index();
			
			// Look up the base class type by its type index
			if (base_type == Type::Struct && base_type_index < gTypeInfo.size()) {
				const TypeInfo& base_type_info = gTypeInfo[base_type_index];
				std::string_view base_class_name = StringTable::getStringView(base_type_info.name());
				
				// Add the base class to the instantiated struct
				struct_info->addBaseClass(base_class_name, base_type_index, deferred_base.access, deferred_base.is_virtual);
				FLASH_LOG(Templates, Debug, "Added deferred base class: ", base_class_name, " with type_index=", base_type_index);
			} else {
				FLASH_LOG(Templates, Warning, "Deferred base class type is not a struct or invalid type_index=", base_type_index);
			}
		} else {
			FLASH_LOG(Templates, Warning, "Deferred base class expression is neither ExpressionNode nor TypeSpecifierNode");
		}
	}

	// Copy members from the template, substituting template parameters with concrete types
	for (const auto& member_decl : class_decl.members()) {
		const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();

		// Substitute template parameter if the member type is a template parameter
		auto [member_type, member_type_index] = substitute_template_parameter(
			type_spec, template_params, template_args_to_use
		);

		// WORKAROUND: If member type is a Struct or UserDefined that is actually a template (not an instantiation),
		// try to instantiate it with the current template arguments.
		// This handles cases like:
		//   template<typename T> struct TC { T val; };
		//   template<typename T> struct TD { TC<T> c; }; 
		// where TC<T> is stored as a dependent placeholder with Type::UserDefined.
		// We need to instantiate TC with the concrete args when instantiating TD.
		if ((member_type == Type::Struct || member_type == Type::UserDefined) && member_type_index < gTypeInfo.size()) {
			const TypeInfo& member_type_info = gTypeInfo[member_type_index];
			std::string_view member_struct_name = StringTable::getStringView(member_type_info.name());
			
			FLASH_LOG(Templates, Debug, "Member type_info: name='", member_struct_name, 
			          "', isTemplateInstantiation=", member_type_info.isTemplateInstantiation(),
			          ", hasStructInfo=", (member_type_info.getStructInfo() != nullptr),
			          ", total_size=", member_type_info.getStructInfo() ? member_type_info.getStructInfo()->total_size : 0);
			
			// Phase 6: Use TypeInfo::isTemplateInstantiation() instead of parsing $
			// Check if this is a template instantiation that needs instantiation
			// A template needs instantiation if it's a placeholder (no struct_info or total_size == 0)
			bool needs_instantiation = false;
			if (member_type_info.isTemplateInstantiation()) {
				// This is a template instantiation - check if it's already fully instantiated
				// Need to instantiate if: no struct_info OR struct_info exists but size is 0
				if (!member_type_info.getStructInfo() || member_type_info.getStructInfo()->total_size == 0) {
					// Not yet instantiated - get the base template name and instantiate
					member_struct_name = StringTable::getStringView(member_type_info.baseTemplateName());
					needs_instantiation = true;
					FLASH_LOG(Templates, Debug, "Member needs instantiation (placeholder with size=0 or no struct_info): base_name='", member_struct_name, "'");
				} else {
					FLASH_LOG(Templates, Debug, "Member already instantiated: ", member_struct_name, ", size=", 
					          member_type_info.getStructInfo() ? member_type_info.getStructInfo()->total_size : 0);
				}
			} else if (member_type_info.getStructInfo() && member_type_info.getStructInfo()->total_size == 0) {
				// This is a non-template struct with size 0 (shouldn't normally happen)
				needs_instantiation = true;
				FLASH_LOG(Templates, Debug, "Member needs instantiation (non-template, total_size=0): name='", member_struct_name, "'");
			}
			
			if (needs_instantiation) {
				// Try to instantiate with the current template arguments
				FLASH_LOG(Templates, Debug, "Instantiating member template: ", member_struct_name, " with ", template_args_to_use.size(), " args");
				auto inst_result = try_instantiate_class_template(member_struct_name, template_args_to_use);
				
				// If instantiation succeeded, look up the instantiated type
				std::string_view inst_name_view = get_instantiated_class_name(member_struct_name, template_args_to_use);
				std::string inst_name(inst_name_view);
				auto inst_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
				if (inst_type_it != gTypesByName.end()) {
					// Update member_type_index to point to the instantiated type
					member_type_index = inst_type_it->second->type_index_;
					// Update member_type to match the instantiated type's actual type
					// This ensures codegen knows it's a struct type (fixes Type::UserDefined issue)
					member_type = inst_type_it->second->type_;
				}
			}
		}

		// After template refactoring, instantiated templates may have Type::UserDefined
		// but gTypeInfo correctly stores them as Type::Struct. Synchronize member_type.
		if (member_type_index < gTypeInfo.size() && member_type_index > 0) {
			const TypeInfo& member_type_info = gTypeInfo[member_type_index];
			if (member_type_info.getStructInfo() && member_type == Type::UserDefined) {
				// Fix Type::UserDefined to Type::Struct for instantiated templates
				member_type = member_type_info.type_;
			}
		}

		// Handle array size substitution for non-type template parameters
		std::optional<ASTNode> substituted_array_size;
		if (decl.is_array()) {
			if (decl.array_size().has_value()) {
				ASTNode array_size_node = *decl.array_size();
				
				// The array size might be stored directly or wrapped in different node types
				// Try to extract the identifier or value from various possible representations
				std::optional<std::string_view> identifier_name;
				std::optional<int64_t> literal_value;
				
				// Check if it's an ExpressionNode
				if (array_size_node.is<ExpressionNode>()) {
					const ExpressionNode& expr = array_size_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(expr)) {
						const IdentifierNode& ident = std::get<IdentifierNode>(expr);
						identifier_name = ident.name();
					} else if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
						const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
						identifier_name = tparam_ref.param_name().view();
					} else if (std::holds_alternative<NumericLiteralNode>(expr)) {
						const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
						const auto& val = lit.value();
						if (std::holds_alternative<unsigned long long>(val)) {
							literal_value = static_cast<int64_t>(std::get<unsigned long long>(val));
						}
					}
				}
				// Check if it's a direct IdentifierNode (shouldn't happen, but be safe)
				else if (array_size_node.is<IdentifierNode>()) {
					const IdentifierNode& ident = array_size_node.as<IdentifierNode>();
					identifier_name = ident.name();
				}
				
				// If we found an identifier, try to substitute it with a non-type template parameter value
				if (identifier_name.has_value()) {
					// Try to find which non-type template parameter this is
					for (size_t i = 0; i < template_params.size(); ++i) {
						const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
						if (tparam.kind() == TemplateParameterKind::NonType && tparam.name() == *identifier_name) {
							// Found the non-type parameter - substitute with the actual value
							if (i < template_args_to_use.size() && template_args_to_use[i].is_value) {
								// Create a numeric literal node with the substituted value
								int64_t val = template_args_to_use[i].value;
								Token num_token(Token::Type::Literal, StringBuilder().append(val).commit(), 0, 0, 0);
								auto num_literal = emplace_node<ExpressionNode>(
									NumericLiteralNode(num_token, static_cast<unsigned long long>(val), Type::Int, TypeQualifier::None, 32)
								);
								substituted_array_size = num_literal;
								break;
							}
						}
					}
				}
			} else {
				FLASH_LOG(Templates, Error, "Array does NOT have array_size!");
			}
			
			// If we didn't substitute, keep the original array size
			if (!substituted_array_size.has_value()) {
				substituted_array_size = decl.array_size();
			}
		}

		// Create the substituted type specifier
		// IMPORTANT: Preserve the base CV qualifier from the original type!
		// For example: const T* should become const int* when T=int
		auto substituted_type = emplace_node<TypeSpecifierNode>(
			member_type,
			member_type_index,
			get_type_size_bits(member_type),
			Token(),
			type_spec.cv_qualifier()  // Preserve const/volatile qualifier
		);

		// Copy pointer levels from the original type specifier
		auto& substituted_type_spec = substituted_type.as<TypeSpecifierNode>();
		for (const auto& ptr_level : type_spec.pointer_levels()) {
			substituted_type_spec.add_pointer_level(ptr_level.cv_qualifier);
		}

		// Preserve reference qualifiers from the original type
		if (type_spec.is_rvalue_reference()) {
			substituted_type_spec.set_reference(true);  // true for rvalue reference
		} else if (type_spec.is_reference()) {
			substituted_type_spec.set_reference(false);  // false for lvalue reference
		}
		
		// Add to the instantiated struct
		// new_struct_ref.add_member(new_member_decl, member_decl.access, member_decl.default_initializer);

		// Calculate member size - for arrays, multiply element size by array size
		size_t member_size;
		if (substituted_array_size.has_value()) {
			// Extract the array size value
			size_t array_size = 1;
			const ASTNode& size_node = *substituted_array_size;
			if (size_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = size_node.as<ExpressionNode>();
				if (std::holds_alternative<NumericLiteralNode>(expr)) {
					const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
					const auto& val = lit.value();
					if (std::holds_alternative<unsigned long long>(val)) {
						array_size = static_cast<size_t>(std::get<unsigned long long>(val));
					}
				}
			}
			member_size = (get_type_size_bits(member_type) / 8) * array_size;
		} else {
			// Check if the ORIGINAL type is a pointer or reference (use original type_spec, not substituted member_type)
			if (type_spec.is_pointer() || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
				member_size = 8;  // Pointers and references are 64-bit on x64
			} else if (member_type == Type::Struct && member_type_index != 0) {
				// For struct types, look up the actual size in gTypeInfo
				const TypeInfo* member_struct_info = nullptr;
				for (const auto& ti : gTypeInfo) {
					if (ti.type_index_ == member_type_index) {
						member_struct_info = &ti;
						break;
					}
				}
				if (member_struct_info && member_struct_info->getStructInfo()) {
					member_size = member_struct_info->getStructInfo()->total_size;
					FLASH_LOG_FORMAT(Templates, Debug, "Primary template: Found struct member '{}' with type_index={}, total_size={} bytes, struct name={}", 
					                 decl.identifier_token().value(), member_type_index, member_size,
					                 StringTable::getStringView(member_struct_info->name()));
				} else {
					member_size = get_type_size_bits(member_type) / 8;
					FLASH_LOG_FORMAT(Templates, Debug, "Primary template: Struct member '{}' type_index={} not found in gTypeInfo, using default size={} bytes", 
					                 decl.identifier_token().value(), member_type_index, member_size);
				}
			} else {
				member_size = get_type_size_bits(member_type) / 8;
			}
		}

		// Calculate member alignment
		// For pointers and references, use 8-byte alignment (pointer alignment on x64)
		size_t member_alignment;
		if (type_spec.is_pointer() || type_spec.is_reference() || type_spec.is_rvalue_reference()) {
			member_alignment = 8;  // Pointer/reference alignment on x64
		} else if (member_type == Type::Struct && member_type_index != 0) {
			// For struct types, look up the actual alignment from gTypeInfo
			const TypeInfo* member_struct_info = nullptr;
			for (const auto& ti : gTypeInfo) {
				if (ti.type_index_ == member_type_index) {
					member_struct_info = &ti;
					break;
				}
			}
			if (member_struct_info && member_struct_info->getStructInfo()) {
				member_alignment = member_struct_info->getStructInfo()->alignment;
			} else {
				member_alignment = get_type_alignment(member_type, member_size);
			}
		} else {
			member_alignment = get_type_alignment(member_type, member_size);
		}
		bool is_ref_member = type_spec.is_reference();
		bool is_rvalue_ref_member = type_spec.is_rvalue_reference();
	
		// For reference members, we need to pass the size of the referenced type, not the pointer size
		size_t referenced_size_bits = 0;
		if (is_ref_member || is_rvalue_ref_member) {
			referenced_size_bits = get_type_size_bits(member_type);
		}
	
		// Substitute template parameters in default member initializers
		std::optional<ASTNode> substituted_default_initializer = substitute_default_initializer(
			member_decl.default_initializer, template_args_to_use, template_params);
	
		// Phase 7B: Intern member name and use StringHandle overload
		StringHandle member_name_handle = decl.identifier_token().handle();
		struct_info->addMember(
			member_name_handle,
			member_type,
			member_type_index,
			member_size,
			member_alignment,
			member_decl.access,
			substituted_default_initializer,
			is_ref_member,
			is_rvalue_ref_member,
			referenced_size_bits
		);
	}

	// Skip member function instantiation - we only need type information for nested classes
	// Member functions will be instantiated on-demand when called

	// Copy static members from the primary template with template parameter substitution
	// This handles cases like:
	//   template<bool... Bs> struct __and_ { static constexpr bool value = (Bs && ...); };
	// where the fold expression needs to be evaluated with the actual template arguments
	//
	// Note: Static members can be in two places:
	// 1. class_decl.static_members() - AST node storage
	// 2. StructTypeInfo for the template - type system storage
	// We need to check both.
	
	// First, try to get static members from the template's StructTypeInfo
	auto template_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(template_name));
	const StructTypeInfo* template_struct_info = nullptr;
	if (template_type_it != gTypesByName.end() && template_type_it->second->getStructInfo()) {
		template_struct_info = template_type_it->second->getStructInfo();
	}
	
	// Process static members from StructTypeInfo (preferred source)
	if (template_struct_info && !template_struct_info->static_members.empty()) {
		FLASH_LOG(Templates, Debug, "Processing ", template_struct_info->static_members.size(), " static members from primary template StructTypeInfo");
		
		// Helper to check if an initializer needs complex substitution
		// Returns true for fold expressions, sizeof...(pack), template parameter references, etc.
		auto needs_complex_substitution = [](const std::optional<ASTNode>& initializer) -> bool {
			if (!initializer.has_value() || !initializer->is<ExpressionNode>()) {
				return false;
			}
			const ExpressionNode& expr = initializer->as<ExpressionNode>();
			
			// Check for expression types that require template parameter substitution
			if (std::holds_alternative<FoldExpressionNode>(expr)) return true;
			if (std::holds_alternative<SizeofPackNode>(expr)) return true;
			if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) return true;
			
			// Check for static_cast wrapping sizeof...
			if (std::holds_alternative<StaticCastNode>(expr)) {
				const StaticCastNode& cast_node = std::get<StaticCastNode>(expr);
				if (cast_node.expr().is<ExpressionNode>()) {
					const ExpressionNode& inner = cast_node.expr().as<ExpressionNode>();
					if (std::holds_alternative<SizeofPackNode>(inner)) return true;
				}
			}
			
			// Check for BinaryOperatorNode that might contain sizeof...
			if (std::holds_alternative<BinaryOperatorNode>(expr)) return true;
			
			// Check for TernaryOperatorNode (condition might be template param)
			if (std::holds_alternative<TernaryOperatorNode>(expr)) return true;
			
			// Check for IdentifierNode (might be a template parameter)
			// Note: We'd need context to determine this, so be conservative
			if (std::holds_alternative<IdentifierNode>(expr)) return true;
			
			return false;
		};
		
		for (const auto& static_member : template_struct_info->static_members) {
			FLASH_LOG(Templates, Debug, "Copying static member: ", StringTable::getStringView(static_member.getName()));
			
			// Check if this static member should be lazily instantiated
			bool member_needs_complex_substitution = needs_complex_substitution(static_member.initializer);
			
			if (use_lazy_instantiation && member_needs_complex_substitution) {
				// Register for lazy instantiation instead of processing now
				FLASH_LOG(Templates, Debug, "Registering static member '", static_member.getName(), 
				          "' for lazy instantiation");
				
				LazyStaticMemberInfo lazy_info;
				lazy_info.class_template_name = StringTable::getOrInternStringHandle(template_name);
				lazy_info.instantiated_class_name = instantiated_name;
				lazy_info.member_name = static_member.getName();
				lazy_info.type = static_member.type;
				lazy_info.type_index = static_member.type_index;
				lazy_info.size = static_member.size;
				lazy_info.alignment = static_member.alignment;
				lazy_info.access = static_member.access;
				lazy_info.initializer = static_member.initializer;
				lazy_info.cv_qualifier = static_member.is_const ? CVQualifier::Const : CVQualifier::None;
				lazy_info.template_params = template_params;
				lazy_info.template_args = template_args_to_use;
				lazy_info.needs_substitution = true;
				
				LazyStaticMemberRegistry::getInstance().registerLazyStaticMember(lazy_info);
				
				// Still add the member to struct_info for name lookup, but without initializer
				// Type substitution is still done eagerly (for sizeof, alignof, etc.)
				TypeSpecifierNode original_type_spec(static_member.type, TypeQualifier::None, static_member.size * 8);
				original_type_spec.set_type_index(static_member.type_index);
				
				auto [substituted_type, substituted_type_index] = substitute_template_parameter(
					original_type_spec, template_params, template_args_to_use);
				
				size_t substituted_size = get_type_size_bits(substituted_type) / 8;
				
				// Add with nullopt initializer - will be filled in during lazy instantiation
				struct_info->addStaticMember(
					static_member.getName(),
					substituted_type,
					substituted_type_index,
					substituted_size,
					static_member.alignment,
					static_member.access,
					std::nullopt,  // Initializer will be computed lazily
					static_member.is_const
				);
				
				continue;  // Skip the eager processing below
			}
			
			// Eager processing path (when lazy is disabled or not needed)
			// Check if initializer needs substitution (e.g., fold expressions, template parameters)
			std::optional<ASTNode> substituted_initializer = static_member.initializer;
			if (static_member.initializer.has_value() && static_member.initializer->is<ExpressionNode>()) {
				const ExpressionNode& expr = static_member.initializer->as<ExpressionNode>();
				
				// Helper to calculate pack size for substitution
				auto calculate_pack_size = [&](std::string_view pack_name) -> std::optional<size_t> {
					FLASH_LOG(Templates, Debug, "Looking for pack: ", pack_name);
					for (size_t i = 0; i < template_params.size(); ++i) {
						const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
						FLASH_LOG(Templates, Debug, "  Checking param ", tparam.name(), " is_variadic=", tparam.is_variadic() ? "true" : "false");
						if (tparam.name() == pack_name && tparam.is_variadic()) {
							size_t non_variadic_count = 0;
							for (const auto& param : template_params) {
								if (!param.as<TemplateParameterNode>().is_variadic()) {
									non_variadic_count++;
								}
							}
							return template_args_to_use.size() - non_variadic_count;
						}
					}
					return std::nullopt;
				};
				
				// Helper to create a numeric literal from pack size
				auto make_pack_size_literal = [&](size_t pack_size) -> ASTNode {
					std::string_view pack_size_str = StringBuilder().append(pack_size).commit();
					Token num_token(Token::Type::Literal, pack_size_str, 0, 0, 0);
					return emplace_node<ExpressionNode>(
						NumericLiteralNode(num_token, static_cast<unsigned long long>(pack_size), Type::Int, TypeQualifier::None, 32)
					);
				};
				
				// Handle SizeofPackNode (e.g., static constexpr int value = sizeof...(Ts);)
				if (std::holds_alternative<SizeofPackNode>(expr)) {
					const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(expr);
					if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
						substituted_initializer = make_pack_size_literal(*pack_size);
						FLASH_LOG(Templates, Debug, "Substituted sizeof...(", sizeof_pack.pack_name(), ") with ", *pack_size);
					}
				}
				// Handle static_cast<T>(sizeof...(Ts)) patterns
				else if (std::holds_alternative<StaticCastNode>(expr)) {
					const StaticCastNode& cast_node = std::get<StaticCastNode>(expr);
					if (cast_node.expr().is<ExpressionNode>()) {
						const ExpressionNode& cast_inner = cast_node.expr().as<ExpressionNode>();
						if (std::holds_alternative<SizeofPackNode>(cast_inner)) {
							const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(cast_inner);
							if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
								substituted_initializer = make_pack_size_literal(*pack_size);
								FLASH_LOG(Templates, Debug, "Substituted static_cast of sizeof...(", sizeof_pack.pack_name(), ") with ", *pack_size);
							}
						}
					}
				}
				// Handle complex expressions containing sizeof... using ConstExpr::Evaluator
				// (e.g., static_cast<int>(sizeof...(Ts)) * 2 + 40, binary expressions, etc.)
				else if (std::holds_alternative<BinaryOperatorNode>(expr)) {
					// Recursive helper to substitute SizeofPackNode with numeric literals in an expression
					std::function<ASTNode(const ASTNode&)> substitute_sizeof_pack = [&](const ASTNode& node) -> ASTNode {
						if (!node.is<ExpressionNode>()) {
							return node;
						}
						const ExpressionNode& expr_node = node.as<ExpressionNode>();
						
						// Handle SizeofPackNode directly
						if (std::holds_alternative<SizeofPackNode>(expr_node)) {
							const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(expr_node);
							if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
								return make_pack_size_literal(*pack_size);
							}
							return node;
						}
						// Handle static_cast wrapping sizeof...
						if (std::holds_alternative<StaticCastNode>(expr_node)) {
							const StaticCastNode& cast_node = std::get<StaticCastNode>(expr_node);
							ASTNode substituted_inner = substitute_sizeof_pack(cast_node.expr());
							// If inner was substituted to a literal, just use the literal (static_cast has no effect)
							if (substituted_inner.is<ExpressionNode>()) {
								const ExpressionNode& inner_expr = substituted_inner.as<ExpressionNode>();
								if (std::holds_alternative<NumericLiteralNode>(inner_expr)) {
									return substituted_inner;
								}
							}
							return node;
						}
						// Handle BinaryOperatorNode - recursively substitute both sides
						if (std::holds_alternative<BinaryOperatorNode>(expr_node)) {
							const BinaryOperatorNode& bin_op = std::get<BinaryOperatorNode>(expr_node);
							ASTNode subst_lhs = substitute_sizeof_pack(bin_op.get_lhs());
							ASTNode subst_rhs = substitute_sizeof_pack(bin_op.get_rhs());
							// Create new binary operator with substituted operands
							BinaryOperatorNode& new_bin = gChunkedAnyStorage.emplace_back<BinaryOperatorNode>(
								bin_op.get_token(), subst_lhs, subst_rhs);
							return emplace_node<ExpressionNode>(new_bin);
						}
						return node;
					};
					
					// Substitute sizeof... in the expression
					ASTNode substituted_expr = substitute_sizeof_pack(static_member.initializer.value());
					
					// Now use ConstExpr::Evaluator to evaluate the expression
					ConstExpr::EvaluationContext eval_context(gSymbolTable);
					ConstExpr::EvalResult result = ConstExpr::Evaluator::evaluate(substituted_expr, eval_context);
					
					if (result.success()) {
						substituted_initializer = make_pack_size_literal(static_cast<size_t>(result.as_int()));
						FLASH_LOG(Templates, Debug, "Evaluated expression with sizeof... using ConstExpr::Evaluator to ", result.as_int());
					}
				}
				// Handle FoldExpressionNode (e.g., static constexpr bool value = (Bs && ...);)
				else if (std::holds_alternative<FoldExpressionNode>(expr)) {
					const FoldExpressionNode& fold = std::get<FoldExpressionNode>(expr);
					std::string_view pack_name = fold.pack_name();
					std::string_view op = fold.op();
					FLASH_LOG(Templates, Debug, "Static member initializer contains fold expression with pack: ", pack_name, " op: ", op);
					
					// Find the parameter pack in template parameters
					std::optional<size_t> pack_param_idx;
					for (size_t p = 0; p < template_params.size(); ++p) {
						const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
						if (tparam.name() == pack_name && tparam.is_variadic()) {
							pack_param_idx = p;
							break;
						}
					}
					
					if (pack_param_idx.has_value()) {
						// Collect the values from the variadic pack arguments
						std::vector<int64_t> pack_values;
						bool all_values_found = true;
						
						// For variadic packs, arguments after non-variadic parameters are the pack values
						size_t non_variadic_count = 0;
						for (const auto& param : template_params) {
							if (!param.as<TemplateParameterNode>().is_variadic()) {
								non_variadic_count++;
							}
						}
						
						for (size_t i = non_variadic_count; i < template_args_to_use.size() && all_values_found; ++i) {
							if (template_args_to_use[i].is_value) {
								pack_values.push_back(template_args_to_use[i].value);
								FLASH_LOG(Templates, Debug, "Pack value[", i - non_variadic_count, "] = ", template_args_to_use[i].value);
							} else {
								all_values_found = false;
							}
						}
						
						if (all_values_found && !pack_values.empty()) {
							auto fold_result = evaluate_fold_expression(op, pack_values);
							if (fold_result.has_value()) {
								substituted_initializer = *fold_result;
							}
						}
					}
				}
				// Handle TemplateParameterReferenceNode (e.g., static constexpr T value = v;)
				else if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
					const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
					FLASH_LOG(Templates, Debug, "Static member initializer contains template parameter reference: ", tparam_ref.param_name());
					if (auto subst = substitute_template_param_in_initializer(tparam_ref.param_name().view(), template_args_to_use, template_params)) {
						substituted_initializer = subst;
						FLASH_LOG(Templates, Debug, "Substituted template parameter '", tparam_ref.param_name(), "'");
					}
				}
				// Handle IdentifierNode that might be a template parameter
				else if (std::holds_alternative<IdentifierNode>(expr)) {
					const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
					std::string_view id_name = id_node.name();
					FLASH_LOG(Templates, Debug, "Static member initializer contains IdentifierNode: ", id_name);
					if (auto subst = substitute_template_param_in_initializer(id_name, template_args_to_use, template_params)) {
						substituted_initializer = subst;
						FLASH_LOG(Templates, Debug, "Substituted identifier '", id_name, "' (template parameter)");
					}
				}
				// Handle TernaryOperatorNode where the condition is a template parameter (e.g., IsArith ? 42 : 0)
				else if (std::holds_alternative<TernaryOperatorNode>(expr)) {
					const TernaryOperatorNode& ternary = std::get<TernaryOperatorNode>(expr);
					const ASTNode& cond_node = ternary.condition();
					
					// Check if condition is a template parameter reference or identifier
					if (cond_node.is<ExpressionNode>()) {
						const ExpressionNode& cond_expr = cond_node.as<ExpressionNode>();
						std::optional<int64_t> cond_value;
						
						if (std::holds_alternative<TemplateParameterReferenceNode>(cond_expr)) {
							const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(cond_expr);
							FLASH_LOG(Templates, Debug, "Ternary condition is template parameter: ", tparam_ref.param_name());
							
							// Look up the parameter value
							for (size_t p = 0; p < template_params.size(); ++p) {
								const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
								if (tparam.name() == tparam_ref.param_name() && tparam.kind() == TemplateParameterKind::NonType) {
									if (p < template_args_to_use.size() && template_args_to_use[p].is_value) {
										cond_value = template_args_to_use[p].value;
										FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
									}
									break;
								}
							}
						}
						else if (std::holds_alternative<IdentifierNode>(cond_expr)) {
							const IdentifierNode& id_node = std::get<IdentifierNode>(cond_expr);
							std::string_view id_name = id_node.name();
							FLASH_LOG(Templates, Debug, "Ternary condition is identifier: ", id_name);
							
							// Look up the identifier as a template parameter
							for (size_t p = 0; p < template_params.size(); ++p) {
								const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
								if (tparam.name() == id_name && tparam.kind() == TemplateParameterKind::NonType) {
									if (p < template_args_to_use.size() && template_args_to_use[p].is_value) {
										cond_value = template_args_to_use[p].value;
										FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
									}
									break;
								}
							}
						}
						
						// If we found the condition value, evaluate the ternary
						if (cond_value.has_value()) {
							const ASTNode& result_branch = (*cond_value != 0) ? ternary.true_expr() : ternary.false_expr();
							
							if (result_branch.is<ExpressionNode>()) {
								const ExpressionNode& result_expr = result_branch.as<ExpressionNode>();
								if (std::holds_alternative<NumericLiteralNode>(result_expr)) {
									const NumericLiteralNode& lit = std::get<NumericLiteralNode>(result_expr);
									const auto& val = lit.value();
									unsigned long long num_val = std::holds_alternative<unsigned long long>(val)
										? std::get<unsigned long long>(val)
										: static_cast<unsigned long long>(std::get<double>(val));
									
									// Create a new numeric literal with the evaluated result
									std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(num_val)).commit();
									Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
									substituted_initializer = emplace_node<ExpressionNode>(
										NumericLiteralNode(num_token, num_val, lit.type(), lit.qualifier(), lit.sizeInBits())
									);
									FLASH_LOG(Templates, Debug, "Evaluated ternary to: ", num_val);
								}
							}
						}
					}
				}
			}
			
			// Substitute type if it's a template parameter (e.g., "T" in "static constexpr T value = v;")
			// Create a TypeSpecifierNode from the static member's type info to use substitute_template_parameter
			TypeSpecifierNode original_type_spec(static_member.type, TypeQualifier::None, static_member.size * 8);
			original_type_spec.set_type_index(static_member.type_index);
			
			// Use substitute_template_parameter for consistent template parameter matching
			auto [substituted_type, substituted_type_index] = substitute_template_parameter(
				original_type_spec, template_params, template_args_to_use);
			
			// Calculate the substituted size based on the substituted type
			size_t substituted_size = get_type_size_bits(substituted_type) / 8;
			
			FLASH_LOG(Templates, Debug, "Static member type substitution: original type=", (int)static_member.type,
			          " -> substituted type=", (int)substituted_type, ", size=", substituted_size);
			
			struct_info->addStaticMember(
				static_member.getName(),
				substituted_type,
				substituted_type_index,
				substituted_size,
				static_member.alignment,
				static_member.access,
				substituted_initializer,
				static_member.is_const
			);
		}
	}
	// Fallback: Process static members from AST node (for patterns/specializations)
	else if (!class_decl.static_members().empty()) {
		FLASH_LOG(Templates, Debug, "Processing ", class_decl.static_members().size(), " static members from primary template AST node");
		for (const auto& static_member : class_decl.static_members()) {
			FLASH_LOG(Templates, Debug, "Copying static member: ", StringTable::getStringView(static_member.name));
			
			// Check if initializer needs substitution (e.g., fold expressions, template parameters)
			std::optional<ASTNode> substituted_initializer = static_member.initializer;
		if (static_member.initializer.has_value() && static_member.initializer->is<ExpressionNode>()) {
			const ExpressionNode& expr = static_member.initializer->as<ExpressionNode>();
			
			// Handle FoldExpressionNode (e.g., static constexpr bool value = (Bs && ...);)
			if (std::holds_alternative<FoldExpressionNode>(expr)) {
				const FoldExpressionNode& fold = std::get<FoldExpressionNode>(expr);
				std::string_view pack_name = fold.pack_name();
				std::string_view op = fold.op();
				FLASH_LOG(Templates, Debug, "Static member initializer contains fold expression with pack: ", pack_name, " op: ", op);
				
				// Find the parameter pack in template parameters
				std::optional<size_t> pack_param_idx;
				for (size_t p = 0; p < template_params.size(); ++p) {
					const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
					if (tparam.name() == pack_name && tparam.is_variadic()) {
						pack_param_idx = p;
						break;
					}
				}
				
				if (pack_param_idx.has_value()) {
					// Collect the values from the variadic pack arguments
					std::vector<int64_t> pack_values;
					bool all_values_found = true;
					
					// For variadic packs, arguments after non-variadic parameters are the pack values
					size_t non_variadic_count = 0;
					for (const auto& param : template_params) {
						if (!param.as<TemplateParameterNode>().is_variadic()) {
							non_variadic_count++;
						}
					}
					
					for (size_t i = non_variadic_count; i < template_args_to_use.size() && all_values_found; ++i) {
						if (template_args_to_use[i].is_value) {
							pack_values.push_back(template_args_to_use[i].value);
							FLASH_LOG(Templates, Debug, "Pack value[", i - non_variadic_count, "] = ", template_args_to_use[i].value);
						} else {
							all_values_found = false;
						}
					}
					
					if (all_values_found && !pack_values.empty()) {
						auto fold_result = evaluate_fold_expression(op, pack_values);
						if (fold_result.has_value()) {
							substituted_initializer = *fold_result;
						}
					}
				}
			}
			// Handle TemplateParameterReferenceNode (e.g., static constexpr T value = v;)
			else if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
				const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
				FLASH_LOG(Templates, Debug, "Static member initializer contains template parameter reference: ", tparam_ref.param_name());
				if (auto subst = substitute_template_param_in_initializer(tparam_ref.param_name().view(), template_args_to_use, template_params)) {
					substituted_initializer = subst;
					FLASH_LOG(Templates, Debug, "Substituted template parameter '", tparam_ref.param_name(), "'");
				}
			}
			// Handle IdentifierNode that might be a template parameter
			else if (std::holds_alternative<IdentifierNode>(expr)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
				std::string_view id_name = id_node.name();
				FLASH_LOG(Templates, Debug, "Static member initializer contains IdentifierNode: ", id_name);
				if (auto subst = substitute_template_param_in_initializer(id_name, template_args_to_use, template_params)) {
					substituted_initializer = subst;
					FLASH_LOG(Templates, Debug, "Substituted identifier '", id_name, "' (template parameter)");
				}
			}
		}
		
		struct_info->addStaticMember(
			static_member.name,
			static_member.type,
			static_member.type_index,
			static_member.size,
			static_member.alignment,
			static_member.access,
			substituted_initializer,
			static_member.is_const
		);
		}
	}

	// Copy nested classes from the template with template parameter substitution
	for (const auto& nested_class : class_decl.nested_classes()) {
		if (nested_class.is<StructDeclarationNode>()) {
			const StructDeclarationNode& nested_struct = nested_class.as<StructDeclarationNode>();
			auto qualified_name = StringTable::getOrInternStringHandle(StringBuilder().append(instantiated_name).append("::"sv).append(nested_struct.name()));
			
			// Create a new StructTypeInfo for the nested class
			auto nested_struct_info = std::make_unique<StructTypeInfo>((qualified_name), nested_struct.default_access());
			
			// Copy and substitute members from the nested class
			for (const auto& member_decl : nested_struct.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
				
				// Use substitute_template_parameter for consistent template parameter matching
				// This handles pointer levels, qualifiers, and all type transformations correctly
				auto [substituted_type, substituted_type_index] = substitute_template_parameter(
					type_spec, template_params, template_args_to_use);
				
				// Create a substituted type specifier with the substituted type
				TypeSpecifierNode substituted_type_spec(
					substituted_type,
					type_spec.qualifier(),
					get_type_size_bits(substituted_type),
					Token()  // Empty token
				);
				substituted_type_spec.set_type_index(substituted_type_index);
				
				// Copy pointer levels from the original type specifier
				for (const auto& ptr_level : type_spec.pointer_levels()) {
					substituted_type_spec.add_pointer_level(ptr_level.cv_qualifier);
				}
				
				// Add member to the nested struct info
				// For pointers, use pointer size (64 bits on x64), otherwise use type size
				size_t member_size;
				if (substituted_type_spec.is_pointer()) {
					member_size = 8;  // 64-bit pointer
				} else {
					member_size = substituted_type_spec.size_in_bits() / 8;
				}
				size_t member_alignment = get_type_alignment(substituted_type_spec.type(), member_size);
				
				bool is_ref_member = substituted_type_spec.is_reference();
				bool is_rvalue_ref_member = substituted_type_spec.is_rvalue_reference();
				// Phase 7B: Intern member name and use StringHandle overload
				StringHandle member_name_handle = decl.identifier_token().handle();
				nested_struct_info->addMember(
					member_name_handle,
					substituted_type_spec.type(),
					substituted_type_spec.type_index(),
					member_size,
					member_alignment,
					member_decl.access,
					member_decl.default_initializer,
					is_ref_member,
					is_rvalue_ref_member,
					(is_ref_member || is_rvalue_ref_member) ? get_type_size_bits(substituted_type_spec.type()) : 0
				);
			}
			
			// Copy static members from the original nested struct
			// Look up the original nested struct by building its qualified name from the template
			StringBuilder original_nested_name_builder;
			original_nested_name_builder.append(template_name).append("::"sv).append(nested_struct.name());
			std::string_view original_nested_name = original_nested_name_builder.commit();
			
			FLASH_LOG(Templates, Debug, "Looking for original nested class: ", original_nested_name);
			auto original_nested_it = gTypesByName.find(StringTable::getOrInternStringHandle(original_nested_name));
			if (original_nested_it != gTypesByName.end()) {
				const TypeInfo* original_nested_type = original_nested_it->second;
				FLASH_LOG(Templates, Debug, "Found original nested class, checking struct info...");
				if (original_nested_type->getStructInfo()) {
					const StructTypeInfo* original_struct_info = original_nested_type->getStructInfo();
					FLASH_LOG(Templates, Debug, "Copying ", original_struct_info->static_members.size(), 
					          " static members from nested class ", original_nested_name);
					for (const auto& static_member : original_struct_info->static_members) {
						FLASH_LOG(Templates, Debug, "  Copying static member: ", StringTable::getStringView(static_member.getName()));
						nested_struct_info->addStaticMember(
							static_member.getName(),
							static_member.type,
							static_member.type_index,
							static_member.size,
							static_member.alignment,
							static_member.access,
							static_member.initializer,
							static_member.is_const
						);
					}
				} else {
					FLASH_LOG(Templates, Debug, "Original nested class has no struct info");
				}
			} else {
				// Try looking up with just the nested struct name (without template prefix)
				// This handles cases where templates use simple names for nested types
				std::string_view simple_name = StringTable::getStringView(nested_struct.name());
				FLASH_LOG(Templates, Debug, "Looking for nested class with simple name: ", simple_name);
				auto simple_nested_it = gTypesByName.find(nested_struct.name());
				if (simple_nested_it != gTypesByName.end()) {
					const TypeInfo* original_nested_type = simple_nested_it->second;
					if (original_nested_type->getStructInfo()) {
						const StructTypeInfo* original_struct_info = original_nested_type->getStructInfo();
						FLASH_LOG(Templates, Debug, "Copying ", original_struct_info->static_members.size(), 
						          " static members from nested class (simple name) ", simple_name);
						for (const auto& static_member : original_struct_info->static_members) {
							FLASH_LOG(Templates, Debug, "  Copying static member: ", StringTable::getStringView(static_member.getName()));
							nested_struct_info->addStaticMember(
								static_member.getName(),
								static_member.type,
								static_member.type_index,
								static_member.size,
								static_member.alignment,
								static_member.access,
								static_member.initializer,
								static_member.is_const
							);
						}
					}
				} else {
					FLASH_LOG(Templates, Debug, "Original nested class not found in gTypesByName");
				}
			}
			
			// Finalize the nested struct layout
			if (!nested_struct_info->finalize()) {
				// Log error and return nullopt - compilation will continue but template instantiation fails
				FLASH_LOG(Parser, Error, nested_struct_info->getFinalizationError());
				return std::nullopt;
			}
			
			// Register the nested class in the type system
			auto& nested_type_info = gTypeInfo.emplace_back(qualified_name, Type::Struct, gTypeInfo.size(), 0); // Placeholder size
			nested_type_info.setStructInfo(std::move(nested_struct_info));
			if (nested_type_info.getStructInfo()) {
				nested_type_info.type_size_ = nested_type_info.getStructInfo()->total_size;
			}
			gTypesByName.emplace(qualified_name, &nested_type_info);
			FLASH_LOG(Templates, Debug, "Registered nested class: ", StringTable::getStringView(qualified_name));
		}
	}

	// Copy type aliases from the template with template parameter substitution
	for (const auto& type_alias : class_decl.type_aliases()) {
		auto qualified_alias_name = StringTable::getOrInternStringHandle(StringBuilder().append(instantiated_name).append("::"sv).append(type_alias.alias_name));
		
		// Get the aliased type and substitute template parameters
		const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();
		
		// Create a substituted type specifier
		Type substituted_type = alias_type_spec.type();
		TypeIndex substituted_type_index = alias_type_spec.type_index();
		int substituted_size = alias_type_spec.size_in_bits();
		
		// Substitute template parameters in the alias type
		// Handle both UserDefined and Struct types (template types are often registered as Struct)
		if (substituted_type == Type::UserDefined || substituted_type == Type::Struct) {
			TypeIndex type_idx = alias_type_spec.type_index();
			if (type_idx < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_idx];
				std::string_view type_name = StringTable::getStringView(type_info.name());
				
				// Check for self-referential type alias (e.g., "using type = bool_constant;" inside bool_constant template)
				// When the template is instantiated (e.g., bool_constant_true), this should point to the instantiated type
				if (type_name == template_name) {
					// Self-referential type alias - point to the instantiated type
					auto inst_it = gTypesByName.find(instantiated_name);
					if (inst_it != gTypesByName.end()) {
						// Use the type_index_ field directly instead of pointer arithmetic
						// Pointer arithmetic on deque elements is undefined behavior
						TypeIndex inst_idx = inst_it->second->type_index_;
						substituted_type_index = inst_idx;
						FLASH_LOG(Templates, Debug, "Self-referential type alias '", StringTable::getStringView(type_alias.alias_name), 
						          "' now points to instantiated type '", instantiated_name, "' (index ", inst_idx, ")");
					}
				} else {
					// Use substitute_template_parameter for consistent template parameter matching
					auto [subst_type, subst_type_index] = substitute_template_parameter(
						alias_type_spec, template_params, template_args_to_use);
					
					// Only apply substitution if the type was actually a template parameter
					if (subst_type != alias_type_spec.type() || subst_type_index != alias_type_spec.type_index()) {
						substituted_type = subst_type;
						substituted_type_index = subst_type_index;
						substituted_size = get_type_size_bits(substituted_type);
					}
				}
			}
		}
		
		// Register the type alias in gTypesByName
		auto& alias_type_info = gTypeInfo.emplace_back(qualified_alias_name, substituted_type, substituted_type_index, substituted_size);
		gTypesByName.emplace(qualified_alias_name, &alias_type_info);
	}

	// Finalize the struct layout
	bool finalize_success;
	if (!struct_info->base_classes.empty()) {
		finalize_success = struct_info->finalizeWithBases();
	} else {
		finalize_success = struct_info->finalize();
	}
	
	// Check for semantic errors during finalization
	if (!finalize_success) {
		// Log error and return nullopt - compilation will continue but template instantiation fails
		FLASH_LOG(Parser, Error, struct_info->getFinalizationError());
		return std::nullopt;
	}

	// Store struct info in type info
	struct_type_info.setStructInfo(std::move(struct_info));
	
	// Update type_size_ from the finalized struct's total size
	if (struct_type_info.getStructInfo()) {
		struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
	}

	// Register member template aliases with the instantiated name
	// Member template aliases were registered during parsing with the primary template name (e.g., "__conditional::type")
	// We need to re-register them with the instantiated name (e.g., "__conditional_1::type")
	// This allows lookups like __conditional<true>::type<Args> to work correctly
	{
		// Build the template prefix string (e.g., "__conditional::")
		StringBuilder prefix_builder;
		std::string_view template_prefix = prefix_builder.append(template_name).append("::").preview();
		
		// Get all alias templates from the registry with this prefix
		std::vector<std::string_view> base_aliases_to_copy = gTemplateRegistry.get_alias_templates_with_prefix(template_prefix);
		prefix_builder.reset();
		
		// Now register each one with the instantiated name
		for (const auto& base_alias_name : base_aliases_to_copy) {
			// Extract the member name (everything after "template_name::")
			std::string_view member_name = std::string_view(base_alias_name).substr(template_prefix.size());
			
			// Build the new qualified name with the instantiated struct name
			std::string_view inst_alias_name = StringBuilder()
				.append(instantiated_name)
				.append("::")
				.append(member_name)
				.commit();
			
			// Look up the original alias node
			auto alias_opt = gTemplateRegistry.lookup_alias_template(base_alias_name);
			if (alias_opt.has_value()) {
				// Re-register with the instantiated name
				gTemplateRegistry.register_alias_template(std::string(inst_alias_name), *alias_opt);
			}
		}
	}

	// Get a pointer to the moved struct_info for later use
	StructTypeInfo* struct_info_ptr = struct_type_info.getStructInfo();

	// Create an AST node for the instantiated struct so member functions can be code-generated
	auto instantiated_struct = emplace_node<StructDeclarationNode>(
		instantiated_name,
		false  // is_class
	);
	StructDeclarationNode& instantiated_struct_ref = instantiated_struct.as<StructDeclarationNode>();
	
	// Log lazy instantiation status (already determined earlier in the function)
	if (use_lazy_instantiation) {
		FLASH_LOG(Templates, Debug, "Using LAZY instantiation for ", instantiated_name, " - registering ", 
		          class_decl.member_functions().size(), " member functions for on-demand instantiation");
	} else if (force_eager) {
		FLASH_LOG(Templates, Debug, "Using EAGER instantiation for ", instantiated_name, " (forced by explicit instantiation) - instantiating ", 
		          class_decl.member_functions().size(), " member functions immediately");
	}
	
	// Copy member functions from the template
	for (const StructMemberFunctionDecl& mem_func : class_decl.member_functions()) {

		if (mem_func.function_declaration.is<FunctionDeclarationNode>()) {
			const FunctionDeclarationNode& func_decl = mem_func.function_declaration.as<FunctionDeclarationNode>();
			const DeclarationNode& decl = func_decl.decl_node();

			// For lazy instantiation, register function for later instantiation instead of instantiating now
			if (use_lazy_instantiation && (func_decl.get_definition().has_value() || func_decl.has_template_body_position())) {
				// Register this member function for lazy instantiation
				LazyMemberFunctionInfo lazy_info;
				lazy_info.class_template_name = StringTable::getOrInternStringHandle(template_name);
				lazy_info.instantiated_class_name = instantiated_name;
				lazy_info.member_function_name = decl.identifier_token().handle();
				lazy_info.original_function_node = mem_func.function_declaration;
				lazy_info.template_params = template_params;
				lazy_info.template_args = template_args_to_use;
				lazy_info.access = mem_func.access;
				lazy_info.is_virtual = mem_func.is_virtual;
				lazy_info.is_pure_virtual = mem_func.is_pure_virtual;
				lazy_info.is_override = mem_func.is_override;
				lazy_info.is_final = mem_func.is_final;
				lazy_info.is_const_method = mem_func.is_const;
				lazy_info.is_constructor = false;
				lazy_info.is_destructor = false;
				
				LazyMemberInstantiationRegistry::getInstance().registerLazyMember(std::move(lazy_info));
				
				FLASH_LOG(Templates, Debug, "Registered lazy member function: ", 
				          instantiated_name, "::", decl.identifier_token().value());
				
				// Create function declaration with signature but WITHOUT body
				// This allows the function to be found during name lookup, but defers code generation
				
				// Substitute return type
				const TypeSpecifierNode& return_type_spec = decl.type_node().as<TypeSpecifierNode>();
				Type return_type = return_type_spec.type();
				TypeIndex return_type_index = return_type_spec.type_index();
				
				// First, check if the return type is a type alias defined in this template class
				// (e.g., "operator value_type()" where "using value_type = T;")
				// This is needed because substitute_template_parameter doesn't have access to type aliases
				if (return_type == Type::UserDefined && return_type_index == 0) {
					// type_index=0 means this is a placeholder (type parsed inside template before registration)
					// Try to find the type name from the token
					std::string_view return_type_name = return_type_spec.token().value();
					if (!return_type_name.empty()) {
						// Look up in the template class's type aliases
						for (const auto& type_alias : class_decl.type_aliases()) {
							if (StringTable::getStringView(type_alias.alias_name) == return_type_name) {
								// Found the type alias - check what it resolves to
								const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();
								
								// If the alias resolves to a template parameter, substitute it
								if (alias_type_spec.type() == Type::UserDefined) {
									// Try to substitute the alias target
									auto [subst_type, subst_index] = substitute_template_parameter(
										alias_type_spec, template_params, template_args_to_use
									);
									if (subst_type != Type::UserDefined || subst_index != 0) {
										return_type = subst_type;
										return_type_index = subst_index;
										FLASH_LOG(Templates, Debug, "Resolved return type alias '", return_type_name, 
											"' to type=", static_cast<int>(return_type));
									}
								}
								break;
							}
						}
					}
				}
				
				// If not resolved via type alias, try normal substitution
				if (return_type == Type::UserDefined) {
					auto [subst_type, subst_index] = substitute_template_parameter(
						return_type_spec, template_params, template_args_to_use
					);
					return_type = subst_type;
					return_type_index = subst_index;
				}

				// Create substituted return type node
				TypeSpecifierNode substituted_return_type(
					return_type,
					return_type_spec.qualifier(),
					get_type_size_bits(return_type),
					decl.identifier_token()
				);
				substituted_return_type.set_type_index(return_type_index);

				// Copy pointer levels and reference qualifiers from original
				for (const auto& ptr_level : return_type_spec.pointer_levels()) {
					substituted_return_type.add_pointer_level(ptr_level.cv_qualifier);
				}
				if (return_type_spec.is_rvalue_reference()) {
					substituted_return_type.set_reference(true);
				} else if (return_type_spec.is_reference()) {
					substituted_return_type.set_reference(false);
				}

				auto substituted_return_node = emplace_node<TypeSpecifierNode>(substituted_return_type);

				// Create a new function declaration with substituted return type but NO BODY
				auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(
					substituted_return_node, decl.identifier_token()
				);
				auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
					new_func_decl_ref, instantiated_name
				);

				// Substitute and copy parameters
				for (const auto& param : func_decl.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const DeclarationNode& param_decl = param.as<DeclarationNode>();
						const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

						// Substitute parameter type
						auto [param_type, param_type_index] = substitute_template_parameter(
							param_type_spec, template_params, template_args_to_use
						);

						// Create substituted parameter type
						TypeSpecifierNode substituted_param_type(
							param_type,
							param_type_spec.qualifier(),
							get_type_size_bits(param_type),
							param_decl.identifier_token(),
							param_type_spec.cv_qualifier()
						);
						substituted_param_type.set_type_index(param_type_index);

						// Copy pointer levels and reference qualifiers
						for (const auto& ptr_level : param_type_spec.pointer_levels()) {
							substituted_param_type.add_pointer_level(ptr_level.cv_qualifier);
						}
						if (param_type_spec.is_rvalue_reference()) {
							substituted_param_type.set_reference(true);
						} else if (param_type_spec.is_reference()) {
							substituted_param_type.set_reference(false);
						}

						auto substituted_param_type_node = emplace_node<TypeSpecifierNode>(substituted_param_type);
						auto substituted_param_decl = emplace_node<DeclarationNode>(
							substituted_param_type_node, param_decl.identifier_token()
						);
						// Copy default value if present
						if (param_decl.has_default_value()) {
							// Substitute template parameters in the default value expression
							std::unordered_map<std::string_view, TemplateTypeArg> param_map;
							for (size_t i = 0; i < template_params.size() && i < template_args_to_use.size(); ++i) {
								if (template_params[i].is<TemplateParameterNode>()) {
									const TemplateParameterNode& template_param = template_params[i].as<TemplateParameterNode>();
									param_map[template_param.name()] = template_args_to_use[i];
								}
							}
							ExpressionSubstitutor substitutor(param_map, *this);
							std::optional<ASTNode> substituted_default = substitutor.substitute(param_decl.default_value());
							if (substituted_default.has_value()) {
								substituted_param_decl.as<DeclarationNode>().set_default_value(*substituted_default);
							}
						}
						new_func_ref.add_parameter_node(substituted_param_decl);
					} else {
						// Non-declaration parameter, copy as-is
						new_func_ref.add_parameter_node(param);
					}
				}

				// Copy function properties but DO NOT set definition
				new_func_ref.set_is_constexpr(func_decl.is_constexpr());
				new_func_ref.set_is_consteval(func_decl.is_consteval());
				new_func_ref.set_is_constinit(func_decl.is_constinit());
				new_func_ref.set_noexcept(func_decl.is_noexcept());
				new_func_ref.set_is_variadic(func_decl.is_variadic());
				new_func_ref.set_linkage(func_decl.linkage());
				new_func_ref.set_calling_convention(func_decl.calling_convention());

				// Add the signature-only function to the instantiated struct
				instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
				
				// Also add to struct_info so it can be found during codegen
				StringHandle func_name_handle = decl.identifier_token().handle();
				struct_info_ptr->addMemberFunction(
					func_name_handle,
					new_func_node,
					mem_func.access,
					mem_func.is_virtual,
					mem_func.is_pure_virtual,
					mem_func.is_override,
					mem_func.is_final
				);
				
				// Skip to next function - body will be instantiated on-demand
				continue;
			}
			
			// EAGER INSTANTIATION PATH (original code)
			// If the function has a definition or deferred body, we need to substitute template parameters
			if (func_decl.get_definition().has_value() || func_decl.has_template_body_position()) {
				// Substitute return type
				const TypeSpecifierNode& return_type_spec = decl.type_node().as<TypeSpecifierNode>();
				auto [return_type, return_type_index] = substitute_template_parameter(
					return_type_spec, template_params, template_args_to_use
				);

				// Create substituted return type node
				TypeSpecifierNode substituted_return_type(
					return_type,
					return_type_spec.qualifier(),
					get_type_size_bits(return_type),
					decl.identifier_token()
				);
				substituted_return_type.set_type_index(return_type_index);

				// Copy pointer levels and reference qualifiers from original
				for (const auto& ptr_level : return_type_spec.pointer_levels()) {
					substituted_return_type.add_pointer_level(ptr_level.cv_qualifier);
				}
				if (return_type_spec.is_rvalue_reference()) {
					substituted_return_type.set_reference(true);
				} else if (return_type_spec.is_reference()) {
					substituted_return_type.set_reference(false);
				}

				auto substituted_return_node = emplace_node<TypeSpecifierNode>(substituted_return_type);

				// Create a new function declaration with substituted return type
				auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(
					substituted_return_node, decl.identifier_token()
				);
				auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
					new_func_decl_ref, instantiated_name
				);

				// Substitute and copy parameters
				for (const auto& param : func_decl.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const DeclarationNode& param_decl = param.as<DeclarationNode>();
						const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

						// Substitute parameter type
						auto [param_type, param_type_index] = substitute_template_parameter(
							param_type_spec, template_params, template_args_to_use
						);

						// Create substituted parameter type
						TypeSpecifierNode substituted_param_type(
							param_type,
							param_type_spec.qualifier(),
							get_type_size_bits(param_type),
							param_decl.identifier_token(),
							param_type_spec.cv_qualifier()  // Preserve const/volatile qualifiers
						);
						substituted_param_type.set_type_index(param_type_index);

						// Copy pointer levels and reference qualifiers
						for (const auto& ptr_level : param_type_spec.pointer_levels()) {
							substituted_param_type.add_pointer_level(ptr_level.cv_qualifier);
						}
						if (param_type_spec.is_rvalue_reference()) {
							substituted_param_type.set_reference(true);
						} else if (param_type_spec.is_reference()) {
							substituted_param_type.set_reference(false);
						}

						auto substituted_param_type_node = emplace_node<TypeSpecifierNode>(substituted_param_type);
						auto substituted_param_decl = emplace_node<DeclarationNode>(
							substituted_param_type_node, param_decl.identifier_token()
						);
						// Copy default value if present
						if (param_decl.has_default_value()) {
							// Substitute template parameters in the default value expression
							std::unordered_map<std::string_view, TemplateTypeArg> param_map;
							for (size_t i = 0; i < template_params.size() && i < template_args_to_use.size(); ++i) {
								if (template_params[i].is<TemplateParameterNode>()) {
									const TemplateParameterNode& template_param = template_params[i].as<TemplateParameterNode>();
									param_map[template_param.name()] = template_args_to_use[i];
								}
							}
							ExpressionSubstitutor substitutor(param_map, *this);
							std::optional<ASTNode> substituted_default = substitutor.substitute(param_decl.default_value());
							if (substituted_default.has_value()) {
								substituted_param_decl.as<DeclarationNode>().set_default_value(*substituted_default);
							}
						}
						new_func_ref.add_parameter_node(substituted_param_decl);
					} else {
						// Non-declaration parameter, copy as-is
						new_func_ref.add_parameter_node(param);
					}
				}

				// Get the function body - either from definition or by re-parsing from saved position
				std::optional<ASTNode> body_to_substitute;
				
				if (func_decl.get_definition().has_value()) {
					// Use the already-parsed definition
					body_to_substitute = func_decl.get_definition();
				} else if (func_decl.has_template_body_position()) {
					// Re-parse the function body from saved position
					// This is needed for member struct templates where body parsing is deferred
					
					// Set up template parameter types in the type system for body parsing
					FlashCpp::TemplateParameterScope template_scope;
					std::vector<std::string_view> param_names;
					param_names.reserve(template_params.size());
					for (const auto& tparam_node : template_params) {
						if (tparam_node.is<TemplateParameterNode>()) {
							param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
						}
					}
					
					for (size_t i = 0; i < param_names.size() && i < template_args_to_use.size(); ++i) {
						std::string_view param_name = param_names[i];
						Type concrete_type = template_args_to_use[i].base_type;

						auto& type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(param_name), concrete_type, gTypeInfo.size(), get_type_size_bits(concrete_type));
						
						// Copy reference qualifiers from template arg
						type_info.is_reference_ = template_args_to_use[i].is_reference;
						type_info.is_rvalue_reference_ = template_args_to_use[i].is_rvalue_reference;
						
						gTypesByName.emplace(type_info.name(), &type_info);
						template_scope.addParameter(&type_info);
					}

					// Save current position and parsing context
					SaveHandle current_pos = save_token_position();
					const FunctionDeclarationNode* saved_current_function = current_function_;

					// Restore to the function body start
					restore_lexer_position_only(func_decl.template_body_position());

					// Set up parsing context for the function
					gSymbolTable.enter_scope(ScopeType::Function);
					current_function_ = &new_func_ref;

					// Add parameters to symbol table
					for (const auto& param : new_func_ref.parameter_nodes()) {
						if (param.is<DeclarationNode>()) {
							const auto& param_decl = param.as<DeclarationNode>();
							gSymbolTable.insert(param_decl.identifier_token().value(), param);
						}
					}

					// Parse the function body
					auto block_result = parse_block();
					
					if (!block_result.is_error() && block_result.node().has_value()) {
						body_to_substitute = block_result.node();
					}
					
					// Clean up context
					current_function_ = saved_current_function;
					gSymbolTable.exit_scope();

					// Restore original position
					restore_lexer_position_only(current_pos);
					discard_saved_token(current_pos);
				}

				// Substitute template parameters in the function body
				if (body_to_substitute.has_value()) {
					// Convert TemplateTypeArg vector to TemplateArgument vector
					std::vector<TemplateArgument> converted_template_args;
					for (const auto& ttype_arg : template_args_to_use) {
						if (ttype_arg.is_value) {
							converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value, ttype_arg.base_type));
						} else {
							converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
						}
					}

					try {
						ASTNode substituted_body = substituteTemplateParameters(
							*body_to_substitute,
							template_params,
							converted_template_args
						);
						new_func_ref.set_definition(substituted_body);
					} catch (const std::exception& e) {
						FLASH_LOG(Templates, Error, "Exception during template parameter substitution for function ", 
						          decl.identifier_token().value(), ": ", e.what());
						throw;
					} catch (...) {
						FLASH_LOG(Templates, Error, "Unknown exception during template parameter substitution for function ", 
						          decl.identifier_token().value());
						throw;
					}
				}

				// Add the substituted function to the instantiated struct
				instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
				
				// Also add to struct_info so it can be found during codegen
				// Phase 7B: Intern function name and use StringHandle overload
				StringHandle func_name_handle = decl.identifier_token().handle();
				struct_info_ptr->addMemberFunction(
					func_name_handle,
					new_func_node,
					mem_func.access,
					mem_func.is_virtual,
					mem_func.is_pure_virtual,
					mem_func.is_override,
					mem_func.is_final
				);
			} else {
				// No definition, but still need to substitute parameter types and return type
				
				// Substitute return type
				const TypeSpecifierNode& return_type_spec = decl.type_node().as<TypeSpecifierNode>();
				Type return_type = return_type_spec.type();
				TypeIndex return_type_index = return_type_spec.type_index();
				
				// First, check if the return type is a type alias defined in this template class
				// (e.g., "operator value_type()" where "using value_type = T;")
				if (return_type == Type::UserDefined && return_type_index == 0) {
					// type_index=0 means this is a placeholder (type parsed inside template before registration)
					std::string_view return_type_name = return_type_spec.token().value();
					if (!return_type_name.empty()) {
						// Look up in the template class's type aliases
						for (const auto& type_alias : class_decl.type_aliases()) {
							if (StringTable::getStringView(type_alias.alias_name) == return_type_name) {
								// Found the type alias - check what it resolves to
								const TypeSpecifierNode& alias_type_spec = type_alias.type_node.as<TypeSpecifierNode>();
								
								// If the alias resolves to a template parameter, substitute it
								if (alias_type_spec.type() == Type::UserDefined) {
									auto [subst_type, subst_index] = substitute_template_parameter(
										alias_type_spec, template_params, template_args_to_use
									);
									if (subst_type != Type::UserDefined || subst_index != 0) {
										return_type = subst_type;
										return_type_index = subst_index;
										FLASH_LOG(Templates, Debug, "Resolved return type alias '", return_type_name, 
											"' to type=", static_cast<int>(return_type), " (no-definition path)");
									}
								}
								break;
							}
						}
					}
				}
				
				// If not resolved via type alias, try normal substitution
				if (return_type == Type::UserDefined) {
					auto [subst_type, subst_index] = substitute_template_parameter(
						return_type_spec, template_params, template_args_to_use
					);
					return_type = subst_type;
					return_type_index = subst_index;
				}

				// Create substituted return type node
				TypeSpecifierNode substituted_return_type(
					return_type,
					return_type_spec.qualifier(),
					get_type_size_bits(return_type),
					decl.identifier_token()
				);
				substituted_return_type.set_type_index(return_type_index);

				// Copy pointer levels and reference qualifiers from original
				for (const auto& ptr_level : return_type_spec.pointer_levels()) {
					substituted_return_type.add_pointer_level(ptr_level.cv_qualifier);
				}
				if (return_type_spec.is_rvalue_reference()) {
					substituted_return_type.set_reference(true);
				} else if (return_type_spec.is_reference()) {
					substituted_return_type.set_reference(false);
				}

				auto substituted_return_node = emplace_node<TypeSpecifierNode>(substituted_return_type);

				// Create a new function declaration with substituted return type
				auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(
					substituted_return_node, decl.identifier_token()
				);
				auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
					new_func_decl_ref, instantiated_name
				);

				// Substitute and copy parameters
				for (const auto& param : func_decl.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const DeclarationNode& param_decl = param.as<DeclarationNode>();
						const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

						// Substitute parameter type
						auto [param_type, param_type_index] = substitute_template_parameter(
							param_type_spec, template_params, template_args_to_use
						);

						// Create substituted parameter type
						TypeSpecifierNode substituted_param_type(
							param_type,
							param_type_spec.qualifier(),
							get_type_size_bits(param_type),
							param_decl.identifier_token()
						);
						substituted_param_type.set_type_index(param_type_index);

						// Copy pointer levels and reference qualifiers
						for (const auto& ptr_level : param_type_spec.pointer_levels()) {
							substituted_param_type.add_pointer_level(ptr_level.cv_qualifier);
						}
						if (param_type_spec.is_rvalue_reference()) {
							substituted_param_type.set_reference(true);
						} else if (param_type_spec.is_reference()) {
							substituted_param_type.set_reference(false);
						}

						auto substituted_param_node = emplace_node<TypeSpecifierNode>(substituted_param_type);
						auto [param_decl_node, param_decl_ref] = emplace_node_ref<DeclarationNode>(
							substituted_param_node, param_decl.identifier_token()
						);

						new_func_ref.add_parameter_node(param_decl_node);
					}
				}

				// Copy other function properties
				new_func_ref.set_is_constexpr(func_decl.is_constexpr());
				new_func_ref.set_is_consteval(func_decl.is_consteval());
				new_func_ref.set_is_constinit(func_decl.is_constinit());
				new_func_ref.set_noexcept(func_decl.is_noexcept());
				new_func_ref.set_is_variadic(func_decl.is_variadic());
				new_func_ref.set_linkage(func_decl.linkage());
				new_func_ref.set_calling_convention(func_decl.calling_convention());

				// Add the substituted function to the instantiated struct
				instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
				
				// Also add to struct_info so it can be found during codegen
				// Phase 7B: Intern function name and use StringHandle overload
				StringHandle func_name_handle = decl.identifier_token().handle();
				struct_info_ptr->addMemberFunction(
					func_name_handle,
					new_func_node,
					mem_func.access,
					mem_func.is_virtual,
					mem_func.is_pure_virtual,
					mem_func.is_override,
					mem_func.is_final
				);
			}
		} else if (mem_func.function_declaration.is<ConstructorDeclarationNode>()) {
			const ConstructorDeclarationNode& ctor_decl = mem_func.function_declaration.as<ConstructorDeclarationNode>();
			
			// NOTE: Constructors are ALWAYS eagerly instantiated (not lazy)
			// because they're needed for object creation
			
			// EAGER INSTANTIATION PATH (original code)
			if (ctor_decl.get_definition().has_value()) {
				// Convert TemplateTypeArg vector to TemplateArgument vector
				std::vector<TemplateArgument> converted_template_args;
				for (const auto& ttype_arg : template_args_to_use) {
					if (ttype_arg.is_value) {
						converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value, ttype_arg.base_type));
					} else {
						converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
					}
				}

				try {
					ASTNode substituted_body = substituteTemplateParameters(
						*ctor_decl.get_definition(),
						template_params,
						converted_template_args
					);
					
					// Create a new constructor declaration with substituted body
					auto [new_ctor_node, new_ctor_ref] = emplace_node_ref<ConstructorDeclarationNode>(
						instantiated_name,
						instantiated_name
					);
					
					// Substitute and copy parameters
					for (const auto& param : ctor_decl.parameter_nodes()) {
						if (param.is<DeclarationNode>()) {
							const DeclarationNode& param_decl = param.as<DeclarationNode>();
							const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

							// Substitute parameter type
							auto [param_type, param_type_index] = substitute_template_parameter(
								param_type_spec, template_params, template_args_to_use
							);

							// Create substituted parameter type
							TypeSpecifierNode substituted_param_type(
								param_type,
								param_type_spec.qualifier(),
								get_type_size_bits(param_type),
								param_decl.identifier_token(),
								param_type_spec.cv_qualifier()  // Preserve const/volatile qualifiers
							);
							substituted_param_type.set_type_index(param_type_index);

							// Copy pointer levels and reference qualifiers
							for (const auto& ptr_level : param_type_spec.pointer_levels()) {
								substituted_param_type.add_pointer_level(ptr_level.cv_qualifier);
							}
							if (param_type_spec.is_rvalue_reference()) {
								substituted_param_type.set_reference(true);
							} else if (param_type_spec.is_reference()) {
								substituted_param_type.set_reference(false);
							}

							auto substituted_param_type_node = emplace_node<TypeSpecifierNode>(substituted_param_type);
							auto substituted_param_decl = emplace_node<DeclarationNode>(
								substituted_param_type_node, param_decl.identifier_token()
							);
							// Copy default value if present
							if (param_decl.has_default_value()) {
								// Substitute template parameters in the default value expression
								std::unordered_map<std::string_view, TemplateTypeArg> param_map;
								bool fill_template_param_order = template_param_order.empty();
								for (size_t i = 0; i < template_params.size() && i < template_args_to_use.size(); ++i) {
									if (template_params[i].is<TemplateParameterNode>()) {
										const TemplateParameterNode& template_param = template_params[i].as<TemplateParameterNode>();
										param_map[template_param.name()] = template_args_to_use[i];

										if (fill_template_param_order) {
											template_param_order.push_back(template_param.name());
										}
									}
								}
								ExpressionSubstitutor substitutor(param_map, *this, template_param_order);
								std::optional<ASTNode> substituted_default = substitutor.substitute(param_decl.default_value());
								if (substituted_default.has_value()) {
									substituted_param_decl.as<DeclarationNode>().set_default_value(*substituted_default);
								}
							}
							new_ctor_ref.add_parameter_node(substituted_param_decl);
						} else {
							// Non-declaration parameter, copy as-is
							new_ctor_ref.add_parameter_node(param);
						}
					}
					
					// Copy other properties
					for (const auto& init : ctor_decl.member_initializers()) {
						new_ctor_ref.add_member_initializer(init.member_name, init.initializer_expr);
					}
					for (const auto& init : ctor_decl.base_initializers()) {
						// Phase 7B: Intern base class name and use StringHandle overload
						StringHandle base_name_handle = init.getBaseClassName();
						new_ctor_ref.add_base_initializer(base_name_handle, init.arguments);
					}
					if (ctor_decl.delegating_initializer().has_value()) {
						new_ctor_ref.set_delegating_initializer(ctor_decl.delegating_initializer()->arguments);
					}
					new_ctor_ref.set_is_implicit(ctor_decl.is_implicit());
					new_ctor_ref.set_definition(substituted_body);
					
					// Add the substituted constructor to the instantiated struct AST node
					instantiated_struct_ref.add_constructor(new_ctor_node, mem_func.access);
					
					// Also add to struct_info so it can be found during codegen
					struct_info_ptr->addConstructor(new_ctor_node, mem_func.access);
				} catch (const std::exception& e) {
					FLASH_LOG(Templates, Error, "Exception during template parameter substitution for constructor ", 
					          ctor_decl.name(), ": ", e.what());
					throw;
				} catch (...) {
					FLASH_LOG(Templates, Error, "Unknown exception during template parameter substitution for constructor ", 
					          ctor_decl.name());
					throw;
				}
			} else {
				// No definition to substitute, copy directly
				instantiated_struct_ref.add_constructor(
					mem_func.function_declaration,
					mem_func.access
				);
				
				// Also add to struct_info so it can be found during codegen
				struct_info_ptr->addConstructor(mem_func.function_declaration, mem_func.access);
			}
		} else if (mem_func.function_declaration.is<DestructorDeclarationNode>()) {
			const DestructorDeclarationNode& dtor_decl = mem_func.function_declaration.as<DestructorDeclarationNode>();
			
			// NOTE: Destructors are ALWAYS eagerly instantiated (not lazy)
			// because they're needed for object destruction
			
			// EAGER INSTANTIATION PATH (original code)
			if (dtor_decl.get_definition().has_value()) {
				// Convert TemplateTypeArg vector to TemplateArgument vector
				std::vector<TemplateArgument> converted_template_args;
				for (const auto& ttype_arg : template_args_to_use) {
					if (ttype_arg.is_value) {
						converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value, ttype_arg.base_type));
					} else {
						converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
					}
				}

				try {
					ASTNode substituted_body = substituteTemplateParameters(
						*dtor_decl.get_definition(),
						template_params,
						converted_template_args
					);
					
					// Create a new destructor declaration with substituted body
					StringHandle specialized_dtor_name = StringTable::getOrInternStringHandle(StringBuilder()
						.append("~")
						.append(instantiated_name));
					auto [new_dtor_node, new_dtor_ref] = emplace_node_ref<DestructorDeclarationNode>(
						instantiated_name,
						specialized_dtor_name
					);
					
					new_dtor_ref.set_definition(substituted_body);
					
					// Add the substituted destructor to the instantiated struct
					instantiated_struct_ref.add_destructor(new_dtor_node, mem_func.access);

					// Also add to struct_info so hasDestructor() returns true during codegen
					struct_info_ptr->addDestructor(new_dtor_node, mem_func.access, mem_func.is_virtual);
				} catch (const std::exception& e) {
					FLASH_LOG(Templates, Error, "Exception during template parameter substitution for destructor ", 
					          dtor_decl.name(), ": ", e.what());
					throw;
				} catch (...) {
					FLASH_LOG(Templates, Error, "Unknown exception during template parameter substitution for destructor ", 
					          dtor_decl.name());
					throw;
				}
			} else {
				// No definition to substitute, copy directly
				instantiated_struct_ref.add_destructor(
					mem_func.function_declaration,
					mem_func.access
				);

				// Also add to struct_info so hasDestructor() returns true during codegen
				struct_info_ptr->addDestructor(mem_func.function_declaration, mem_func.access, mem_func.is_virtual);
			}
		} else if (mem_func.function_declaration.is<TemplateFunctionDeclarationNode>()) {
			// Member template functions should be copied to the instantiated class as-is
			// They are themselves templates and will be instantiated when called
			const TemplateFunctionDeclarationNode& template_func = 
				mem_func.function_declaration.as<TemplateFunctionDeclarationNode>();
			
			FLASH_LOG(Templates, Debug, "Copying member template function to instantiated class");
			
			// Add the member template function to the instantiated struct
			instantiated_struct_ref.add_member_function(
				mem_func.function_declaration,
				mem_func.access
			);
			
			// Also register the member template function in the global template registry
			// with the instantiated class name
			const FunctionDeclarationNode& func_decl = 
				template_func.function_declaration().as<FunctionDeclarationNode>();
			const DeclarationNode& decl_node = func_decl.decl_node();
			
			// Register with qualified name (InstantiatedClassName::functionName)
			StringBuilder qualified_name_builder;
			qualified_name_builder.append(StringTable::getStringView(instantiated_name))
			                     .append("::")
			                     .append(decl_node.identifier_token().value());
			std::string_view qualified_name = qualified_name_builder.commit();
			
			gTemplateRegistry.registerTemplate(qualified_name, mem_func.function_declaration);
			
			// Also register with simple name for unqualified lookups
			gTemplateRegistry.registerTemplate(decl_node.identifier_token().value(), mem_func.function_declaration);
		} else {
			FLASH_LOG(Templates, Error, "Unknown member function type in template instantiation: ", 
			          mem_func.function_declaration.type_name());
			// Copy directly as fallback
			instantiated_struct_ref.add_member_function(
				mem_func.function_declaration,
				mem_func.access
			);
		}
	}

	// Process out-of-line member function definitions for the template
	auto out_of_line_members = gTemplateRegistry.getOutOfLineMemberFunctions(template_name);
	FLASH_LOG(Templates, Debug, "Processing ", out_of_line_members.size(), " out-of-line member functions for ", template_name);
	
	for (const auto& out_of_line_member : out_of_line_members) {
		// The function_node should be a FunctionDeclarationNode
		if (!out_of_line_member.function_node.is<FunctionDeclarationNode>()) {
			FLASH_LOG(Templates, Error, "Out-of-line member function_node is not a FunctionDeclarationNode, type: ", out_of_line_member.function_node.type_name());
			continue;
		}
		
		const FunctionDeclarationNode& func_decl = out_of_line_member.function_node.as<FunctionDeclarationNode>();
		const DeclarationNode& decl = func_decl.decl_node();
		
		FLASH_LOG(Templates, Debug, "  Looking for match of out-of-line '", decl.identifier_token().value(), 
		          "' in ", instantiated_struct_ref.member_functions().size(), " struct member functions");
		
		// Check if this function is in the instantiated struct's member functions
		// We need to find the matching declaration in the instantiated struct and add the definition
		bool found_match = false;
		for (auto& mem_func : instantiated_struct_ref.member_functions()) {
			if (mem_func.function_declaration.is<FunctionDeclarationNode>()) {
				FunctionDeclarationNode& inst_func = mem_func.function_declaration.as<FunctionDeclarationNode>();
				const DeclarationNode& inst_decl = inst_func.decl_node();
				
				// Check if function names match
				if (inst_decl.identifier_token().value() == decl.identifier_token().value()) {
					// Save current position
					SaveHandle saved_pos = save_token_position();
					
					// Add function parameters to scope so they're available during body parsing
					gSymbolTable.enter_scope(ScopeType::Block);
					for (const auto& param_node : inst_func.parameter_nodes()) {
						if (param_node.is<DeclarationNode>()) {
							const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
							gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
						}
					}
					
					// Set up member function context so member variables (like 'value') are resolved
					// as this->value instead of causing "missing identifier" errors
					member_function_context_stack_.push_back({
						instantiated_name,
						struct_type_info.type_index_,
						&instantiated_struct_ref,
						nullptr  // local_struct_info - not needed for out-of-line member functions
					});
					
					// Restore to the out-of-line function body position
					restore_lexer_position_only(out_of_line_member.body_start);
					
					// The current token should be '{'
					if (peek() != "{"_tok) {
						FLASH_LOG(Templates, Error, "Expected '{' at body_start position, got: ", 
						          (!peek().is_eof() ? std::string(peek_info().value()) : "EOF"));
						member_function_context_stack_.pop_back();
						gSymbolTable.exit_scope();
						restore_lexer_position_only(saved_pos);
						continue;
					}
					
					// Parse the function body
					auto body_result = parse_block();
					
					// Pop member function context
					member_function_context_stack_.pop_back();
					
					// Pop parameter scope
					gSymbolTable.exit_scope();
					
					// Restore position
					restore_lexer_position_only(saved_pos);
					
					if (body_result.is_error() || !body_result.node().has_value()) {
						FLASH_LOG(Templates, Error, "Failed to parse out-of-line function body for ", 
						          decl.identifier_token().value());
						continue;
					}
					
					// Now substitute template parameters in the parsed body
					std::vector<TemplateArgument> converted_template_args;
					converted_template_args.reserve(template_args_to_use.size());
					for (const auto& ttype_arg : template_args_to_use) {
						if (ttype_arg.is_value) {
							converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value, ttype_arg.base_type));
						} else {
							converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
						}
					}
					
					try {
						ASTNode substituted_body = substituteTemplateParameters(
							*body_result.node(),
							out_of_line_member.template_params,
							converted_template_args
						);
						inst_func.set_definition(substituted_body);
						found_match = true;
						break;
					} catch (const std::exception& e) {
						FLASH_LOG(Templates, Error, "Exception during template parameter substitution for out-of-line function ", 
						          decl.identifier_token().value(), ": ", e.what());
					}
				}
			}
			// Also check ConstructorDeclarationNode members for out-of-line constructor definitions
			// The out-of-line definition uses the template name (e.g., "Buffer") but the
			// instantiated constructor uses the instantiated name (e.g., "Buffer$hash")
			else if (mem_func.is_constructor && mem_func.function_declaration.is<ConstructorDeclarationNode>()) {
				auto& ctor = mem_func.function_declaration.as<ConstructorDeclarationNode>();
				std::string_view ool_name = decl.identifier_token().value();
				std::string_view ctor_name = StringTable::getStringView(ctor.name());
				// Match if names are equal, or if ctor name starts with ool_name + '$'
				bool names_match = (ctor_name == ool_name);
				if (!names_match && ctor_name.size() > ool_name.size() && 
				    ctor_name[ool_name.size()] == '$' &&
				    ctor_name.substr(0, ool_name.size()) == ool_name) {
					names_match = true;
				}
				if (names_match) {
					// Save current position
					SaveHandle saved_pos = save_token_position();
					
					// Add constructor parameters to scope
					gSymbolTable.enter_scope(ScopeType::Block);
					for (const auto& param_node : ctor.parameter_nodes()) {
						if (param_node.is<DeclarationNode>()) {
							const DeclarationNode& param_decl = param_node.as<DeclarationNode>();
							gSymbolTable.insert(param_decl.identifier_token().value(), param_node);
						}
					}
					
					// Set up member function context
					member_function_context_stack_.push_back({
						instantiated_name,
						struct_type_info.type_index_,
						&instantiated_struct_ref,
						nullptr
					});
					
					// Restore to the out-of-line function body position
					restore_lexer_position_only(out_of_line_member.body_start);
					
					if (peek() != "{"_tok) {
						FLASH_LOG(Templates, Error, "Expected '{' at body_start position for constructor, got: ", 
						          (!peek().is_eof() ? std::string(peek_info().value()) : "EOF"));
						member_function_context_stack_.pop_back();
						gSymbolTable.exit_scope();
						restore_lexer_position_only(saved_pos);
						continue;
					}
					
					auto body_result = parse_block();
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					restore_lexer_position_only(saved_pos);
					
					if (body_result.is_error() || !body_result.node().has_value()) {
						FLASH_LOG(Templates, Error, "Failed to parse out-of-line constructor body for ", 
						          decl.identifier_token().value());
						continue;
					}
					
					std::vector<TemplateArgument> converted_template_args;
					converted_template_args.reserve(template_args_to_use.size());
					for (const auto& ttype_arg : template_args_to_use) {
						if (ttype_arg.is_value) {
							converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value, ttype_arg.base_type));
						} else {
							converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
						}
					}
					
					try {
						ASTNode substituted_body = substituteTemplateParameters(
							*body_result.node(),
							out_of_line_member.template_params,
							converted_template_args
						);
						ctor.set_definition(substituted_body);
						// Also update the StructTypeInfo's copy (used by codegen)
						if (struct_type_info.struct_info_) {
							for (auto& info_func : struct_type_info.struct_info_->member_functions) {
								if (info_func.is_constructor && info_func.function_decl.is<ConstructorDeclarationNode>()) {
									auto& info_ctor = info_func.function_decl.as<ConstructorDeclarationNode>();
									if (info_ctor.name() == ctor.name() && !info_ctor.get_definition().has_value()) {
										info_ctor.set_definition(substituted_body);
										break;
									}
								}
							}
						}
						found_match = true;
						break;
					} catch (const std::exception& e) {
						FLASH_LOG(Templates, Error, "Exception during template parameter substitution for out-of-line constructor ", 
						          decl.identifier_token().value(), ": ", e.what());
					}
				}
			}
		}
		
		if (!found_match) {
			FLASH_LOG(Templates, Warning, "Out-of-line member function ", decl.identifier_token().value(), 
			          " not found in instantiated struct");
		}
	}

	// Process out-of-line static member variable definitions for the template
	auto out_of_line_vars = gTemplateRegistry.getOutOfLineMemberVariables(template_name);
	
	for (const auto& out_of_line_var : out_of_line_vars) {
		// Substitute template parameters in the type and initializer
		std::vector<TemplateArgument> converted_template_args;
		converted_template_args.reserve(template_args_to_use.size());
		for (const auto& ttype_arg : template_args_to_use) {
			if (ttype_arg.is_value) {
				converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value, ttype_arg.base_type));
			} else {
				converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
			}
		}
		
		// Substitute template parameters in the initializer
		std::optional<ASTNode> substituted_initializer = out_of_line_var.initializer;
		if (out_of_line_var.initializer.has_value()) {
			try {
				substituted_initializer = substituteTemplateParameters(
					*out_of_line_var.initializer,
					out_of_line_var.template_params,
					converted_template_args
				);
			} catch (const std::exception& e) {
				FLASH_LOG(Templates, Error, "Exception during template parameter substitution for static member ", 
				          out_of_line_var.member_name, ": ", e.what());
			}
		}
		
		// Add the static member to the instantiated struct (or update if it already exists)
		if (out_of_line_var.type_node.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_spec = out_of_line_var.type_node.as<TypeSpecifierNode>();
			size_t member_size = get_type_size_bits(type_spec.type()) / 8;
			size_t member_alignment = get_type_alignment(type_spec.type(), member_size);

			StringHandle static_member_name_handle = out_of_line_var.member_name;

			// Check if this static member was already added (e.g., from primary template processing)
			// If it exists, update the initializer; otherwise add a new member
			const StructStaticMember* existing_member = struct_info_ptr->findStaticMember(static_member_name_handle);
			if (existing_member != nullptr) {
				// Member already exists - update the initializer with the out-of-line definition
				if (substituted_initializer.has_value()) {
					struct_info_ptr->updateStaticMemberInitializer(static_member_name_handle, substituted_initializer);
					FLASH_LOG(Templates, Debug, "Updated out-of-line static member initializer for ", out_of_line_var.member_name,
					          " in instantiated struct ", instantiated_name);
				}
			} else {
				struct_info_ptr->addStaticMember(
					static_member_name_handle,
					type_spec.type(),
					type_spec.type_index(),
					member_size,
					member_alignment,
					AccessSpecifier::Public,
					substituted_initializer,
					false  // is_const
				);

				FLASH_LOG(Templates, Debug, "Added out-of-line static member ", out_of_line_var.member_name,
				          " to instantiated struct ", instantiated_name);
			}
		}
	}

	// Copy static members from the primary template
	// Get the primary template's StructTypeInfo
	auto primary_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(template_name));
	if (primary_type_it != gTypesByName.end()) {
		const TypeInfo* primary_type_info = primary_type_it->second;
		const StructTypeInfo* primary_struct_info = primary_type_info->getStructInfo();
		if (primary_struct_info) {
			for (const auto& static_member : primary_struct_info->static_members) {
				
				// Check if initializer contains sizeof...(pack_name) and substitute with pack size
				std::optional<ASTNode> substituted_initializer = static_member.initializer;
				if (static_member.initializer.has_value() && static_member.initializer->is<ExpressionNode>()) {
					const ExpressionNode& expr = static_member.initializer->as<ExpressionNode>();
					
					// Calculate pack size for substitution
					auto calculate_pack_size = [&](std::string_view pack_name) -> std::optional<size_t> {
						for (size_t i = 0; i < template_params.size(); ++i) {
							const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
							if (tparam.name() == pack_name && tparam.is_variadic()) {
								size_t non_variadic_count = 0;
								for (const auto& param : template_params) {
									if (!param.as<TemplateParameterNode>().is_variadic()) {
										non_variadic_count++;
									}
								}
								return template_args_to_use.size() - non_variadic_count;
							}
						}
						return std::nullopt;
					};
					
					// Helper to create a numeric literal from pack size
					auto make_pack_size_literal = [&](size_t pack_size) -> ASTNode {
						std::string_view pack_size_str = StringBuilder().append(pack_size).commit();
						Token num_token(Token::Type::Literal, pack_size_str, 0, 0, 0);
						return emplace_node<ExpressionNode>(
							NumericLiteralNode(num_token, static_cast<unsigned long long>(pack_size), Type::Int, TypeQualifier::None, 32)
						);
					};
					
					if (std::holds_alternative<SizeofPackNode>(expr)) {
						// Direct sizeof... expression
						const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(expr);
						if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
							substituted_initializer = make_pack_size_literal(*pack_size);
						}
					}
					else if (std::holds_alternative<BinaryOperatorNode>(expr)) {
						// Binary expression like "1 + sizeof...(Rest)" - need to substitute sizeof...
						const BinaryOperatorNode& bin_expr = std::get<BinaryOperatorNode>(expr);
						
						// Helper to extract pack size from various expression forms (including static_cast)
						auto try_extract_pack_size = [&](const ExpressionNode& e) -> std::optional<size_t> {
							if (std::holds_alternative<SizeofPackNode>(e)) {
								const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(e);
								return calculate_pack_size(sizeof_pack.pack_name());
							}
							// Handle static_cast<T>(sizeof...(Ts))
							if (std::holds_alternative<StaticCastNode>(e)) {
								const StaticCastNode& cast_node = std::get<StaticCastNode>(e);
								if (cast_node.expr().is<ExpressionNode>()) {
									const ExpressionNode& cast_inner = cast_node.expr().as<ExpressionNode>();
									if (std::holds_alternative<SizeofPackNode>(cast_inner)) {
										const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(cast_inner);
										return calculate_pack_size(sizeof_pack.pack_name());
									}
								}
							}
							return std::nullopt;
						};
						
						// Helper to extract numeric value from expression
						auto try_extract_numeric = [](const ExpressionNode& e) -> std::optional<unsigned long long> {
							if (std::holds_alternative<NumericLiteralNode>(e)) {
								const NumericLiteralNode& num = std::get<NumericLiteralNode>(e);
								auto val = num.value();
								return std::holds_alternative<unsigned long long>(val) 
									? std::get<unsigned long long>(val)
									: static_cast<unsigned long long>(std::get<double>(val));
							}
							return std::nullopt;
						};
						
						// Helper to evaluate a binary expression
						auto evaluate_binary = [](std::string_view op, unsigned long long lhs, unsigned long long rhs) -> unsigned long long {
							if (op == "+") return lhs + rhs;
							if (op == "-") return lhs - rhs;
							if (op == "*") return lhs * rhs;
							if (op == "/") return rhs != 0 ? lhs / rhs : 0;
							return 0;
						};
						
						// Try to evaluate the top-level binary expression
						if (bin_expr.get_lhs().is<ExpressionNode>() && bin_expr.get_rhs().is<ExpressionNode>()) {
							const ExpressionNode& lhs_expr = bin_expr.get_lhs().as<ExpressionNode>();
							const ExpressionNode& rhs_expr = bin_expr.get_rhs().as<ExpressionNode>();
							
							// Case 1: LHS is pack_size_expr (direct or via static_cast), RHS is numeric
							if (auto lhs_pack = try_extract_pack_size(lhs_expr)) {
								if (auto rhs_num = try_extract_numeric(rhs_expr)) {
									unsigned long long result = evaluate_binary(bin_expr.op(), *lhs_pack, *rhs_num);
									substituted_initializer = make_pack_size_literal(static_cast<size_t>(result));
								}
							}
							// Case 2: LHS is numeric, RHS is pack_size_expr
							else if (auto lhs_num = try_extract_numeric(lhs_expr)) {
								if (auto rhs_pack = try_extract_pack_size(rhs_expr)) {
									unsigned long long result = evaluate_binary(bin_expr.op(), *lhs_num, *rhs_pack);
									substituted_initializer = make_pack_size_literal(static_cast<size_t>(result));
								}
							}
							// Case 3: LHS is nested binary expression (e.g., static_cast<int>(sizeof...(Ts)) * 2), RHS is numeric
							// Handles patterns like (static_cast<int>(sizeof...(Ts)) * 2) + 40
							else if (std::holds_alternative<BinaryOperatorNode>(lhs_expr)) {
								const BinaryOperatorNode& nested_bin = std::get<BinaryOperatorNode>(lhs_expr);
								if (nested_bin.get_lhs().is<ExpressionNode>() && nested_bin.get_rhs().is<ExpressionNode>()) {
									const ExpressionNode& nested_lhs = nested_bin.get_lhs().as<ExpressionNode>();
									const ExpressionNode& nested_rhs = nested_bin.get_rhs().as<ExpressionNode>();
									
									std::optional<unsigned long long> nested_result;
									if (auto nlhs_pack = try_extract_pack_size(nested_lhs)) {
										if (auto nrhs_num = try_extract_numeric(nested_rhs)) {
											nested_result = evaluate_binary(nested_bin.op(), *nlhs_pack, *nrhs_num);
										}
									} else if (auto nlhs_num = try_extract_numeric(nested_lhs)) {
										if (auto nrhs_pack = try_extract_pack_size(nested_rhs)) {
											nested_result = evaluate_binary(nested_bin.op(), *nlhs_num, *nrhs_pack);
										}
									}
									
									if (nested_result) {
										if (auto rhs_num = try_extract_numeric(rhs_expr)) {
											unsigned long long result = evaluate_binary(bin_expr.op(), *nested_result, *rhs_num);
											substituted_initializer = make_pack_size_literal(static_cast<size_t>(result));
										}
									}
								}
							}
						}
					}
					// Handle template parameter reference substitution using shared helper lambda
					else if (std::holds_alternative<TemplateParameterReferenceNode>(expr) || 
					         std::holds_alternative<IdentifierNode>(expr)) {
						std::string_view param_name;
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							param_name = std::get<TemplateParameterReferenceNode>(expr).param_name().view();
						} else {
							param_name = std::get<IdentifierNode>(expr).name();
						}
						
						// Use shared helper lambda defined at function scope
						if (auto subst = substitute_template_param_in_initializer(param_name, template_args_to_use, template_params)) {
							substituted_initializer = subst;
							FLASH_LOG(Templates, Debug, "Substituted static member initializer template parameter '", param_name, "'");
						}
					}
					// Handle TernaryOperatorNode where the condition is a template parameter (e.g., IsArith ? 42 : 0)
					else if (std::holds_alternative<TernaryOperatorNode>(expr)) {
						const TernaryOperatorNode& ternary = std::get<TernaryOperatorNode>(expr);
						const ASTNode& cond_node = ternary.condition();
						
						// Check if condition is a template parameter reference or identifier
						if (cond_node.is<ExpressionNode>()) {
							const ExpressionNode& cond_expr = cond_node.as<ExpressionNode>();
							std::optional<int64_t> cond_value;
							
							if (std::holds_alternative<TemplateParameterReferenceNode>(cond_expr)) {
								const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(cond_expr);
								FLASH_LOG(Templates, Debug, "Ternary condition is template parameter: ", tparam_ref.param_name());
								
								// Look up the parameter value
								for (size_t p = 0; p < template_params.size(); ++p) {
									const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
									if (tparam.name() == tparam_ref.param_name() && tparam.kind() == TemplateParameterKind::NonType) {
										if (p < template_args_to_use.size() && template_args_to_use[p].is_value) {
											cond_value = template_args_to_use[p].value;
											FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
										}
										break;
									}
								}
							}
							else if (std::holds_alternative<IdentifierNode>(cond_expr)) {
								const IdentifierNode& id_node = std::get<IdentifierNode>(cond_expr);
								std::string_view id_name = id_node.name();
								FLASH_LOG(Templates, Debug, "Ternary condition is identifier: ", id_name);
								
								// Look up the identifier as a template parameter
								for (size_t p = 0; p < template_params.size(); ++p) {
									const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
									if (tparam.name() == id_name && tparam.kind() == TemplateParameterKind::NonType) {
										if (p < template_args_to_use.size() && template_args_to_use[p].is_value) {
											cond_value = template_args_to_use[p].value;
											FLASH_LOG(Templates, Debug, "Found template param value: ", *cond_value);
										}
										break;
									}
								}
							}
							
							// If we found the condition value, evaluate the ternary
							if (cond_value.has_value()) {
								const ASTNode& result_branch = (*cond_value != 0) ? ternary.true_expr() : ternary.false_expr();
								
								if (result_branch.is<ExpressionNode>()) {
									const ExpressionNode& result_expr = result_branch.as<ExpressionNode>();
									if (std::holds_alternative<NumericLiteralNode>(result_expr)) {
										const NumericLiteralNode& lit = std::get<NumericLiteralNode>(result_expr);
										const auto& val = lit.value();
										unsigned long long num_val = std::holds_alternative<unsigned long long>(val)
											? std::get<unsigned long long>(val)
											: static_cast<unsigned long long>(std::get<double>(val));
										
										// Create a new numeric literal with the evaluated result
										std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(num_val)).commit();
										Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
										substituted_initializer = emplace_node<ExpressionNode>(
											NumericLiteralNode(num_token, num_val, lit.type(), lit.qualifier(), lit.sizeInBits())
										);
										FLASH_LOG(Templates, Debug, "Evaluated ternary to: ", num_val);
									}
								}
							}
						}
					}
				}
				
				// Use struct_info_ptr instead of struct_info (which was moved)
				// Phase 7B: Intern static member name and use StringHandle overload
				StringHandle static_member_name_handle = StringTable::getOrInternStringHandle(StringTable::getStringView(static_member.getName()));
				
				// Check if this static member was already added (e.g., by lazy instantiation path)
				// If it exists but has no initializer, update it with the substituted initializer
				// This ensures lazy instantiation registrations get their initializers filled in
				const StructStaticMember* existing_member = struct_info_ptr->findStaticMember(static_member_name_handle);
				if (existing_member != nullptr) {
					// Member already exists - update the initializer if we have a substituted one
					if (substituted_initializer.has_value()) {
						struct_info_ptr->updateStaticMemberInitializer(static_member_name_handle, substituted_initializer);
					}
					// Skip adding duplicate
				} else {
					struct_info_ptr->addStaticMember(
						static_member_name_handle,
						static_member.type,
						static_member.type_index,
						static_member.size,
						static_member.alignment,
						static_member.access,
						substituted_initializer,  // Use substituted initializer if sizeof... was replaced
						static_member.is_const
					);
				}
			}
		}
	}

	// PHASE 2: Parse deferred template member function bodies (two-phase lookup)
	// Now that TypeInfo is fully created and registered in gTypesByName,
	// we can parse the member function bodies that were deferred during template definition
	// This allows static member lookups to work correctly
	if (!template_class.deferred_bodies().empty()) {
		FLASH_LOG(Templates, Debug, "Parsing ", template_class.deferred_bodies().size(), 
		          " deferred template member function bodies for ", instantiated_name);
		
		// Save current position before parsing deferred bodies
		// We need to restore this after parsing so the parser continues from the correct location
		SaveHandle saved_pos = save_token_position();
		FLASH_LOG(Templates, Debug, "Saved current position: ", saved_pos);
		
		// Parse each deferred body
		// Note: parse_delayed_function_body internally restores to body_start, parses, then leaves position at end of body
		for (const auto& deferred : template_class.deferred_bodies()) {
			FLASH_LOG(Templates, Debug, "About to parse body for ", deferred.function_name, " at position ", deferred.body_start);
			
			// Find the corresponding member function in the instantiated struct
			FunctionDeclarationNode* target_func = nullptr;
			ConstructorDeclarationNode* target_ctor = nullptr;
			DestructorDeclarationNode* target_dtor = nullptr;
			
			// Search in member_functions() which contains all functions, constructors, and destructors
			for (auto& mem_func : instantiated_struct_ref.member_functions()) {
				if (deferred.is_constructor && mem_func.is_constructor) {
					if (mem_func.function_declaration.is<ConstructorDeclarationNode>()) {
						auto& ctor = mem_func.function_declaration.as<ConstructorDeclarationNode>();
						if (ctor.name() == deferred.function_name) {
							target_ctor = &ctor;
							break;
						}
					}
				} else if (deferred.is_destructor && mem_func.is_destructor) {
					if (mem_func.function_declaration.is<DestructorDeclarationNode>()) {
						target_dtor = &mem_func.function_declaration.as<DestructorDeclarationNode>();
						break;
					}
				} else if (!mem_func.is_constructor && !mem_func.is_destructor) {
					// Regular member function
					if (mem_func.function_declaration.is<FunctionDeclarationNode>()) {
						auto& func = mem_func.function_declaration.as<FunctionDeclarationNode>();
						const auto& decl = func.decl_node();
						// Match by name and const qualifier
						if (decl.identifier_token().value() == deferred.function_name &&
						    mem_func.is_const == deferred.is_const_method) {
							target_func = &func;
							break;
						}
					}
				}
			}
			
			if (!target_func && !target_ctor && !target_dtor) {
				FLASH_LOG(Templates, Error, "Could not find member function ", deferred.function_name, 
				          " in instantiated struct ", instantiated_name);
				continue;
			}
			
			// Restore position to the function body
			restore_token_position(deferred.body_start);
			
			// Convert DeferredTemplateMemberBody back to DelayedFunctionBody for parsing
			DelayedFunctionBody delayed;
			delayed.func_node = target_func;
			delayed.body_start = deferred.body_start;
			delayed.initializer_list_start = deferred.initializer_list_start;
			delayed.has_initializer_list = deferred.has_initializer_list;
			delayed.struct_name = instantiated_name;  // Use INSTANTIATED name, not template name
			delayed.struct_type_index = struct_type_info.type_index_;  // Now valid!
			delayed.struct_node = &instantiated_struct_ref;  // Use instantiated struct
			delayed.is_constructor = deferred.is_constructor;
			delayed.is_destructor = deferred.is_destructor;
			delayed.ctor_node = target_ctor;
			delayed.dtor_node = target_dtor;
			// Use template argument names for template parameter substitution
			for (const auto& param_name : deferred.template_param_names) {
				delayed.template_param_names.push_back(param_name);
			}
			
			// Set up template parameter substitution context
			// Map template parameter names to actual types and values
			current_template_param_names_ = delayed.template_param_names;
			
			// Create template parameter substitutions for non-type AND type parameters
			// This allows template parameters like 'v' in 'return v;' to be substituted with actual values
			// and type parameters like '_R1' in '__is_ratio_v<_R1>' to be substituted with actual types
			template_param_substitutions_.clear();
			for (size_t i = 0; i < template_params.size() && i < template_args_to_use.size(); ++i) {
				const auto& param = template_params[i].as<TemplateParameterNode>();
				const auto& arg = template_args_to_use[i];
				
				if (param.kind() == TemplateParameterKind::NonType && arg.is_value) {
					// Non-type parameter - store value for substitution
					TemplateParamSubstitution subst;
					subst.param_name = param.name();
					subst.is_value_param = true;
					subst.value = arg.value;
					subst.value_type = arg.base_type;
					template_param_substitutions_.push_back(subst);
					
					FLASH_LOG(Templates, Debug, "Registered non-type template parameter '", 
					          param.name(), "' with value ", arg.value);
				} else if (param.kind() == TemplateParameterKind::Type && !arg.is_value) {
					// Type parameter - store type for substitution
					// This enables variable templates inside function templates to work correctly:
					// e.g., __is_ratio_v<_R1> where _R1 should be substituted with ratio<1,2>
					TemplateParamSubstitution subst;
					subst.param_name = param.name();
					subst.is_value_param = false;
					subst.is_type_param = true;
					subst.substituted_type = arg;
					template_param_substitutions_.push_back(subst);
					
					FLASH_LOG(Templates, Debug, "Registered type template parameter '", 
					          param.name(), "' with type ", arg.toString());
				}
			}
			
			FLASH_LOG(Templates, Debug, "About to parse deferred body for ", deferred.function_name);
			
			// Parse the body
			std::optional<ASTNode> body;
			auto result = parse_delayed_function_body(delayed, body);
			
			FLASH_LOG(Templates, Debug, "Finished parse_delayed_function_body, result.is_error()=", result.is_error());
			
			current_template_param_names_.clear();
			template_param_substitutions_.clear();  // Clear substitutions after parsing
			
			if (result.is_error()) {
				FLASH_LOG(Templates, Error, "Failed to parse deferred template body: ", result.error_message());
				// Continue with other bodies instead of failing entirely
				continue;
			}
			
			FLASH_LOG(Templates, Debug, "Successfully parsed deferred template body for ", deferred.function_name);
		}
		
		FLASH_LOG(Templates, Debug, "Finished parsing all deferred bodies");
		
		// Restore the position we saved before parsing deferred bodies
		// This ensures the parser continues from the correct location after template instantiation
		FLASH_LOG(Templates, Debug, "About to restore to saved position: ", saved_pos);
		
		// Check if the saved position is still valid
		if (saved_tokens_.find(saved_pos) == saved_tokens_.end()) {
			FLASH_LOG(Templates, Error, "Saved position ", saved_pos, " not found in saved_tokens_!");
		} else {
			FLASH_LOG(Templates, Debug, "Saved position ", saved_pos, " found, restoring...");
			restore_lexer_position_only(saved_pos);
			FLASH_LOG(Templates, Debug, "Restored to saved position");
		}
	}

	FLASH_LOG(Templates, Debug, "About to return instantiated_struct for ", instantiated_name);
	
	// Check if the template class has any constructors
	// If not, mark that we need to generate a default one for the instantiation
	bool has_constructor = false;
	for (const auto& mem_func : class_decl.member_functions()) {
		if (mem_func.is_constructor) {
			has_constructor = true;
			break;
		}
	}
	struct_info_ptr->needs_default_constructor = !has_constructor;
	FLASH_LOG(Templates, Debug, "Instantiated struct ", instantiated_name, " has_constructor=", has_constructor, 
	          ", needs_default_constructor=", struct_info_ptr->needs_default_constructor);
	
	// Re-evaluate deferred static_asserts with substituted template parameters
	FLASH_LOG(Templates, Debug, "Checking deferred static_asserts for struct '", class_decl.name(), 
	          "': found ", class_decl.deferred_static_asserts().size(), " deferred asserts");
	
	for (const auto& deferred_assert : class_decl.deferred_static_asserts()) {
		FLASH_LOG(Templates, Debug, "Re-evaluating deferred static_assert during template instantiation");
		
		// Build template parameter name to type mapping for substitution
		std::unordered_map<std::string_view, TemplateTypeArg> param_map;
		for (size_t i = 0; i < template_params.size() && i < template_args_to_use.size(); ++i) {
			const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
			param_map[param.name()] = template_args_to_use[i];
		}
		
		// Create substitution context with template parameter mappings
		ExpressionSubstitutor substitutor(param_map, *this);
		
		// Substitute template parameters in the condition expression
		ASTNode substituted_expr = substitutor.substitute(deferred_assert.condition_expr);
		
		// Evaluate the substituted expression
		ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
		eval_ctx.parser = this;
		eval_ctx.struct_node = &instantiated_struct.as<StructDeclarationNode>();
		
		auto eval_result = ConstExpr::Evaluator::evaluate(substituted_expr, eval_ctx);
		
		if (!eval_result.success()) {
			std::string error_msg = "static_assert failed during template instantiation: " + 
			                       eval_result.error_message;
			std::string_view message_view = StringTable::getStringView(deferred_assert.message);
			if (!message_view.empty()) {
				error_msg += " - " + std::string(message_view);
			}
			FLASH_LOG(Templates, Error, error_msg);
			// Don't return error - continue with other static_asserts
			// This matches the behavior of most compilers which report all failures
			continue;
		}
		
		// Check if the assertion failed
		if (!eval_result.as_bool()) {
			std::string error_msg = "static_assert failed during template instantiation";
			std::string_view message_view = StringTable::getStringView(deferred_assert.message);
			if (!message_view.empty()) {
				error_msg += ": " + std::string(message_view);
			}
			FLASH_LOG(Templates, Error, error_msg);
			// Don't return error - continue with other static_asserts
			continue;
		}
		
		FLASH_LOG(Templates, Debug, "Deferred static_assert passed during template instantiation");
	}
	
	// Mark instantiation complete with the type index
	FlashCpp::gInstantiationQueue.markComplete(inst_key, struct_type_info.type_index_);
	in_progress_guard.dismiss();  // Don't remove from in_progress in destructor
	
	// Register in V2 cache for O(1) lookup on future instantiations
	gTemplateRegistry.registerInstantiationV2(v2_key, instantiated_struct);
	
	// Return the instantiated struct node for code generation
	return instantiated_struct;
}

// Try to instantiate a member function template during a member function call
// This is called when parsing obj.method(args) where method is a template
std::optional<ASTNode> Parser::try_instantiate_member_function_template(
	std::string_view struct_name,
	std::string_view member_name,
	const std::vector<TypeSpecifierNode>& arg_types) {
	
	// Build the qualified template name
	StringBuilder qualified_name_sb;
	qualified_name_sb.append(struct_name).append("::").append(member_name);
	StringHandle qualified_name = StringTable::getOrInternStringHandle(qualified_name_sb);
	
	// Look up the template in the registry
	auto template_opt = gTemplateRegistry.lookupTemplate(qualified_name);
	
	// If not found and struct_name looks like an instantiated template (e.g., Vector_int),
	// try the base template class name (e.g., Vector::method)
	if (!template_opt.has_value()) {
		// Check if struct_name is an instantiated template class (contains '_' as type separator)
		size_t underscore_pos = struct_name.rfind('_');
		if (underscore_pos != std::string_view::npos) {
			std::string_view base_name = struct_name.substr(0, underscore_pos);
			StringBuilder base_qualified_name_sb;
			base_qualified_name_sb.append(base_name).append("::").append(member_name);
			StringHandle base_qualified_name = StringTable::getOrInternStringHandle(base_qualified_name_sb);
			template_opt = gTemplateRegistry.lookupTemplate(base_qualified_name);
		}
	}
	
	if (!template_opt.has_value()) {
		return std::nullopt;  // Not a template
	}

	const ASTNode& template_node = *template_opt;
	if (!template_node.is<TemplateFunctionDeclarationNode>()) {
		return std::nullopt;  // Not a function template
	}

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

	// Deduce template arguments from function call arguments
	if (arg_types.empty()) {
		return std::nullopt;  // Can't deduce without arguments
	}

	// Build template argument list
	std::vector<TemplateArgument> template_args;
	
	// Deduce template parameters in order from function arguments
	size_t arg_index = 0;
	for (const auto& template_param_node : template_params) {
		const TemplateParameterNode& param = template_param_node.as<TemplateParameterNode>();

		if (param.kind() == TemplateParameterKind::Template) {
			// Template template parameter - cannot be deduced from function arguments
			// Template template parameters must be explicitly specified
			return std::nullopt;
		} else if (param.kind() == TemplateParameterKind::Type) {
			if (arg_index < arg_types.size()) {
				template_args.push_back(TemplateArgument::makeType(arg_types[arg_index].type()));
				arg_index++;
			} else {
				// Not enough arguments - use first argument type
				template_args.push_back(TemplateArgument::makeType(arg_types[0].type()));
			}
		} else {
			// Non-type parameter - not yet supported
			return std::nullopt;
		}
	}

	// Check if we already have this instantiation
	TemplateInstantiationKey key;
	key.template_name = qualified_name;  // Already a StringHandle
	for (const auto& arg : template_args) {
		if (arg.kind == TemplateArgument::Kind::Type) {
			key.type_arguments.push_back(arg.type_value);
		} else if (arg.kind == TemplateArgument::Kind::Template) {
			key.template_arguments.push_back(arg.template_name);
		} else {
			key.value_arguments.push_back(arg.int_value);
		}
	}

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		return *existing_inst;  // Return existing instantiation
	}

	// Generate mangled name for the instantiation
	std::string_view mangled_name = TemplateRegistry::mangleTemplateName(member_name, template_args);

	// Get the original function's declaration
	const DeclarationNode& orig_decl = func_decl.decl_node();

	// Substitute the return type if it's a template parameter
	const TypeSpecifierNode& return_type_spec = orig_decl.type_node().as<TypeSpecifierNode>();
	Type return_type = return_type_spec.type();
	TypeIndex return_type_index = return_type_spec.type_index();

	if (return_type == Type::UserDefined && return_type_index < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[return_type_index];
		std::string_view type_name = StringTable::getStringView(type_info.name());

		// Try to find which template parameter this is
		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
			if (tparam.name() == type_name) {
				return_type = template_args[i].type_value;
				return_type_index = 0;
				break;
			}
		}
	}

	// Create mangled token
	Token mangled_token(Token::Type::Identifier, mangled_name,
	                    orig_decl.identifier_token().line(), orig_decl.identifier_token().column(),
	                    orig_decl.identifier_token().file_index());

	// Create return type node
	ASTNode substituted_return_type = emplace_node<TypeSpecifierNode>(
		return_type,
		TypeQualifier::None,
		get_type_size_bits(return_type),
		Token()
	);
	
	// Copy pointer levels from the original return type specifier
	auto& substituted_return_type_spec = substituted_return_type.as<TypeSpecifierNode>();
	for (const auto& ptr_level : return_type_spec.pointer_levels()) {
		substituted_return_type_spec.add_pointer_level(ptr_level.cv_qualifier);
	}

	// Create the new function declaration
	auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(substituted_return_type, mangled_token);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(new_func_decl_ref, struct_name);

	// Copy and substitute parameters
	for (const auto& param : func_decl.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

			Type param_type = param_type_spec.type();
			TypeIndex param_type_index = param_type_spec.type_index();

			if (param_type == Type::UserDefined && param_type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[param_type_index];
				std::string_view type_name = StringTable::getStringView(type_info.name());

				// Try to find which template parameter this is
				for (size_t i = 0; i < template_params.size(); ++i) {
					const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
					if (tparam.name() == type_name) {
						param_type = template_args[i].type_value;
						param_type_index = 0;
						break;
					}
				}
			}

			// Create the substituted parameter type specifier
			auto substituted_param_type = emplace_node<TypeSpecifierNode>(
				param_type,
				TypeQualifier::None,
				get_type_size_bits(param_type),
				Token()
			);
			
			// Copy pointer levels from the original parameter type specifier
			auto& substituted_param_type_spec = substituted_param_type.as<TypeSpecifierNode>();
			for (const auto& ptr_level : param_type_spec.pointer_levels()) {
				substituted_param_type_spec.add_pointer_level(ptr_level.cv_qualifier);
			}

			// Create the new parameter declaration
			auto new_param_decl = emplace_node<DeclarationNode>(substituted_param_type, param_decl.identifier_token());
			new_func_ref.add_parameter_node(new_param_decl);
		}
	}

	// Check if the template has a body position stored
	if (!func_decl.has_template_body_position()) {
		// No body to parse - compute mangled name for proper linking and symbol resolution
		// Even without a body, the mangled name is needed for code generation and linking
		compute_and_set_mangled_name(new_func_ref);
		ast_nodes_.push_back(new_func_node);
		gTemplateRegistry.registerInstantiation(key, new_func_node);
		return new_func_node;
	}
	
	// Temporarily add the concrete types to the type system with template parameter names
	// Using RAII scope guard (Phase 6) for automatic cleanup
	FlashCpp::TemplateParameterScope template_scope;
	std::vector<std::string_view> param_names;
	for (const auto& tparam_node : template_params) {
		if (tparam_node.is<TemplateParameterNode>()) {
			param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
		}
	}
	
	for (size_t i = 0; i < param_names.size() && i < template_args.size(); ++i) {
		std::string_view param_name = param_names[i];
		Type concrete_type = template_args[i].type_value;

		auto& type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(param_name), concrete_type, gTypeInfo.size(), getTypeSizeFromTemplateArgument(template_args[i]));
		gTypesByName.emplace(type_info.name(), &type_info);
		template_scope.addParameter(&type_info);  // RAII cleanup on all return paths
	}

	// Save current position
	SaveHandle current_pos = save_token_position();

	// Restore to the function body start (lexer only - keep AST nodes from previous instantiations)
	restore_lexer_position_only(func_decl.template_body_position());

	// Look up the struct type info
	auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
	if (struct_type_it == gTypesByName.end()) {
		// Clean up and return error - template_scope RAII handles type cleanup
		restore_token_position(current_pos);
		return std::nullopt;
	}

	const TypeInfo* struct_type_info = struct_type_it->second;
	TypeIndex struct_type_index = struct_type_info->type_index_;

	// Set up parsing context for the member function
	gSymbolTable.enter_scope(ScopeType::Function);
	current_function_ = &new_func_ref;
		
	// Find the struct node
	StructDeclarationNode* struct_node_ptr = nullptr;
	for (auto& node : ast_nodes_) {
		if (node.is<StructDeclarationNode>()) {
			auto& sn = node.as<StructDeclarationNode>();
			if (sn.name() == struct_name) {
				struct_node_ptr = &sn;
				break;
			}
		}
	}
		
	// If not found and this is a member function template of a template class,
	// look for the base template class struct to get member info
	if (!struct_node_ptr || struct_node_ptr->members().empty()) {
		// Check if struct_name looks like an instantiated template class (contains '_' as type separator)
		size_t underscore_pos = struct_name.rfind('_');
		if (underscore_pos != std::string_view::npos) {
			std::string_view base_name = struct_name.substr(0, underscore_pos);
			for (auto& node : ast_nodes_) {
				if (node.is<StructDeclarationNode>()) {
					auto& sn = node.as<StructDeclarationNode>();
					if (sn.name() == base_name) {
						// Use the base template struct for member info
						// The members are the same, just with template parameter types
						struct_node_ptr = &sn;
						break;
					}
				}
			}
		}
	}
		
	member_function_context_stack_.push_back({
		StringTable::getOrInternStringHandle(struct_name),
		struct_type_index,
		struct_node_ptr,
		nullptr  // local_struct_info - not needed for out-of-class member function definitions
	});

	// Add 'this' pointer to symbol table
	ASTNode this_type = emplace_node<TypeSpecifierNode>(
		Type::UserDefined,
		struct_type_index,
		64,  // Pointer size
		Token()
	);

	Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
	auto this_decl = emplace_node<DeclarationNode>(this_type, this_token);
	gSymbolTable.insert("this"sv, this_decl);

	// Add parameters to symbol table
	for (const auto& param : new_func_ref.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const auto& param_decl = param.as<DeclarationNode>();
			gSymbolTable.insert(param_decl.identifier_token().value(), param);
		}
	}

	// Parse the function body
	auto block_result = parse_block();
	if (!block_result.is_error() && block_result.node().has_value()) {
		new_func_ref.set_definition(*block_result.node());
	}

	// Clean up context
	current_function_ = nullptr;
	member_function_context_stack_.pop_back();
	gSymbolTable.exit_scope();

	// Restore original position (lexer only - keep AST nodes we created)
	restore_lexer_position_only(current_pos);

	// template_scope RAII guard automatically removes temporary type infos

	// Add the instantiated function to the AST
	ast_nodes_.push_back(new_func_node);

	// Update the saved position to include this new node so it doesn't get erased
	// when we restore position in the caller
	// Update the saved position to include this new node (current_pos is already a SaveKey)
	saved_tokens_[current_pos].ast_nodes_size_ = ast_nodes_.size();

	// Compute and set the proper mangled name (Itanium/MSVC) for code generation
	compute_and_set_mangled_name(new_func_ref);
	
	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	return new_func_node;
}

// Instantiate member function template with explicit template arguments
// Example: obj.convert<int>(42)
std::optional<ASTNode> Parser::try_instantiate_member_function_template_explicit(
	std::string_view struct_name,
	std::string_view member_name,
	const std::vector<TemplateTypeArg>& template_type_args) {
	
	// Build the qualified template name using StringBuilder
	StringBuilder qualified_name_sb;
	qualified_name_sb.append(struct_name).append("::").append(member_name);
	StringHandle qualified_name = StringTable::getOrInternStringHandle(qualified_name_sb);
	
	// FIRST: Check if we have an explicit specialization for these template arguments
	auto specialization_opt = gTemplateRegistry.lookupSpecialization(qualified_name.view(), template_type_args);
	if (specialization_opt.has_value()) {
		FLASH_LOG(Templates, Debug, "Found explicit specialization for ", qualified_name.view());
		// We have an explicit specialization - parse its body if needed
		const ASTNode& spec_node = *specialization_opt;
		if (spec_node.is<FunctionDeclarationNode>()) {
			FunctionDeclarationNode& spec_func = const_cast<FunctionDeclarationNode&>(spec_node.as<FunctionDeclarationNode>());
			
			// If the specialization has a body position and no definition yet, parse it now
			if (spec_func.has_template_body_position() && !spec_func.get_definition().has_value()) {
				FLASH_LOG(Templates, Debug, "Parsing specialization body for ", qualified_name.view());
				
				// Look up the struct type index and node for the member function context
				TypeIndex struct_type_index = 0;
				StructDeclarationNode* struct_node_ptr = nullptr;
				auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
				if (struct_type_it != gTypesByName.end()) {
					struct_type_index = struct_type_it->second->type_index_;
					
					// Try to find the struct node in the symbol table
					auto struct_symbol_opt = lookup_symbol(StringTable::getOrInternStringHandle(struct_name));
					if (struct_symbol_opt.has_value() && struct_symbol_opt->is<StructDeclarationNode>()) {
						struct_node_ptr = &const_cast<StructDeclarationNode&>(struct_symbol_opt->as<StructDeclarationNode>());
					}
				}
				
				// Save the current position
				SaveHandle saved_pos = save_token_position();
				
				// Enter a function scope
				gSymbolTable.enter_scope(ScopeType::Function);
				
				// Set up member function context
				member_function_context_stack_.push_back({
					StringTable::getOrInternStringHandle(struct_name),
					struct_type_index,
					struct_node_ptr,
					nullptr  // local_struct_info - not needed for specialization functions
				});
				
				// Add parameters to symbol table
				for (const auto& param : spec_func.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& param_decl = param.as<DeclarationNode>();
						gSymbolTable.insert(param_decl.identifier_token().value(), param);
					}
				}
				
				// Restore to the body position
				restore_lexer_position_only(spec_func.template_body_position());
				
				// Parse the function body
				auto body_result = parse_block();
				
				// Clean up member function context
				if (!member_function_context_stack_.empty()) {
					member_function_context_stack_.pop_back();
				}
				
				// Exit the function scope
				gSymbolTable.exit_scope();
				
				// Restore the original position
				restore_lexer_position_only(saved_pos);
				
				if (body_result.is_error() || !body_result.node().has_value()) {
					FLASH_LOG(Templates, Error, "Failed to parse specialization body: ", body_result.error_message());
				} else {
					spec_func.set_definition(*body_result.node());
					FLASH_LOG(Templates, Debug, "Successfully parsed specialization body");
					
					// Add the specialization to ast_nodes_ so it gets code generated
					// We need to do this because the specialization was created during parsing
					// but may not have been added to the top-level AST
					ast_nodes_.push_back(spec_node);
					FLASH_LOG(Templates, Debug, "Added specialization to AST for code generation");
				}
			}
			
			return spec_node;
		}
	}
	
	// Look up the template in the registry
	auto template_opt = gTemplateRegistry.lookupTemplate(qualified_name);
	
	// If not found, try with the base template class name
	// For instantiated classes like Helper_int, try Helper::member
	if (!template_opt.has_value()) {
		size_t underscore_pos = struct_name.rfind('_');
		if (underscore_pos != std::string_view::npos) {
			std::string_view base_class_name = struct_name.substr(0, underscore_pos);
			StringBuilder base_qualified_name_sb;
			base_qualified_name_sb.append(base_class_name).append("::").append(member_name);
			StringHandle base_qualified_name = StringTable::getOrInternStringHandle(base_qualified_name_sb);
			template_opt = gTemplateRegistry.lookupTemplate(base_qualified_name);
			FLASH_LOG(Templates, Debug, "Trying base template class lookup: ", base_qualified_name.view());
		}
	}
	
	if (!template_opt.has_value()) {
		return std::nullopt;  // Not a template
	}

	const ASTNode& template_node = *template_opt;
	if (!template_node.is<TemplateFunctionDeclarationNode>()) {
		return std::nullopt;  // Not a function template
	}

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

	// Convert TemplateTypeArg to TemplateArgument
	std::vector<TemplateArgument> template_args;
	for (const auto& type_arg : template_type_args) {
		template_args.push_back(TemplateArgument::makeType(type_arg.base_type));
	}

	// Check if we already have this instantiation
	TemplateInstantiationKey key;
	key.template_name = qualified_name;  // Already a StringHandle
	for (const auto& arg : template_args) {
		if (arg.kind == TemplateArgument::Kind::Type) {
			key.type_arguments.push_back(arg.type_value);
		} else if (arg.kind == TemplateArgument::Kind::Template) {
			key.template_arguments.push_back(arg.template_name);
		} else {
			key.value_arguments.push_back(arg.int_value);
		}
	}

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		return *existing_inst;  // Return existing instantiation
	}

	// Generate mangled name for the instantiation
	std::string_view mangled_name = TemplateRegistry::mangleTemplateName(member_name, template_args);

	// Get the original function's declaration
	const DeclarationNode& orig_decl = func_decl.decl_node();

	// Substitute the return type if it's a template parameter
	const TypeSpecifierNode& return_type_spec = orig_decl.type_node().as<TypeSpecifierNode>();
	Type return_type = return_type_spec.type();
	TypeIndex return_type_index = return_type_spec.type_index();

	if (return_type == Type::UserDefined && return_type_index < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[return_type_index];
		std::string_view type_name = StringTable::getStringView(type_info.name());

		// Try to find which template parameter this is
		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
			if (tparam.name() == type_name && i < template_args.size()) {
				return_type = template_args[i].type_value;
				return_type_index = 0;
				break;
			}
		}
	}

	// Create mangled token
	Token mangled_token(Token::Type::Identifier, mangled_name,
	                    orig_decl.identifier_token().line(), orig_decl.identifier_token().column(),
	                    orig_decl.identifier_token().file_index());

	// Create return type node
	ASTNode substituted_return_type = emplace_node<TypeSpecifierNode>(
		return_type,
		TypeQualifier::None,
		get_type_size_bits(return_type),
		Token()
	);
	
	// Copy pointer levels from the original return type specifier
	auto& substituted_return_type_spec = substituted_return_type.as<TypeSpecifierNode>();
	for (const auto& ptr_level : return_type_spec.pointer_levels()) {
		substituted_return_type_spec.add_pointer_level(ptr_level.cv_qualifier);
	}

	// Create the new function declaration
	auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(substituted_return_type, mangled_token);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(new_func_decl_ref, struct_name);

	// Copy and substitute parameters
	for (const auto& param : func_decl.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

			Type param_type = param_type_spec.type();
			TypeIndex param_type_index = param_type_spec.type_index();

			if (param_type == Type::UserDefined && param_type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[param_type_index];
				std::string_view type_name = StringTable::getStringView(type_info.name());

				// Try to find which template parameter this is
				for (size_t i = 0; i < template_params.size(); ++i) {
					const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
					if (tparam.name() == type_name && i < template_args.size()) {
						param_type = template_args[i].type_value;
						param_type_index = 0;
						break;
					}
				}
			}

			// Create the substituted parameter type specifier
			auto substituted_param_type = emplace_node<TypeSpecifierNode>(
				param_type,
				TypeQualifier::None,
				get_type_size_bits(param_type),
				Token()
			);
			
			// Copy pointer levels from the original parameter type specifier
			auto& substituted_param_type_spec = substituted_param_type.as<TypeSpecifierNode>();
			for (const auto& ptr_level : param_type_spec.pointer_levels()) {
				substituted_param_type_spec.add_pointer_level(ptr_level.cv_qualifier);
			}

			// Create the new parameter declaration
			auto new_param_decl = emplace_node<DeclarationNode>(substituted_param_type, param_decl.identifier_token());
			new_func_ref.add_parameter_node(new_param_decl);
		}
	}

	// Check if the template has a body position stored
	if (!func_decl.has_template_body_position()) {
		// No body to parse - compute mangled name for proper linking and symbol resolution
		// Even without a body, the mangled name is needed for code generation and linking
		compute_and_set_mangled_name(new_func_ref);
		ast_nodes_.push_back(new_func_node);
		gTemplateRegistry.registerInstantiation(key, new_func_node);
		return new_func_node;
	}

	// Temporarily add the concrete types to the type system with template parameter names
	// Using RAII scope guard (Phase 6) for automatic cleanup
	FlashCpp::TemplateParameterScope template_scope;
	std::vector<std::string_view> param_names;
	for (const auto& tparam_node : template_params) {
		if (tparam_node.is<TemplateParameterNode>()) {
			param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
		}
	}
	
	for (size_t i = 0; i < param_names.size() && i < template_args.size(); ++i) {
		std::string_view param_name = param_names[i];
		Type concrete_type = template_args[i].type_value;

		// TypeInfo constructor requires std::string, but we keep param_name as string_view elsewhere
		auto& type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(param_name), concrete_type, gTypeInfo.size(), getTypeSizeFromTemplateArgument(template_args[i]));
		gTypesByName.emplace(type_info.name(), &type_info);
		template_scope.addParameter(&type_info);  // RAII cleanup on all return paths
	}

	// Save current position
	SaveHandle current_pos = save_token_position();

	// Restore to the function body start (lexer only - keep AST nodes from previous instantiations)
	restore_lexer_position_only(func_decl.template_body_position());

	// Look up the struct type info
	auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
	if (struct_type_it == gTypesByName.end()) {
		FLASH_LOG(Templates, Debug, "Struct type not found: ", struct_name);
		// Clean up and return error - template_scope RAII handles type cleanup
		restore_token_position(current_pos);
		return std::nullopt;
	}

	const TypeInfo* struct_type_info = struct_type_it->second;
	TypeIndex struct_type_index = struct_type_info->type_index_;

	// Set up parsing context for the member function
	gSymbolTable.enter_scope(ScopeType::Function);
	current_function_ = &new_func_ref;
	
	// Find the struct node
	StructDeclarationNode* struct_node_ptr = nullptr;
	for (auto& node : ast_nodes_) {
		if (node.is<StructDeclarationNode>()) {
			auto& sn = node.as<StructDeclarationNode>();
			if (sn.name() == struct_name) {
				struct_node_ptr = &sn;
				break;
			}
		}
	}
	
	member_function_context_stack_.push_back({
		StringTable::getOrInternStringHandle(struct_name),
		struct_type_index,
		struct_node_ptr,
		nullptr  // local_struct_info - not needed for out-of-class member function definitions
	});

	// Add 'this' pointer to symbol table
	ASTNode this_type = emplace_node<TypeSpecifierNode>(
		Type::UserDefined,
		struct_type_index,
		64,  // Pointer size
		Token()
	);

	Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
	auto this_decl = emplace_node<DeclarationNode>(this_type, this_token);
	gSymbolTable.insert("this"sv, this_decl);

	// Add parameters to symbol table
	for (const auto& param : new_func_ref.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const auto& param_decl = param.as<DeclarationNode>();
			gSymbolTable.insert(param_decl.identifier_token().value(), param);
		}
	}

	// Parse the function body
	auto block_result = parse_block();
	if (!block_result.is_error() && block_result.node().has_value()) {
		new_func_ref.set_definition(*block_result.node());
	}

	// Clean up context
	current_function_ = nullptr;
	member_function_context_stack_.pop_back();
	gSymbolTable.exit_scope();

	// Restore original position (lexer only - keep AST nodes we created)
	restore_lexer_position_only(current_pos);

	// template_scope RAII guard automatically removes temporary type infos

	// Add the instantiated function to the AST
	ast_nodes_.push_back(new_func_node);

	// Update the saved position to include this new node so it doesn't get erased
	// when we restore position in the caller
	// Update the saved position to include this new node (current_pos is already a SaveKey)
	saved_tokens_[current_pos].ast_nodes_size_ = ast_nodes_.size();

	// Compute and set the proper mangled name (Itanium/MSVC) for code generation
	compute_and_set_mangled_name(new_func_ref);
	
	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	return new_func_node;
}

// Instantiate a lazy member function on-demand
// This performs the template parameter substitution that was deferred during lazy registration
std::optional<ASTNode> Parser::instantiateLazyMemberFunction(const LazyMemberFunctionInfo& lazy_info) {
	FLASH_LOG(Templates, Debug, "instantiateLazyMemberFunction: ", 
	          lazy_info.instantiated_class_name, "::", lazy_info.member_function_name);
	
	// Get the original function declaration
	if (!lazy_info.original_function_node.is<FunctionDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Lazy member function node is not a FunctionDeclarationNode");
		return std::nullopt;
	}
	
	const FunctionDeclarationNode& func_decl = lazy_info.original_function_node.as<FunctionDeclarationNode>();
	const DeclarationNode& decl = func_decl.decl_node();
	
	if (!func_decl.get_definition().has_value() && !func_decl.has_template_body_position()) {
		FLASH_LOG(Templates, Error, "Lazy member function has no definition and no deferred body position");
		return std::nullopt;
	}
	
	// Perform template parameter substitution (same as eager path)
	// Substitute return type
	const TypeSpecifierNode& return_type_spec = decl.type_node().as<TypeSpecifierNode>();
	auto [return_type, return_type_index] = substitute_template_parameter(
		return_type_spec, lazy_info.template_params, lazy_info.template_args
	);

	// Create substituted return type node
	TypeSpecifierNode substituted_return_type(
		return_type,
		return_type_spec.qualifier(),
		get_type_size_bits(return_type),
		decl.identifier_token()
	);
	substituted_return_type.set_type_index(return_type_index);

	// Copy pointer levels and reference qualifiers from original
	for (const auto& ptr_level : return_type_spec.pointer_levels()) {
		substituted_return_type.add_pointer_level(ptr_level.cv_qualifier);
	}
	if (return_type_spec.is_rvalue_reference()) {
		substituted_return_type.set_reference(true);
	} else if (return_type_spec.is_reference()) {
		substituted_return_type.set_reference(false);
	}

	auto substituted_return_node = emplace_node<TypeSpecifierNode>(substituted_return_type);

	// Create a new function declaration with substituted return type
	auto [new_func_decl_node, new_func_decl_ref] = emplace_node_ref<DeclarationNode>(
		substituted_return_node, decl.identifier_token()
	);
	auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
		new_func_decl_ref, lazy_info.instantiated_class_name
	);

	// Substitute and copy parameters
	for (const auto& param : func_decl.parameter_nodes()) {
		if (param.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();

			// Substitute parameter type
			auto [param_type, param_type_index] = substitute_template_parameter(
				param_type_spec, lazy_info.template_params, lazy_info.template_args
			);

			// Create substituted parameter type
			TypeSpecifierNode substituted_param_type(
				param_type,
				param_type_spec.qualifier(),
				get_type_size_bits(param_type),
				param_decl.identifier_token(),
				param_type_spec.cv_qualifier()
			);
			substituted_param_type.set_type_index(param_type_index);

			// Copy pointer levels and reference qualifiers
			for (const auto& ptr_level : param_type_spec.pointer_levels()) {
				substituted_param_type.add_pointer_level(ptr_level.cv_qualifier);
			}
			if (param_type_spec.is_rvalue_reference()) {
				substituted_param_type.set_reference(true);
			} else if (param_type_spec.is_reference()) {
				substituted_param_type.set_reference(false);
			}

			auto substituted_param_type_node = emplace_node<TypeSpecifierNode>(substituted_param_type);
			auto substituted_param_decl = emplace_node<DeclarationNode>(
				substituted_param_type_node, param_decl.identifier_token()
			);
			// Copy default value if present
if (param_decl.has_default_value()) {
// Substitute template parameters in the default value expression
							std::unordered_map<std::string_view, TemplateTypeArg> param_map;
							// Note: In this context, we don't have easy access to template parameter order
							// so we fallback to the original approach for now
							ExpressionSubstitutor substitutor(param_map, *this);
							std::optional<ASTNode> substituted_default = substitutor.substitute(param_decl.default_value());
				if (substituted_default.has_value()) {
					substituted_param_decl.as<DeclarationNode>().set_default_value(*substituted_default);
				}
			}
			new_func_ref.add_parameter_node(substituted_param_decl);
		} else {
			// Non-declaration parameter, copy as-is
			new_func_ref.add_parameter_node(param);
		}
	}

	// Get the function body - either from definition or by re-parsing from saved position
	std::optional<ASTNode> body_to_substitute;
	
	if (func_decl.get_definition().has_value()) {
		// Use the already-parsed definition
		body_to_substitute = func_decl.get_definition();
	} else if (func_decl.has_template_body_position()) {
		// Re-parse the function body from saved position
		// This is needed for member struct templates where body parsing is deferred
		
		// Set up template parameter types in the type system for body parsing
		FlashCpp::TemplateParameterScope template_scope;
		std::vector<std::string_view> param_names;
		param_names.reserve(lazy_info.template_params.size());
		for (const auto& tparam_node : lazy_info.template_params) {
			if (tparam_node.is<TemplateParameterNode>()) {
				param_names.push_back(tparam_node.as<TemplateParameterNode>().name());
			}
		}
		
		for (size_t i = 0; i < param_names.size() && i < lazy_info.template_args.size(); ++i) {
			std::string_view param_name = param_names[i];
			Type concrete_type = lazy_info.template_args[i].base_type;

			auto& type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(param_name), concrete_type, gTypeInfo.size(), get_type_size_bits(concrete_type));
			
			// Copy reference qualifiers from template arg
			type_info.is_reference_ = lazy_info.template_args[i].is_reference;
			type_info.is_rvalue_reference_ = lazy_info.template_args[i].is_rvalue_reference;
			
			gTypesByName.emplace(type_info.name(), &type_info);
			template_scope.addParameter(&type_info);
		}

		// Save current position and parsing context
		SaveHandle current_pos = save_token_position();
		const FunctionDeclarationNode* saved_current_function = current_function_;

		// Restore to the function body start
		restore_lexer_position_only(func_decl.template_body_position());

		// Set up parsing context for the function
		gSymbolTable.enter_scope(ScopeType::Function);
		current_function_ = &new_func_ref;

		// Add parameters to symbol table
		for (const auto& param : new_func_ref.parameter_nodes()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				gSymbolTable.insert(param_decl.identifier_token().value(), param);
			}
		}

		// Parse the function body
		auto block_result = parse_block();
		
		if (!block_result.is_error() && block_result.node().has_value()) {
			body_to_substitute = block_result.node();
		}
		
		// Clean up context
		current_function_ = saved_current_function;
		gSymbolTable.exit_scope();

		// Restore original position
		restore_lexer_position_only(current_pos);
		discard_saved_token(current_pos);
	}

	// Substitute template parameters in the function body
	if (body_to_substitute.has_value()) {
		// Convert TemplateTypeArg vector to TemplateArgument vector
		std::vector<TemplateArgument> converted_template_args;
		for (const auto& ttype_arg : lazy_info.template_args) {
			if (ttype_arg.is_value) {
				converted_template_args.push_back(TemplateArgument::makeValue(ttype_arg.value, ttype_arg.base_type));
			} else {
				converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type));
			}
		}

		try {
			ASTNode substituted_body = substituteTemplateParameters(
				*body_to_substitute,
				lazy_info.template_params,
				converted_template_args
			);
			new_func_ref.set_definition(substituted_body);
		} catch (const std::exception& e) {
			FLASH_LOG(Templates, Error, "Exception during lazy template parameter substitution for function ", 
			          decl.identifier_token().value(), ": ", e.what());
			throw;
		} catch (...) {
			FLASH_LOG(Templates, Error, "Unknown exception during lazy template parameter substitution for function ", 
			          decl.identifier_token().value());
			throw;
		}
	}

	// Copy function properties
	new_func_ref.set_is_constexpr(func_decl.is_constexpr());
	new_func_ref.set_is_consteval(func_decl.is_consteval());
	new_func_ref.set_is_constinit(func_decl.is_constinit());
	new_func_ref.set_noexcept(func_decl.is_noexcept());
	new_func_ref.set_is_variadic(func_decl.is_variadic());
	new_func_ref.set_linkage(func_decl.linkage());
	new_func_ref.set_calling_convention(func_decl.calling_convention());

	// Add the instantiated function to the AST so it gets visited during codegen
	// This is safe now that the StringBuilder bug is fixed
	ast_nodes_.push_back(new_func_node);
	
	// Also update the StructTypeInfo to replace the signature-only function with the full definition
	// Find the struct in gTypesByName
	auto struct_it = gTypesByName.find(lazy_info.instantiated_class_name);
	if (struct_it != gTypesByName.end()) {
		const TypeInfo* struct_type_info = struct_it->second;
		StructTypeInfo* struct_info = const_cast<StructTypeInfo*>(struct_type_info->getStructInfo());
		if (struct_info) {
			// Find and update the member function
			for (auto& member_func : struct_info->member_functions) {
				if (member_func.getName() == lazy_info.member_function_name) {
					// Replace with the instantiated function
					member_func.function_decl = new_func_node;
					FLASH_LOG(Templates, Debug, "Updated StructTypeInfo with instantiated function body");
					break;
				}
			}
		}
	}
	
	FLASH_LOG(Templates, Debug, "Successfully instantiated lazy member function: ", 
	          lazy_info.instantiated_class_name, "::", lazy_info.member_function_name);
	
	return new_func_node;
}

// Instantiate a lazy static member on-demand
// This is called when a static member is accessed for the first time
// Returns true if instantiation was performed, false if not needed or failed
bool Parser::instantiateLazyStaticMember(StringHandle instantiated_class_name, StringHandle member_name) {
	// Check if this member needs lazy instantiation
	if (!LazyStaticMemberRegistry::getInstance().needsInstantiation(instantiated_class_name, member_name)) {
		return false;  // Not registered for lazy instantiation
	}
	
	FLASH_LOG(Templates, Debug, "Lazy instantiation triggered for static member: ", 
	          instantiated_class_name, "::", member_name);
	
	// Get the lazy member info (returns a pointer to avoid copying)
	const LazyStaticMemberInfo* lazy_info_ptr = LazyStaticMemberRegistry::getInstance().getLazyStaticMemberInfo(
		instantiated_class_name, member_name);
	if (!lazy_info_ptr) {
		FLASH_LOG(Templates, Error, "Failed to get lazy static member info for: ", 
		          instantiated_class_name, "::", member_name);
		return false;
	}
	
	const LazyStaticMemberInfo& lazy_info = *lazy_info_ptr;
	
	// Find the struct_info to add the member to
	auto type_it = gTypesByName.find(instantiated_class_name);
	if (type_it == gTypesByName.end()) {
		FLASH_LOG(Templates, Error, "Failed to find struct info for: ", instantiated_class_name);
		return false;
	}
	
	// Need const_cast because gTypesByName stores const TypeInfo*, but we need to modify StructTypeInfo
	StructTypeInfo* struct_info = const_cast<StructTypeInfo*>(type_it->second->getStructInfo());
	if (!struct_info) {
		FLASH_LOG(Templates, Error, "Type is not a struct: ", instantiated_class_name);
		return false;
	}
	
	// Perform initializer substitution if needed
	std::optional<ASTNode> substituted_initializer = lazy_info.initializer;
	
	if (lazy_info.needs_substitution && lazy_info.initializer.has_value() && 
	    lazy_info.initializer->is<ExpressionNode>()) {
		const ExpressionNode& expr = lazy_info.initializer->as<ExpressionNode>();
		const std::vector<ASTNode>& template_params = lazy_info.template_params;
		const std::vector<TemplateTypeArg>& template_args = lazy_info.template_args;
		
		// Helper to calculate pack size for substitution
		auto calculate_pack_size = [&](std::string_view pack_name) -> std::optional<size_t> {
			for (size_t i = 0; i < template_params.size(); ++i) {
				if (!template_params[i].is<TemplateParameterNode>()) continue;
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.name() == pack_name && tparam.is_variadic()) {
					size_t non_variadic_count = 0;
					for (const auto& param : template_params) {
						if (param.is<TemplateParameterNode>() && !param.as<TemplateParameterNode>().is_variadic()) {
							non_variadic_count++;
						}
					}
					return template_args.size() - non_variadic_count;
				}
			}
			return std::nullopt;
		};
		
		// Helper to create a numeric literal from pack size
		auto make_pack_size_literal = [&](size_t pack_size) -> ASTNode {
			std::string_view pack_size_str = StringBuilder().append(pack_size).commit();
			Token num_token(Token::Type::Literal, pack_size_str, 0, 0, 0);
			return emplace_node<ExpressionNode>(
				NumericLiteralNode(num_token, static_cast<unsigned long long>(pack_size), Type::Int, TypeQualifier::None, 32)
			);
		};
		
		// Handle SizeofPackNode
		if (std::holds_alternative<SizeofPackNode>(expr)) {
			const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(expr);
			if (auto pack_size = calculate_pack_size(sizeof_pack.pack_name())) {
				substituted_initializer = make_pack_size_literal(*pack_size);
			}
		}
		// Handle FoldExpressionNode
		else if (std::holds_alternative<FoldExpressionNode>(expr)) {
			const FoldExpressionNode& fold = std::get<FoldExpressionNode>(expr);
			std::string_view pack_name = fold.pack_name();
			std::string_view op = fold.op();
			
			// Find the parameter pack
			std::optional<size_t> pack_param_idx;
			for (size_t p = 0; p < template_params.size(); ++p) {
				if (!template_params[p].is<TemplateParameterNode>()) continue;
				const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
				if (tparam.name() == pack_name && tparam.is_variadic()) {
					pack_param_idx = p;
					break;
				}
			}
			
			if (pack_param_idx.has_value()) {
				// Collect pack values
				std::vector<int64_t> pack_values;
				bool all_values_found = true;
				
				size_t non_variadic_count = 0;
				for (const auto& param : template_params) {
					if (param.is<TemplateParameterNode>() && !param.as<TemplateParameterNode>().is_variadic()) {
						non_variadic_count++;
					}
				}
				
				for (size_t i = non_variadic_count; i < template_args.size() && all_values_found; ++i) {
					if (template_args[i].is_value) {
						pack_values.push_back(template_args[i].value);
					} else {
						all_values_found = false;
					}
				}
				
				if (all_values_found && !pack_values.empty()) {
					auto fold_result = ConstExpr::evaluate_fold_expression(op, pack_values);
					if (fold_result.has_value()) {
						// Create a bool literal for && and ||, numeric for others
						if (op == "&&" || op == "||") {
							Token bool_token(Token::Type::Keyword, *fold_result ? "true"sv : "false"sv, 0, 0, 0);
							substituted_initializer = emplace_node<ExpressionNode>(
								BoolLiteralNode(bool_token, *fold_result != 0)
							);
						} else {
							std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(*fold_result)).commit();
							Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
							substituted_initializer = emplace_node<ExpressionNode>(
								NumericLiteralNode(num_token, static_cast<unsigned long long>(*fold_result), Type::Int, TypeQualifier::None, 64)
							);
						}
					}
				}
			}
		}
		// Handle TemplateParameterReferenceNode
		else if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
			const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
			if (auto subst = substitute_nontype_template_param(
			        tparam_ref.param_name().view(), template_args, template_params)) {
				substituted_initializer = subst;
			}
		}
		// Handle IdentifierNode that might be a template parameter
		else if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
			if (auto subst = substitute_nontype_template_param(
			        id_node.name(), template_args, template_params)) {
				substituted_initializer = subst;
			}
		}
		
		// General fallback: Use ExpressionSubstitutor for any remaining template-dependent expressions
		// This handles expressions like __v<T> (variable template invocations with template parameters)
		// Check if we still have the original initializer (i.e., no specific handler above modified it)
		bool was_substituted = false;
		if (std::holds_alternative<FoldExpressionNode>(expr)) was_substituted = true;
		if (std::holds_alternative<SizeofPackNode>(expr)) was_substituted = true;
		if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) was_substituted = true;
		// IdentifierNode only gets substituted if it matches a template parameter
		
		if (!was_substituted) {
			// Use ExpressionSubstitutor for general template parameter substitution
			std::unordered_map<std::string_view, TemplateTypeArg> param_map;
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				if (template_params[i].is<TemplateParameterNode>()) {
					const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
					param_map[param.name()] = template_args[i];
				}
			}
			
			if (!param_map.empty()) {
				ExpressionSubstitutor substitutor(param_map, *this);
				substituted_initializer = substitutor.substitute(lazy_info.initializer.value());
				FLASH_LOG(Templates, Debug, "Applied general template parameter substitution to lazy static member initializer");
			}
		}
	}
	
	// Perform type substitution
	TypeSpecifierNode original_type_spec(lazy_info.type, TypeQualifier::None, lazy_info.size * 8);
	original_type_spec.set_type_index(lazy_info.type_index);
	
	auto [substituted_type, substituted_type_index] = substitute_template_parameter(
		original_type_spec, lazy_info.template_params, lazy_info.template_args);
	
	size_t substituted_size = get_type_size_bits(substituted_type) / 8;
	
	// Update the existing static member with the computed initializer
	// (The member was already added during template instantiation with std::nullopt initializer)
	if (!struct_info->updateStaticMemberInitializer(lazy_info.member_name, substituted_initializer)) {
		// Member doesn't exist yet - add it (shouldn't normally happen with lazy instantiation)
		// Convert CVQualifier back to bool for addStaticMember (which expects bool is_const)
		bool is_const = (lazy_info.cv_qualifier == CVQualifier::Const || lazy_info.cv_qualifier == CVQualifier::ConstVolatile);
		struct_info->addStaticMember(
			lazy_info.member_name,
			substituted_type,
			substituted_type_index,
			substituted_size,
			lazy_info.alignment,
			lazy_info.access,
			substituted_initializer,
			is_const
		);
	}
	
	// Mark as instantiated (remove from lazy registry)
	LazyStaticMemberRegistry::getInstance().markInstantiated(instantiated_class_name, member_name);
	
	FLASH_LOG(Templates, Debug, "Successfully instantiated lazy static member: ", 
	          instantiated_class_name, "::", member_name);
	
	return true;
}

// Phase 2: Instantiate a lazy class to the specified phase
// Returns true if instantiation was performed or already at/past target phase, false on failure
bool Parser::instantiateLazyClassToPhase(StringHandle instantiated_name, ClassInstantiationPhase target_phase) {
	auto& registry = LazyClassInstantiationRegistry::getInstance();
	
	// Check if the class is registered for lazy instantiation
	if (!registry.isRegistered(instantiated_name)) {
		// Not a lazily instantiated class - might be already fully instantiated or not a template
		return true;
	}
	
	// Check if already at or past target phase
	ClassInstantiationPhase current_phase = registry.getCurrentPhase(instantiated_name);
	if (static_cast<uint8_t>(current_phase) >= static_cast<uint8_t>(target_phase)) {
		return true;  // Already done
	}
	
	const LazyClassInstantiationInfo* lazy_info = registry.getLazyClassInfo(instantiated_name);
	if (!lazy_info) {
		FLASH_LOG(Templates, Error, "Failed to get lazy class info for: ", instantiated_name);
		return false;
	}
	
	FLASH_LOG(Templates, Debug, "Instantiating lazy class '", instantiated_name, 
	          "' from phase ", static_cast<int>(current_phase),
	          " to phase ", static_cast<int>(target_phase));
	
	// Phase A -> B transition: Compute size and alignment
	if (current_phase < ClassInstantiationPhase::Layout && 
	    target_phase >= ClassInstantiationPhase::Layout) {
		
		// Look up the type info
		auto type_it = gTypesByName.find(instantiated_name);
		if (type_it == gTypesByName.end()) {
			FLASH_LOG(Templates, Error, "Type not found in gTypesByName: ", instantiated_name);
			return false;
		}
		
		// Get the StructTypeInfo and ensure layout is computed
		// Note: Layout computation happens during try_instantiate_class_template 
		// when the struct_info is created, so this phase is mostly about
		// ensuring members have been processed for size computation
		const TypeInfo* type_info = type_it->second;
		if (type_info->isStruct()) {
			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (struct_info) {
				// Layout is already computed during minimal instantiation
				// Just verify it's valid
				if (struct_info->total_size == 0 && !struct_info->members.empty()) {
					FLASH_LOG(Templates, Warning, "Struct has members but zero size: ", instantiated_name);
				}
			}
		}
		
		registry.updatePhase(instantiated_name, ClassInstantiationPhase::Layout);
		current_phase = ClassInstantiationPhase::Layout;
		
		FLASH_LOG(Templates, Debug, "Completed Layout phase for: ", instantiated_name);
	}
	
	// Phase B -> C transition: Instantiate all members and base classes
	if (current_phase < ClassInstantiationPhase::Full && 
	    target_phase >= ClassInstantiationPhase::Full) {
		
		// Force instantiate all static members
		auto type_it = gTypesByName.find(instantiated_name);
		if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
			const StructTypeInfo* struct_info = type_it->second->getStructInfo();
			if (struct_info) {
				// Trigger lazy instantiation of all static members
				for (const auto& static_member : struct_info->static_members) {
					if (!static_member.initializer.has_value()) {
						// May need lazy instantiation
						instantiateLazyStaticMember(instantiated_name, static_member.name);
					}
				}
			}
		}
		
		// Mark as fully instantiated
		registry.markFullyInstantiated(instantiated_name);
		
		FLASH_LOG(Templates, Debug, "Completed Full phase for: ", instantiated_name);
	}
	
	return true;
}

// Phase 3: Evaluate a lazy type alias on-demand
// Returns the evaluated type and type index, or nullopt if not found/failed
std::optional<std::pair<Type, TypeIndex>> Parser::evaluateLazyTypeAlias(
	StringHandle instantiated_class_name, StringHandle member_name) {
	
	auto& registry = LazyTypeAliasRegistry::getInstance();
	
	// Check for cached result first
	auto cached = registry.getCachedResult(instantiated_class_name, member_name);
	if (cached.has_value()) {
		FLASH_LOG(Templates, Debug, "Using cached type alias result for: ", 
		          instantiated_class_name, "::", member_name);
		return cached;
	}
	
	// Get the lazy alias info (nullptr if not registered)
	const LazyTypeAliasInfo* lazy_info = registry.getLazyTypeAliasInfo(instantiated_class_name, member_name);
	if (!lazy_info) {
		return std::nullopt;  // Not registered for lazy evaluation
	}
	
	FLASH_LOG(Templates, Debug, "Evaluating lazy type alias: ", 
	          instantiated_class_name, "::", member_name);
	
	// Evaluate the type alias by substituting template parameters
	if (!lazy_info->unevaluated_target.is<TypeSpecifierNode>()) {
		FLASH_LOG(Templates, Error, "Lazy type alias target is not a TypeSpecifierNode: ", 
		          instantiated_class_name, "::", member_name);
		return std::nullopt;
	}
	
	const TypeSpecifierNode& target_type = lazy_info->unevaluated_target.as<TypeSpecifierNode>();
	
	// Perform template parameter substitution
	auto [substituted_type, substituted_type_index] = substitute_template_parameter(
		target_type, lazy_info->template_params, lazy_info->template_args);
	
	// Cache the result
	registry.markEvaluated(instantiated_class_name, member_name, substituted_type, substituted_type_index);
	
	FLASH_LOG(Templates, Debug, "Successfully evaluated lazy type alias: ", 
	          instantiated_class_name, "::", member_name, 
	          " -> type=", static_cast<int>(substituted_type), ", index=", substituted_type_index);
	
	return std::make_pair(substituted_type, substituted_type_index);
}

// Phase 4: Instantiate a lazy nested type on-demand
// Returns the type index of the instantiated nested type, or nullopt if not found/failed
std::optional<TypeIndex> Parser::instantiateLazyNestedType(
	StringHandle parent_class_name, StringHandle nested_type_name) {
	
	auto& registry = LazyNestedTypeRegistry::getInstance();
	
	// Get the lazy nested type info (nullptr if not registered or already instantiated)
	const LazyNestedTypeInfo* lazy_info = registry.getLazyNestedTypeInfo(parent_class_name, nested_type_name);
	if (!lazy_info) {
		return std::nullopt;  // Not registered for lazy instantiation (or already instantiated)
	}
	
	FLASH_LOG(Templates, Debug, "Instantiating lazy nested type: ", 
	          parent_class_name, "::", nested_type_name);
	
	// Get the nested type declaration
	if (!lazy_info->nested_type_declaration.is<StructDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Lazy nested type declaration is not a StructDeclarationNode: ", 
		          parent_class_name, "::", nested_type_name);
		return std::nullopt;
	}
	
	const StructDeclarationNode& nested_struct = lazy_info->nested_type_declaration.as<StructDeclarationNode>();
	
	// Create the qualified name for the nested type
	std::string_view qualified_name = StringTable::getStringView(lazy_info->qualified_name);
	
	// Check if type already exists (may have been instantiated through another path)
	auto existing_type_it = gTypesByName.find(lazy_info->qualified_name);
	if (existing_type_it != gTypesByName.end()) {
		TypeIndex existing_index = existing_type_it->second->type_index_;
		registry.markInstantiated(parent_class_name, nested_type_name);
		return existing_index;
	}
	
	// Create a new struct type for the nested class
	TypeInfo& nested_type_info = add_struct_type(lazy_info->qualified_name);
	TypeIndex type_index = nested_type_info.type_index_;
	
	// Create StructTypeInfo for the nested type
	auto nested_struct_info = std::make_unique<StructTypeInfo>(lazy_info->qualified_name, nested_struct.default_access());
	
	// Process members with template parameter substitution
	for (const auto& member_decl : nested_struct.members()) {
		const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
		const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
		
		// Substitute template parameters using parent's template args
		auto [substituted_type, substituted_type_index] = substitute_template_parameter(
			type_spec, lazy_info->parent_template_params, lazy_info->parent_template_args);
		
		// Get size for the member
		size_t member_size = 0;
		if (substituted_type_index < gTypeInfo.size()) {
			const TypeInfo& member_type_info = gTypeInfo[substituted_type_index];
			if (member_type_info.getStructInfo()) {
				member_size = member_type_info.getStructInfo()->total_size;
			} else {
				member_size = get_type_size_bits(substituted_type) / 8;
			}
		} else {
			member_size = get_type_size_bits(substituted_type) / 8;
		}
		
		// Get alignment for the member
		size_t member_alignment = member_size > 0 ? member_size : 1;
		if (substituted_type_index < gTypeInfo.size()) {
			const TypeInfo& member_type_info = gTypeInfo[substituted_type_index];
			if (member_type_info.getStructInfo()) {
				member_alignment = member_type_info.getStructInfo()->alignment;
			}
		}
		
		// Get the name from the identifier token
		StringHandle member_name_handle = decl.identifier_token().handle();
		
		// Check if the type is a reference type
		bool is_reference = type_spec.is_reference() || type_spec.is_lvalue_reference();
		bool is_rvalue_reference = type_spec.is_reference() && !type_spec.is_lvalue_reference();
		size_t referenced_size_bits = member_size * 8;
		
		// Add member to nested struct info
		nested_struct_info->addMember(
			member_name_handle,
			substituted_type,
			substituted_type_index,
			member_size,
			member_alignment,
			member_decl.access,
			std::nullopt,  // No default initializer for now
			is_reference,
			is_rvalue_reference,
			referenced_size_bits,
			false,  // is_array
			{}      // array_dimensions
		);
	}
	
	// Finalize layout
	nested_struct_info->finalize();
	
	// Set the struct info on the type
	nested_type_info.struct_info_ = std::move(nested_struct_info);
	
	// Mark as instantiated (removes from lazy registry)
	registry.markInstantiated(parent_class_name, nested_type_name);
	
	FLASH_LOG(Templates, Debug, "Successfully instantiated lazy nested type: ", 
	          qualified_name, " (type_index=", type_index, ")");
	
	return type_index;
}

// Try to parse an out-of-line template member function definition
// Pattern: template<typename T> ReturnType ClassName<T>::functionName(...) { ... }
// Returns true if successfully parsed, false if not an out-of-line definition
std::optional<bool> Parser::try_parse_out_of_line_template_member(
	const std::vector<ASTNode>& template_params,
	const std::vector<StringHandle>& template_param_names) {

	// Save position in case this isn't an out-of-line definition
	SaveHandle saved_pos = save_token_position();

	// Check for out-of-line constructor/destructor pattern first:
	// ClassName<Args>::ClassName(...)  (constructor)
	// ClassName<Args>::~ClassName()    (destructor)
	// parse_type_specifier would consume the full qualified name as a type, so detect this early
	if (peek().is_identifier()) {
		SaveHandle ctor_check = save_token_position();
		Token potential_class = peek_info();
		advance(); // consume class name
		if (peek() == "<"_tok) {
			skip_template_arguments();
			if (peek() == "::"_tok) {
				advance(); // consume '::'
				bool is_dtor = false;
				if (peek_info().value() == "~") {
					advance(); // consume '~'
					is_dtor = true;
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
							while (!peek().is_eof() && peek() != "{"_tok) {
								if (peek() == "("_tok) {
									skip_balanced_parens();
								} else if (peek() == "{"_tok) {
									break;
								} else {
									advance();
								}
							}
						}

						// Save body position and skip body
						SaveHandle ctor_body_start = save_token_position();
						if (peek() == "{"_tok) {
							skip_balanced_braces();
						} else if (peek() == ";"_tok) {
							advance();
						}

						// Register as out-of-line member function
						OutOfLineMemberFunction out_of_line_ctor;
						out_of_line_ctor.template_params = template_params;
						out_of_line_ctor.function_node = ctor_func_node;
						out_of_line_ctor.body_start = ctor_body_start;
						out_of_line_ctor.template_param_names = template_param_names;

						gTemplateRegistry.registerOutOfLineMember(ctor_class_name, std::move(out_of_line_ctor));

						FLASH_LOG(Templates, Debug, "Registered out-of-line template ",
						          (is_dtor ? "destructor" : "constructor"), ": ",
						          ctor_class_name);
						discard_saved_token(saved_pos);
						return true;
					}
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
	} else {
		restore_token_position(saved_pos);
		return std::nullopt;
	}

	// Check for template arguments after class name: ClassName<T>, etc.
	// This is optional - only present for template classes
	if (peek() == "<"_tok) {
		// Parse template arguments (these should match the template parameters)
		// For now, we'll just skip over them - we know they're template parameters
		advance();  // consume '<'

		// Skip template arguments until we find '>'
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
		// Skip initializer list entries: name(args), name{args}, name(args)...
		while (!peek().is_eof() && peek() != "{"_tok) {
			if (peek() == "("_tok) {
				skip_balanced_parens();
			} else if (peek() == "{"_tok) {
				break; // function body starts
			} else {
				advance();
			}
		}
	}

	// Save the position of the function body for delayed parsing
	// body_start must be right before '{' - trailing specifiers and initializer lists
	// are already consumed above
	SaveHandle body_start = save_token_position();

	// Skip the function body for now (we'll re-parse it during instantiation or first use)
	if (peek() == "{"_tok) {
		skip_balanced_braces();
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

		gTemplateRegistry.registerOutOfLineMember(class_name, std::move(out_of_line_member));
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
				struct_node_ptr = &const_cast<StructDeclarationNode&>(struct_symbol_opt->as<StructDeclarationNode>());
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
ASTNode Parser::substituteTemplateParameters(
	const ASTNode& node,
	const std::vector<ASTNode>& template_params,
	const std::vector<TemplateArgument>& template_args
) {
	// Helper function to get type name as string
	auto get_type_name = [](Type type) -> std::string_view {
		switch (type) {
			case Type::Void: return "void";
			case Type::Bool: return "bool";
			case Type::Char: return "char";
			case Type::UnsignedChar: return "unsigned char";
			case Type::Short: return "short";
			case Type::UnsignedShort: return "unsigned short";
			case Type::Int: return "int";
			case Type::UnsignedInt: return "unsigned int";
			case Type::Long: return "long";
			case Type::UnsignedLong: return "unsigned long";
			case Type::LongLong: return "long long";
			case Type::UnsignedLongLong: return "unsigned long long";
			case Type::Float: return "float";
			case Type::Double: return "double";
			case Type::LongDouble: return "long double";
			case Type::UserDefined: return "user_defined";  // This should be handled specially
			default: return "unknown";
		}
	};

	// Handle different node types
	if (node.is<ExpressionNode>()) {
		const ExpressionNode& expr_node = node.as<ExpressionNode>();
		const auto& expr = expr_node;

		// Check if this is a TemplateParameterReferenceNode
		if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
			const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
			std::string_view param_name = tparam_ref.param_name().view();

			// Find which template parameter this is
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.name() == param_name) {
					const TemplateArgument& arg = template_args[i];

					if (arg.kind == TemplateArgument::Kind::Type) {
						// Create an identifier node for the concrete type
						Token type_token(Token::Type::Identifier, get_type_name(arg.type_value),
						                tparam_ref.token().line(), tparam_ref.token().column(),
						                tparam_ref.token().file_index());
						return emplace_node<ExpressionNode>(IdentifierNode(type_token));
					} else if (arg.kind == TemplateArgument::Kind::Value) {
						// Create a numeric literal node for the value with the correct type
						Type value_type = arg.value_type;
						int size_bits = get_type_size_bits(value_type);
						Token value_token(Token::Type::Literal, StringBuilder().append(arg.int_value).commit(),
						                 tparam_ref.token().line(), tparam_ref.token().column(),
						                 tparam_ref.token().file_index());
						return emplace_node<ExpressionNode>(NumericLiteralNode(value_token, static_cast<unsigned long long>(arg.int_value), value_type, TypeQualifier::None, size_bits));
					}
					// For template template parameters, not yet supported
					break;
				}
			}

			// If we couldn't substitute, return the original node
			return node;
		}
		
		// Check if this is an IdentifierNode that matches a template parameter name
		// (This handles the case where template parameters are stored as IdentifierNode in the AST)
		if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
			std::string_view id_name = id_node.name();
			
			// Check if this identifier matches a template parameter name
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.name() == id_name) {
					const TemplateArgument& arg = template_args[i];
					
					if (arg.kind == TemplateArgument::Kind::Type) {
						// Create an identifier node for the concrete type
						Token type_token(Token::Type::Identifier, get_type_name(arg.type_value), 0, 0, 0);
						return emplace_node<ExpressionNode>(IdentifierNode(type_token));
					} else if (arg.kind == TemplateArgument::Kind::Value) {
						// Create a numeric literal node for the value with the correct type
						Type value_type = arg.value_type;
						int size_bits = get_type_size_bits(value_type);
						Token value_token(Token::Type::Literal, StringBuilder().append(arg.int_value).commit(), 0, 0, 0);
						return emplace_node<ExpressionNode>(NumericLiteralNode(value_token, static_cast<unsigned long long>(arg.int_value), value_type, TypeQualifier::None, size_bits));
					}
					break;
				}
			}
		}

		// For other expression types, recursively substitute in subexpressions
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const BinaryOperatorNode& bin_op = std::get<BinaryOperatorNode>(expr);
			ASTNode substituted_left = substituteTemplateParameters(bin_op.get_lhs(), template_params, template_args);
			ASTNode substituted_right = substituteTemplateParameters(bin_op.get_rhs(), template_params, template_args);
			return emplace_node<ExpressionNode>(BinaryOperatorNode(bin_op.get_token(), substituted_left, substituted_right));
		} else if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const UnaryOperatorNode& unary_op = std::get<UnaryOperatorNode>(expr);
			ASTNode substituted_operand = substituteTemplateParameters(unary_op.get_operand(), template_params, template_args);
			return emplace_node<ExpressionNode>(UnaryOperatorNode(unary_op.get_token(), substituted_operand, unary_op.is_prefix()));
		} else if (std::holds_alternative<FunctionCallNode>(expr)) {
			const FunctionCallNode& func_call = std::get<FunctionCallNode>(expr);
			ChunkedVector<ASTNode> substituted_args;
			for (size_t i = 0; i < func_call.arguments().size(); ++i) {
				substituted_args.push_back(substituteTemplateParameters(func_call.arguments()[i], template_params, template_args));
			}
			
			// Check if function name contains a dependent template hash (Base$hash::member)
			// that needs to be resolved with concrete template arguments
			std::string_view func_name = func_call.called_from().value();
			if (func_name.empty()) func_name = func_call.function_declaration().identifier_token().value();
			size_t dollar_pos = func_name.empty() ? std::string_view::npos : func_name.find('$');
			size_t scope_pos = func_name.empty() ? std::string_view::npos : func_name.find("::");
			if (dollar_pos != std::string_view::npos && scope_pos != std::string_view::npos && dollar_pos < scope_pos) {
				std::string_view base_template_name = func_name.substr(0, dollar_pos);
				std::string_view member_name = func_name.substr(scope_pos + 2);
				
				// Build concrete template arguments from the substitution context
				std::vector<TemplateTypeArg> inst_args;
				for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
					const TemplateArgument& arg = template_args[i];
					if (arg.kind == TemplateArgument::Kind::Type) {
						TemplateTypeArg type_arg;
						type_arg.base_type = arg.type_value;
						type_arg.type_index = 0;
						type_arg.is_value = false;
						inst_args.push_back(type_arg);
					} else if (arg.kind == TemplateArgument::Kind::Value) {
						TemplateTypeArg val_arg;
						val_arg.is_value = true;
						val_arg.value = arg.int_value;
						val_arg.base_type = arg.value_type;
						inst_args.push_back(val_arg);
					}
				}
				
				if (!inst_args.empty()) {
					try_instantiate_class_template(base_template_name, inst_args, true);
					std::string_view correct_inst_name = get_instantiated_class_name(base_template_name, inst_args);
					
					if (correct_inst_name != func_name.substr(0, scope_pos)) {
						// Build corrected function name
						StringBuilder new_name_builder;
						new_name_builder.append(correct_inst_name).append("::").append(member_name);
						std::string_view new_func_name = new_name_builder.commit();
						
						FLASH_LOG(Templates, Debug, "Resolved dependent qualified call: ", func_name, " -> ", new_func_name);
						
						// Trigger lazy member function instantiation
						StringHandle inst_handle = StringTable::getOrInternStringHandle(correct_inst_name);
						StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
						if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(inst_handle, member_handle)) {
							auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(inst_handle, member_handle);
							if (lazy_info_opt.has_value()) {
								instantiateLazyMemberFunction(*lazy_info_opt);
								LazyMemberInstantiationRegistry::getInstance().markInstantiated(inst_handle, member_handle);
							}
						}
						
						// Create new forward declaration with corrected name.
						// The placeholder return type (Int/32) is safe because the codegen
						// resolves the actual return type from the matched FunctionDeclarationNode,
						// not from this forward declaration's type node.
						Token new_token(Token::Type::Identifier, new_func_name,
							func_call.called_from().line(), func_call.called_from().column(), func_call.called_from().file_index());
						auto type_node_ast = emplace_node<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
						auto fwd_decl = emplace_node<DeclarationNode>(type_node_ast, new_token);
						ASTNode new_func_call_node = emplace_node<ExpressionNode>(
							FunctionCallNode(fwd_decl.as<DeclarationNode>(), std::move(substituted_args), new_token));
						return new_func_call_node;
					}
				}
			}
			
			ASTNode new_func_call = emplace_node<ExpressionNode>(FunctionCallNode(const_cast<DeclarationNode&>(func_call.function_declaration()), std::move(substituted_args), func_call.called_from()));
			// Copy mangled name if present (important for template instantiation)
			if (func_call.has_mangled_name()) {
				std::get<FunctionCallNode>(new_func_call.as<ExpressionNode>()).set_mangled_name(func_call.mangled_name());
			}
			return new_func_call;
		} else if (std::holds_alternative<MemberAccessNode>(expr)) {
			const MemberAccessNode& member_access = std::get<MemberAccessNode>(expr);
			ASTNode substituted_object = substituteTemplateParameters(member_access.object(), template_params, template_args);
			return emplace_node<ExpressionNode>(MemberAccessNode(substituted_object, member_access.member_token()));
		} else if (std::holds_alternative<ConstructorCallNode>(expr)) {
			const ConstructorCallNode& constructor_call = std::get<ConstructorCallNode>(expr);
			ASTNode substituted_type = substituteTemplateParameters(constructor_call.type_node(), template_params, template_args);
			ChunkedVector<ASTNode> substituted_args;
			for (size_t i = 0; i < constructor_call.arguments().size(); ++i) {
				substituted_args.push_back(substituteTemplateParameters(constructor_call.arguments()[i], template_params, template_args));
			}
			return emplace_node<ExpressionNode>(ConstructorCallNode(substituted_type, std::move(substituted_args), constructor_call.called_from()));
		} else if (std::holds_alternative<ArraySubscriptNode>(expr)) {
			const ArraySubscriptNode& array_sub = std::get<ArraySubscriptNode>(expr);
			ASTNode substituted_array = substituteTemplateParameters(array_sub.array_expr(), template_params, template_args);
			ASTNode substituted_index = substituteTemplateParameters(array_sub.index_expr(), template_params, template_args);
			return emplace_node<ExpressionNode>(ArraySubscriptNode(substituted_array, substituted_index, array_sub.bracket_token()));
		} else if (std::holds_alternative<FoldExpressionNode>(expr)) {
			// C++17 Fold expressions - expand into nested binary operations
			const FoldExpressionNode& fold = std::get<FoldExpressionNode>(expr);
		
			// The fold pack_name refers to a function parameter pack (like "args")
			// We need to expand it into individual parameter references (like "args_0", "args_1", "args_2")
			std::vector<ASTNode> pack_values;
		
			// Count pack elements using the helper function
			size_t num_pack_elements = count_pack_elements(fold.pack_name());
			
			FLASH_LOG(Templates, Debug, "Fold expansion: pack_name='", fold.pack_name(), "' num_pack_elements=", num_pack_elements);
		
			if (num_pack_elements == 0) {
				FLASH_LOG(Templates, Warning, "Fold expression pack '", fold.pack_name(), "' has no elements");
				return node;
			}
		
			// Create identifier nodes for each pack element: pack_name_0, pack_name_1, etc.
			for (size_t i = 0; i < num_pack_elements; ++i) {
				StringBuilder param_name_builder;
				param_name_builder.append(fold.pack_name());
				param_name_builder.append('_');
				param_name_builder.append(i);
				std::string_view param_name = param_name_builder.commit();
		
				Token param_token(Token::Type::Identifier, param_name,
								 fold.get_token().line(), fold.get_token().column(),
								 fold.get_token().file_index());
				pack_values.push_back(emplace_node<ExpressionNode>(IdentifierNode(param_token)));
			}
		
			if (pack_values.empty()) {
				FLASH_LOG(Templates, Warning, "Fold expression pack '", fold.pack_name(), "' is empty");
				return node;
			}
		
			// Expand the fold expression based on type and direction
			ASTNode result_expr;
			Token op_token = fold.get_token();
			
			if (fold.type() == FoldExpressionNode::Type::Unary) {
				// Unary fold: (... op pack) or (pack op ...)
				if (fold.direction() == FoldExpressionNode::Direction::Left) {
					// Left fold: (... op pack) = ((pack[0] op pack[1]) op pack[2]) ...
					result_expr = pack_values[0];
					for (size_t i = 1; i < pack_values.size(); ++i) {
						result_expr = emplace_node<ExpressionNode>(
							BinaryOperatorNode(op_token, result_expr, pack_values[i]));
					}
				} else {
					// Right fold: (pack op ...) = pack[0] op (pack[1] op (pack[2] op ...))
					result_expr = pack_values[pack_values.size() - 1];
					for (int i = static_cast<int>(pack_values.size()) - 2; i >= 0; --i) {
						result_expr = emplace_node<ExpressionNode>(
							BinaryOperatorNode(op_token, pack_values[i], result_expr));
					}
				}
			} else {
				// Binary fold with init expression
				ASTNode init = substituteTemplateParameters(*fold.init_expr(), template_params, template_args);
				
				if (fold.direction() == FoldExpressionNode::Direction::Left) {
					// Left binary fold: (init op ... op pack) = (((init op pack[0]) op pack[1]) op ...)
					result_expr = init;
					for (size_t i = 0; i < pack_values.size(); ++i) {
						result_expr = emplace_node<ExpressionNode>(
							BinaryOperatorNode(op_token, result_expr, pack_values[i]));
					}
				} else {
					// Right binary fold: (pack op ... op init) = pack[0] op (pack[1] op (... op init))
					result_expr = init;
					for (int i = static_cast<int>(pack_values.size()) - 1; i >= 0; --i) {
						result_expr = emplace_node<ExpressionNode>(
							BinaryOperatorNode(op_token, pack_values[i], result_expr));
					}
				}
			}
			
			return result_expr;
		} else if (std::holds_alternative<SizeofPackNode>(expr)) {
			// sizeof... operator - replace with the pack size as a constant
			const SizeofPackNode& sizeof_pack = std::get<SizeofPackNode>(expr);
			std::string_view pack_name = sizeof_pack.pack_name();
			
			// Count pack elements using the helper function (works when symbol table scope is active)
			size_t num_pack_elements = count_pack_elements(pack_name);
			
			// Fallback: if count_pack_elements returns 0 (scope may have been exited),
			// try to calculate from template_params/template_args by finding the variadic parameter
			bool found_variadic = false;
			if (num_pack_elements == 0 && !template_args.empty()) {
				// The pack_name is the function parameter name (e.g., "rest")
				// We need to find the corresponding variadic template parameter (e.g., "Rest")
				// The mapping: function param type uses the template param name
				size_t non_variadic_count = 0;
				for (size_t i = 0; i < template_params.size(); ++i) {
					if (template_params[i].is<TemplateParameterNode>()) {
						const auto& tparam = template_params[i].as<TemplateParameterNode>();
						if (tparam.is_variadic()) {
							found_variadic = true;
						} else {
							non_variadic_count++;
						}
					}
				}
				if (found_variadic && template_args.size() >= non_variadic_count) {
					num_pack_elements = template_args.size() - non_variadic_count;
				}
			} else if (num_pack_elements > 0) {
				found_variadic = true; // count_pack_elements found it
			}
			
			// If no variadic parameter was found, check pack_param_info_ as well
			if (!found_variadic) {
				auto pack_size = get_pack_size(pack_name);
				if (pack_size.has_value()) {
					found_variadic = true;
					num_pack_elements = *pack_size;
				}
			}
			
			// Error if pack name doesn't refer to a known parameter pack
			if (!found_variadic) {
				FLASH_LOG(Parser, Error, "'" , pack_name, "' does not refer to the name of a parameter pack");
				throw std::runtime_error("'" + std::string(pack_name) + "' does not refer to the name of a parameter pack");
			}
			
			// Create an integer literal with the pack size
			StringBuilder pack_size_builder;
			std::string_view pack_size_str = pack_size_builder.append(num_pack_elements).commit();
			Token literal_token(Token::Type::Literal, pack_size_str, 
			                   sizeof_pack.sizeof_token().line(), sizeof_pack.sizeof_token().column(), 
			                   sizeof_pack.sizeof_token().file_index());
			return emplace_node<ExpressionNode>(
				NumericLiteralNode(literal_token, static_cast<unsigned long long>(num_pack_elements), 
				                  Type::Int, TypeQualifier::None, 32));
		} else if (std::holds_alternative<SizeofExprNode>(expr)) {
			// sizeof operator - substitute template parameters in the operand and try to evaluate
			const SizeofExprNode& sizeof_expr = std::get<SizeofExprNode>(expr);
			
			if (sizeof_expr.is_type()) {
				// sizeof(type) - substitute the type
				ASTNode type_or_expr = sizeof_expr.type_or_expr();
				
				// Check if the type is a TypeSpecifierNode
				if (type_or_expr.is<TypeSpecifierNode>()) {
					const TypeSpecifierNode& type_spec = type_or_expr.as<TypeSpecifierNode>();
					
					// Check if this is a user-defined type that matches a template parameter
					if (type_spec.type() == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
						std::string_view type_name = StringTable::getStringView(type_info.name());
						
						// Check if this type name matches a template parameter
						for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
							const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
							if (tparam.name() == type_name) {
								const TemplateArgument& arg = template_args[i];
								
								if (arg.kind == TemplateArgument::Kind::Type) {
									// Get the size of the concrete type in bytes
									size_t type_size = get_type_size_bits(arg.type_value) / 8;
									
									// Create an integer literal with the type size
									StringBuilder size_builder;
									std::string_view size_str = size_builder.append(type_size).commit();
									Token literal_token(Token::Type::Literal, size_str, 
									                   sizeof_expr.sizeof_token().line(), sizeof_expr.sizeof_token().column(), 
									                   sizeof_expr.sizeof_token().file_index());
									return emplace_node<ExpressionNode>(
										NumericLiteralNode(literal_token, static_cast<unsigned long long>(type_size), 
										                  Type::UnsignedLongLong, TypeQualifier::None, 64));
								}
								break;
							}
						}
					}
					
					// Otherwise, recursively substitute the type node
					ASTNode substituted_type = substituteTemplateParameters(type_or_expr, template_params, template_args);
					return emplace_node<ExpressionNode>(SizeofExprNode(substituted_type, sizeof_expr.sizeof_token()));
				}
			} else {
				// sizeof(expression) - substitute the expression
				ASTNode substituted_expr = substituteTemplateParameters(sizeof_expr.type_or_expr(), template_params, template_args);
				return emplace_node<ExpressionNode>(SizeofExprNode::from_expression(substituted_expr, sizeof_expr.sizeof_token()));
			}
			
			// Return the original node if no substitution was possible
			return node;
		}

		// For other expression types that don't contain subexpressions, return as-is
		return node;

	} else if (node.is<FunctionCallNode>()) {
		// Handle function calls that might contain template parameter references
		const FunctionCallNode& func_call = node.as<FunctionCallNode>();

		// Substitute arguments
		ChunkedVector<ASTNode> substituted_args;
		for (size_t i = 0; i < func_call.arguments().size(); ++i) {
			substituted_args.push_back(substituteTemplateParameters(func_call.arguments()[i], template_params, template_args));
		}

		// For now, don't substitute the function declaration itself
		// Create new function call with substituted arguments
		ASTNode new_func_call = emplace_node<FunctionCallNode>(const_cast<DeclarationNode&>(func_call.function_declaration()), std::move(substituted_args), func_call.called_from());
		// Copy mangled name if present (important for template instantiation)
		if (func_call.has_mangled_name()) {
			new_func_call.as<FunctionCallNode>().set_mangled_name(func_call.mangled_name());
		}
		return new_func_call;

	} else if (node.is<BinaryOperatorNode>()) {
		// Handle binary operators
		const BinaryOperatorNode& bin_op = node.as<BinaryOperatorNode>();

		ASTNode substituted_left = substituteTemplateParameters(bin_op.get_lhs(), template_params, template_args);
		ASTNode substituted_right = substituteTemplateParameters(bin_op.get_rhs(), template_params, template_args);

		return emplace_node<BinaryOperatorNode>(bin_op.get_token(), substituted_left, substituted_right);

	} else if (node.is<DeclarationNode>()) {
		// Handle declarations that might have template parameter types
		const DeclarationNode& decl = node.as<DeclarationNode>();

		// Substitute the type specifier
		ASTNode substituted_type = substituteTemplateParameters(decl.type_node(), template_params, template_args);

		// Create new declaration with substituted type
		return emplace_node<DeclarationNode>(substituted_type, decl.identifier_token());

	} else if (node.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& type_spec = node.as<TypeSpecifierNode>();

		// Check if this is a user-defined type that matches a template parameter
		if (type_spec.type() == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
			std::string_view type_name = StringTable::getStringView(type_info.name());

			// Check if this type name matches a template parameter
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.name() == type_name && template_args[i].kind == TemplateArgument::Kind::Type) {
					// Substitute with concrete type
					return emplace_node<TypeSpecifierNode>(
						template_args[i].type_value,
						TypeQualifier::None,
						get_type_size_bits(template_args[i].type_value),
						Token()
					);
				}
			}
		}

		return node;

	} else if (node.is<BlockNode>()) {
		// Handle block nodes by substituting in all statements
		const BlockNode& block = node.as<BlockNode>();
		
		auto new_block = emplace_node<BlockNode>();
		BlockNode& new_block_ref = new_block.as<BlockNode>();
		
		for (size_t i = 0; i < block.get_statements().size(); ++i) {
			new_block_ref.add_statement_node(substituteTemplateParameters(block.get_statements()[i], template_params, template_args));
		}
		
		return new_block;

	} else if (node.is<ForStatementNode>()) {
		// Handle for statements
		const ForStatementNode& for_stmt = node.as<ForStatementNode>();
		
		auto init_stmt = for_stmt.get_init_statement().has_value() ? 
			std::optional<ASTNode>(substituteTemplateParameters(*for_stmt.get_init_statement(), template_params, template_args)) : 
			std::nullopt;
		auto condition = for_stmt.get_condition().has_value() ? 
			std::optional<ASTNode>(substituteTemplateParameters(*for_stmt.get_condition(), template_params, template_args)) : 
			std::nullopt;
		auto update_expr = for_stmt.get_update_expression().has_value() ? 
			std::optional<ASTNode>(substituteTemplateParameters(*for_stmt.get_update_expression(), template_params, template_args)) : 
			std::nullopt;
		auto body_stmt = substituteTemplateParameters(for_stmt.get_body_statement(), template_params, template_args);
		
		return emplace_node<ForStatementNode>(init_stmt, condition, update_expr, body_stmt);

	} else if (node.is<UnaryOperatorNode>()) {
		// Handle unary operators
		const UnaryOperatorNode& unary_op = node.as<UnaryOperatorNode>();
		
		ASTNode substituted_operand = substituteTemplateParameters(unary_op.get_operand(), template_params, template_args);
		
		return emplace_node<UnaryOperatorNode>(unary_op.get_token(), substituted_operand, unary_op.is_prefix());

	} else if (node.is<VariableDeclarationNode>()) {
		// Handle variable declarations
		const VariableDeclarationNode& var_decl = node.as<VariableDeclarationNode>();
		
		auto initializer = var_decl.initializer().has_value() ?
			std::optional<ASTNode>(substituteTemplateParameters(*var_decl.initializer(), template_params, template_args)) :
			std::nullopt;
		
		return emplace_node<VariableDeclarationNode>(var_decl.declaration_node(), initializer, var_decl.storage_class());

	} else if (node.is<ReturnStatementNode>()) {
		// Handle return statements
		const ReturnStatementNode& ret_stmt = node.as<ReturnStatementNode>();
		
		auto expr = ret_stmt.expression().has_value() ?
			std::optional<ASTNode>(substituteTemplateParameters(*ret_stmt.expression(), template_params, template_args)) :
			std::nullopt;
		
		return emplace_node<ReturnStatementNode>(expr, ret_stmt.return_token());

	} else if (node.is<IfStatementNode>()) {
		// Handle if statements
		const IfStatementNode& if_stmt = node.as<IfStatementNode>();
		
		ASTNode substituted_condition = substituteTemplateParameters(if_stmt.get_condition(), template_params, template_args);
		
		// For if constexpr, evaluate the condition at compile time and eliminate the dead branch
		if (if_stmt.is_constexpr()) {
			ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
			auto eval_result = ConstExpr::Evaluator::evaluate(substituted_condition, eval_ctx);
			if (eval_result.success()) {
				bool condition_value = eval_result.as_int() != 0;
				FLASH_LOG(Templates, Debug, "if constexpr condition evaluated to ", condition_value ? "true" : "false");
				if (condition_value) {
					return substituteTemplateParameters(if_stmt.get_then_statement(), template_params, template_args);
				} else if (if_stmt.has_else()) {
					return substituteTemplateParameters(*if_stmt.get_else_statement(), template_params, template_args);
				} else {
					// No else branch and condition is false - return empty block
					return emplace_node<BlockNode>();
				}
			}
		}
		
		ASTNode substituted_then = substituteTemplateParameters(if_stmt.get_then_statement(), template_params, template_args);
		auto substituted_else = if_stmt.get_else_statement().has_value() ?
			std::optional<ASTNode>(substituteTemplateParameters(*if_stmt.get_else_statement(), template_params, template_args)) :
			std::nullopt;
		auto substituted_init = if_stmt.get_init_statement().has_value() ?
			std::optional<ASTNode>(substituteTemplateParameters(*if_stmt.get_init_statement(), template_params, template_args)) :
			std::nullopt;
		
		return emplace_node<IfStatementNode>(substituted_condition, substituted_then, substituted_else, substituted_init, if_stmt.is_constexpr());

	} else if (node.is<WhileStatementNode>()) {
		// Handle while statements
		const WhileStatementNode& while_stmt = node.as<WhileStatementNode>();
		
		ASTNode substituted_condition = substituteTemplateParameters(while_stmt.get_condition(), template_params, template_args);
		ASTNode substituted_body = substituteTemplateParameters(while_stmt.get_body_statement(), template_params, template_args);
		
		return emplace_node<WhileStatementNode>(substituted_condition, substituted_body);
	}

	// For other node types, return as-is (simplified implementation)
	return node;
}

// Extract base template name from a mangled template instantiation name
// Supports underscore-based naming: "enable_if_void_int" -> "enable_if"
// Future: Will support hash-based naming: "enable_if$abc123" -> "enable_if"
// 
// Tries progressively longer prefixes by searching for '_' separators
// until a registered template or alias template is found.
//
// Returns: base template name if found, empty string_view otherwise
std::string_view Parser::extract_base_template_name(std::string_view mangled_name) {
	// Try progressively longer prefixes until we find a registered template
	size_t underscore_pos = 0;
	
	while ((underscore_pos = mangled_name.find('_', underscore_pos)) != std::string_view::npos) {
		std::string_view candidate = mangled_name.substr(0, underscore_pos);
		
		// Check if this is a registered class template
		auto candidate_opt = gTemplateRegistry.lookupTemplate(candidate);
		if (candidate_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "extract_base_template_name: found template '", 
			          candidate, "' in mangled name '", mangled_name, "'");
			return candidate;
		}
		
		// Also check alias templates
		auto alias_candidate = gTemplateRegistry.lookup_alias_template(candidate);
		if (alias_candidate.has_value()) {
			FLASH_LOG(Templates, Debug, "extract_base_template_name: found alias template '", 
			          candidate, "' in mangled name '", mangled_name, "'");
			return candidate;
		}
		
		underscore_pos++; // Move past this underscore
	}
	
	return {};  // Not found
}

// Extract base template name by stripping suffixes from right to left
// Used when we have an instantiated name like "Container_int_float"
// and need to find "Container"
//
// Returns: base template name if found, empty string_view otherwise
std::string_view Parser::extract_base_template_name_by_stripping(std::string_view instantiated_name) {
	std::string_view base_template_name = instantiated_name;
	
	// Try progressively stripping '_suffix' patterns until we find a registered template
	while (!base_template_name.empty()) {
		// Check if current name is a registered template
		auto template_opt = gTemplateRegistry.lookupTemplate(base_template_name);
		if (template_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "extract_base_template_name_by_stripping: found template '", 
			          base_template_name, "' by stripping from '", instantiated_name, "'");
			return base_template_name;
		}
		
		// Also check alias templates
		auto alias_opt = gTemplateRegistry.lookup_alias_template(base_template_name);
		if (alias_opt.has_value()) {
			FLASH_LOG(Templates, Debug, "extract_base_template_name_by_stripping: found alias template '", 
			          base_template_name, "' by stripping from '", instantiated_name, "'");
			return base_template_name;
		}
		
		// Strip last suffix
		size_t underscore_pos = base_template_name.find_last_of('_');
		if (underscore_pos == std::string_view::npos) {
			break;  // No more underscores to strip
		}
		
		base_template_name = base_template_name.substr(0, underscore_pos);
	}
	
	return {};  // Not found
}
// Helper: resolve a type name within the current namespace context (including using directives)
static const TypeInfo* lookupTypeInCurrentContext(StringHandle type_handle) {
	// Direct lookup (unqualified)
	auto it = gTypesByName.find(type_handle);
	if (it != gTypesByName.end()) {
		return it->second;
	}

	// Walk current namespace chain outward (e.g., std::foo, ::foo)
	NamespaceHandle ns_handle = gSymbolTable.get_current_namespace_handle();
	while (ns_handle.isValid()) {
		StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(ns_handle, type_handle);
		auto q_it = gTypesByName.find(qualified);
		if (q_it != gTypesByName.end()) {
			return q_it->second;
		}
		if (ns_handle.isGlobal()) {
			break;
		}
		ns_handle = gNamespaceRegistry.getParent(ns_handle);
	}

	// using directives
	for (NamespaceHandle using_ns : gSymbolTable.get_current_using_directive_handles()) {
		if (!using_ns.isValid()) continue;
		StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(using_ns, type_handle);
		auto u_it = gTypesByName.find(qualified);
		if (u_it != gTypesByName.end()) {
			return u_it->second;
		}
	}

	// Fallback: unique suffix match (e.g., std::size_t when current namespace context is unavailable)
	std::string_view type_name_sv = StringTable::getStringView(type_handle);
	const TypeInfo* suffix_match = nullptr;
	for (const auto& [handle, info] : gTypesByName) {
		std::string_view full_name = StringTable::getStringView(handle);
		if (full_name.size() <= type_name_sv.size() + 2) continue;
		if (!full_name.ends_with(type_name_sv)) continue;
		size_t prefix_pos = full_name.size() - type_name_sv.size();
		if (prefix_pos < 2 || full_name[prefix_pos - 2] != ':' || full_name[prefix_pos - 1] != ':') continue;
		if (suffix_match && suffix_match != info) {
			// Ambiguous - multiple matches
			suffix_match = nullptr;
			break;
		}
		suffix_match = info;
	}
	if (suffix_match) {
		return suffix_match;
	}

	return nullptr;
}
