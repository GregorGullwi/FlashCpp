std::optional<ASTNode> Parser::try_instantiate_template_explicit(std::string_view template_name, const std::vector<TemplateTypeArg>& explicit_types, size_t call_arg_count) {
	// FIRST: Check if we have an explicit specialization for these template arguments
	// This handles cases like: template<> int sum<int, int>(int, int) being called as sum<int, int>(3, 7)
	auto specialization_opt = gTemplateRegistry.lookupSpecialization(template_name, explicit_types);
	if (specialization_opt.has_value()) {
		FLASH_LOG(Templates, Debug, "Found explicit specialization for ", template_name);
		return *specialization_opt;
	}

	// Look up ALL templates with this name (for SFINAE overload resolution)
	const std::vector<ASTNode>* all_templates = gTemplateRegistry.lookupAllTemplates(template_name);
	if (!all_templates || all_templates->empty()) {
		return std::nullopt;  // No template with this name
	}

	// Loop over all overloads for SFINAE support
	for (const auto& template_node : *all_templates) {
	if (!template_node.is<TemplateFunctionDeclarationNode>()) {
		continue;  // Not a function template, try next overload
	}

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

	// Filter by call argument count if known (SIZE_MAX means unknown)
	// Only reject if caller provides MORE args than the function has params
	// (fewer args might use defaults, so we allow call_arg_count <= func_param_count)
	if (call_arg_count != SIZE_MAX && !func_decl.is_variadic()) {
		size_t func_param_count = func_decl.parameter_nodes().size();
		bool has_variadic_func_pack = false;
		for (const auto& p : func_decl.parameter_nodes()) {
			if (p.is<DeclarationNode>() && p.as<DeclarationNode>().is_parameter_pack()) {
				has_variadic_func_pack = true;
				break;
			}
		}
		if (!has_variadic_func_pack && call_arg_count > func_param_count) {
			continue;  // Too many arguments for this overload
		}
	}

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
		continue;  // Wrong number of template arguments for non-variadic template, try next overload
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
			continue;  // Not enough template arguments, try next overload
		}
	}

	// Build template argument list
	std::vector<TemplateArgument> template_args;
	size_t explicit_idx = 0;  // Track position in explicit_types
	bool overload_mismatch = false;
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
				overload_mismatch = true;
				break;
			}
			// Use toTemplateArgument() to preserve full type info including references
			template_args.push_back(toTemplateArgument(explicit_types[explicit_idx]));
			++explicit_idx;
		}
	}
	if (overload_mismatch) continue;  // SFINAE: try next overload

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
			
			// Don't create instantiation - constraint failed, try next overload
			continue;
		}
	}

	// CHECK CONCEPT CONSTRAINTS ON TEMPLATE PARAMETERS (C++20 abbreviated templates)
	// For parameters like `template<IsInt _T0>` (from `IsInt auto x`), evaluate the concept
	{
		size_t arg_idx = 0;
		for (const auto& tparam_node : template_params) {
			if (!tparam_node.is<TemplateParameterNode>()) continue;
			const TemplateParameterNode& param = tparam_node.as<TemplateParameterNode>();
			if (param.has_concept_constraint() && arg_idx < explicit_types.size()) {
				std::string_view concept_name = param.concept_constraint();
				auto concept_opt = gConceptRegistry.lookupConcept(concept_name);
				if (concept_opt.has_value()) {
					const auto& concept_node = concept_opt->as<ConceptDeclarationNode>();
					const auto& concept_params = concept_node.template_params();
					// Strip lvalue reference that deduction adds for lvalue arguments.
					TemplateTypeArg concept_arg = explicit_types[arg_idx];
					concept_arg.ref_qualifier = ReferenceQualifier::None;
					std::vector<TemplateTypeArg> concept_args = { concept_arg };
					std::vector<std::string_view> concept_param_names;
					if (!concept_params.empty()) {
						concept_param_names.push_back(concept_params[0].name());
					}
					auto constraint_result = evaluateConstraint(
						concept_node.constraint_expr(), concept_args, concept_param_names);
					if (!constraint_result.satisfied) {
						FLASH_LOG(Parser, Error, "concept constraint '", concept_name, "' not satisfied for parameter '", param.name(), "' of '", template_name, "'");
						FLASH_LOG(Parser, Error, "  ", constraint_result.error_message);
						overload_mismatch = true;
						break;
					}
				}
			}
			if (!param.is_variadic()) ++arg_idx;
		}
	}
	if (overload_mismatch) continue;  // SFINAE: concept constraint failed, try next overload

	// SFINAE for trailing return type: if the function has a declaration position for re-parsing,
	// always re-parse the return type with substituted template parameters.
	// During template parsing, trailing return types like decltype(u->foo(), void(), true)
	// may resolve to concrete types (e.g., bool) even when they contain dependent expressions.
	// The re-parse with concrete template arguments will fail if substitution is invalid.
	if (func_decl.has_trailing_return_type_position()) {
		bool prev_sfinae_context = in_sfinae_context_;
		bool prev_parsing_template_body = parsing_template_body_;
		auto prev_template_param_names = std::move(current_template_param_names_);
		auto prev_sfinae_type_map = std::move(sfinae_type_map_);
		in_sfinae_context_ = true;
		parsing_template_body_ = false;  // Prevent dependent-type fallback during SFINAE
		current_template_param_names_.clear();  // No dependent names during SFINAE
		sfinae_type_map_.clear();

		SaveHandle sfinae_pos = save_token_position();
		restore_lexer_position_only(func_decl.trailing_return_type_position());
		advance();  // consume '->'

		// Register function parameters so they're visible in decltype expressions
		gSymbolTable.enter_scope(ScopeType::Function);
		register_parameters_in_scope(func_decl.parameter_nodes());

		FlashCpp::TemplateParameterScope sfinae_scope;
		for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
			if (!template_params[i].is<TemplateParameterNode>()) continue;
			const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
			Type concrete_type = template_args[i].type_value;
			auto& type_info = gTypeInfo.emplace_back(
				StringTable::getOrInternStringHandle(tparam.name()),
				concrete_type, gTypeInfo.size(),
				getTypeSizeFromTemplateArgument(template_args[i]));
			gTypesByName.emplace(type_info.name(), &type_info);
			sfinae_scope.addParameter(&type_info);
			// Populate SFINAE type map so expression parser can resolve template params
			sfinae_type_map_[type_info.name()] = template_args[i].type_index;
		}

		auto return_type_result = parse_type_specifier();
		gSymbolTable.exit_scope();
		restore_lexer_position_only(sfinae_pos);
		in_sfinae_context_ = prev_sfinae_context;
		parsing_template_body_ = prev_parsing_template_body;
		current_template_param_names_ = std::move(prev_template_param_names);
		sfinae_type_map_ = std::move(prev_sfinae_type_map);

		if (return_type_result.is_error() || !return_type_result.node().has_value()) {
			FLASH_LOG_FORMAT(Templates, Debug, "SFINAE: trailing return type re-parse failed for '{}', trying next overload", template_name);
			continue;  // SFINAE: this overload's return type failed, try next
		}
	}

	// Instantiate the template (same logic as try_instantiate_template)
	// Generate mangled name first - it now includes reference qualifiers
	std::string_view mangled_name = gTemplateRegistry.mangleTemplateName(template_name, template_args);

	// Check if we already have this instantiation using structured key
	// This ensures that int, int&, and int&& are treated as distinct instantiations
	auto key = FlashCpp::makeInstantiationKey(
		StringTable::getOrInternStringHandle(template_name), template_args);

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
	return_type_ref.set_reference_qualifier(orig_return_type.reference_qualifier());

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
			param_type_ref.set_reference_qualifier(orig_param_type.reference_qualifier());

			auto new_param_decl = emplace_node<DeclarationNode>(param_type, param_decl.identifier_token());
			new_func_ref.add_parameter_node(new_param_decl);
		}
	}

	// Handle the function body
	// Helper: substitute template parameters in a function body node
	auto substituteBodyWithArgs = [&](const ASTNode& body) -> ASTNode {
		std::vector<TemplateArgument> converted_template_args;
		converted_template_args.reserve(template_args.size());
		for (const auto& arg : template_args) {
			if (arg.kind == TemplateArgument::Kind::Type) {
				converted_template_args.push_back(TemplateArgument::makeType(arg.type_value));
			} else if (arg.kind == TemplateArgument::Kind::Value) {
				converted_template_args.push_back(TemplateArgument::makeValue(arg.int_value, arg.value_type));
			} else {
				converted_template_args.push_back(arg);
			}
		}
		return substituteTemplateParameters(body, template_params, converted_template_args);
	};

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
				type_info.reference_qualifier_ = ts.reference_qualifier();
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

		// Cycle detection: if this exact instantiation (same mangled name = same template
		// arguments) is already being parsed on this thread, return early to break the cycle.
		// Using the mangled name instead of the original template declaration pointer ensures
		// distinct recursive instantiations (e.g. var_sum<int,int,int> from var_sum<int,int,int,int>)
		// are not blocked.
		static thread_local std::unordered_set<StringHandle> body_parse_in_progress;
		StringHandle cycle_key = StringTable::getOrInternStringHandle(mangled_name);
		if (body_parse_in_progress.count(cycle_key)) {
			// Already parsing this body — skip body to break the cycle.
			FLASH_LOG(Templates, Debug, "Cycle detected in function template body parsing for '", template_name, "' (mangled: '", mangled_name, "'), skipping body");
			template_param_substitutions_ = std::move(saved_template_param_substitutions);
			current_function_ = saved_current_function;
			gSymbolTable.exit_scope();
			restore_lexer_position_only(current_pos);
			discard_saved_token(current_pos);
			return std::nullopt;
		}
		body_parse_in_progress.insert(cycle_key);
		struct BodyParseGuard {
			std::unordered_set<StringHandle>& set;
			StringHandle key;
			~BodyParseGuard() { set.erase(key); }
		} body_guard{body_parse_in_progress, cycle_key};

		// Set current_template_param_names_ so the expression parser can find
		// non-type template parameters (e.g., N in "x * N") via template_param_substitutions_
		std::vector<StringHandle> saved_template_param_names = std::move(current_template_param_names_);
		current_template_param_names_.clear();
		for (const auto& pn : param_names) {
			current_template_param_names_.push_back(StringTable::getOrInternStringHandle(pn));
		}

		// Parse the function body
		auto block_result = parse_block();
		
		// Restore the template parameter substitutions and param names
		current_template_param_names_ = std::move(saved_template_param_names);
		template_param_substitutions_ = std::move(saved_template_param_substitutions);
		
		if (!block_result.is_error() && block_result.node().has_value()) {
			new_func_ref.set_definition(substituteBodyWithArgs(*block_result.node()));
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
			new_func_ref.set_definition(substituteBodyWithArgs(*orig_body));
		}
	}

	// Copy function specifiers from original template
	new_func_ref.set_is_constexpr(func_decl.is_constexpr());
	new_func_ref.set_is_consteval(func_decl.is_consteval());
	new_func_ref.set_is_constinit(func_decl.is_constinit());
	new_func_ref.set_noexcept(func_decl.is_noexcept());
	new_func_ref.set_is_variadic(func_decl.is_variadic());
	new_func_ref.set_is_deleted(func_decl.is_deleted());
	new_func_ref.set_is_static(func_decl.is_static());
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
	} // end of overload loop

	return std::nullopt;  // No overload matched
}

// Try to instantiate a function template with the given argument types
// Returns the instantiated function declaration node if successful
std::optional<ASTNode> Parser::try_instantiate_template(std::string_view template_name, const std::vector<TypeSpecifierNode>& arg_types) {
	PROFILE_TEMPLATE_INSTANTIATION(std::string(template_name) + "_func");
	
	static int recursion_depth = 0;
	recursion_depth++;
	
	if (recursion_depth > 64) {
		FLASH_LOG(Templates, Error, "try_instantiate_template recursion depth exceeded 64! Possible infinite loop for template '", template_name, "'");
		recursion_depth--;
		return std::nullopt;
	}

	// Look up ALL templates with this name (for SFINAE support with same-name overloads)
	const std::vector<ASTNode>* all_templates = gTemplateRegistry.lookupAllTemplates(template_name);
	
	// If not found, try namespace-qualified lookup.
	// When inside a namespace (e.g., std) and looking up "__detail::__or_fn",
	// we need to also try "std::__detail::__or_fn" since templates are registered
	// with their fully-qualified names.
	// Walk up the namespace hierarchy: e.g., in std::__cxx11, try:
	//   std::__cxx11::__detail::__or_fn, then std::__detail::__or_fn, then ::__detail::__or_fn
	if (!all_templates || all_templates->empty()) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		StringHandle template_handle = StringTable::getOrInternStringHandle(template_name);
		
		while (!current_handle.isGlobal() && (!all_templates || all_templates->empty())) {
			StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_handle, template_handle);
			std::string_view qualified_name_view = StringTable::getStringView(qualified_handle);
			
			FLASH_LOG_FORMAT(Templates, Debug, "[depth={}]: Template '{}' not found, trying qualified name '{}'",
				recursion_depth, template_name, qualified_name_view);
			
			all_templates = gTemplateRegistry.lookupAllTemplates(qualified_name_view);
			
			// Move to parent namespace for next iteration
			current_handle = gNamespaceRegistry.getParent(current_handle);
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
	auto key = FlashCpp::makeInstantiationKey(
		StringTable::getOrInternStringHandle(template_name), template_args);

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		PROFILE_TEMPLATE_CACHE_HIT(std::string(template_name) + "_func");
		return *existing_inst;  // Return existing instantiation
	}
	PROFILE_TEMPLATE_CACHE_MISS(std::string(template_name) + "_func");

	// Step 3: Instantiate the template
	// For Phase 2, we'll create a simplified instantiation
	// We'll just use the original function with a mangled name
	// Full AST cloning and substitution will be implemented later

	// Generate mangled name for the instantiation
	std::string_view mangled_name = gTemplateRegistry.mangleTemplateName(template_name, template_args);

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
				type_arg.ref_qualifier = type_spec.reference_qualifier();
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

	// CHECK CONCEPT CONSTRAINTS ON TEMPLATE PARAMETERS (C++20 abbreviated templates)
	{
		size_t arg_idx = 0;
		for (const auto& tparam_node : template_params) {
			if (!tparam_node.is<TemplateParameterNode>()) continue;
			const TemplateParameterNode& param = tparam_node.as<TemplateParameterNode>();
			if (param.has_concept_constraint() && arg_idx < template_args_as_type_args.size()) {
				std::string_view concept_name = param.concept_constraint();
				auto concept_opt = gConceptRegistry.lookupConcept(concept_name);
				if (concept_opt.has_value()) {
					const auto& concept_node = concept_opt->as<ConceptDeclarationNode>();
					const auto& concept_params = concept_node.template_params();
					// Strip the lvalue reference that deduction adds for lvalue arguments.
					// For abbreviated function templates (ConceptName auto x), the deduced
					// type T is the parameter type without reference qualification.
					TemplateTypeArg concept_arg = template_args_as_type_args[arg_idx];
					concept_arg.ref_qualifier = ReferenceQualifier::None;
					std::vector<TemplateTypeArg> concept_args = { concept_arg };
					std::vector<std::string_view> concept_param_names;
					if (!concept_params.empty()) {
						concept_param_names.push_back(concept_params[0].name());
					}
					auto constraint_result = evaluateConstraint(
						concept_node.constraint_expr(), concept_args, concept_param_names);
					if (!constraint_result.satisfied) {
						FLASH_LOG(Parser, Error, "concept constraint '", concept_name, "' not satisfied for parameter '", param.name(), "' of '", template_name, "'");
						FLASH_LOG(Parser, Error, "  ", constraint_result.error_message);
						return std::nullopt;
					}
				}
			}
			if (!param.is_variadic()) ++arg_idx;
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
				// Re-parse if type is incomplete instantiation (has unresolved template params)
				// OR if type name contains template parameter markers like _T or ::type (typename member access)
				bool has_unresolved = orig_type_info.is_incomplete_instantiation_;
				bool has_template_param = type_name.find("_T") != std::string::npos || 
				                          type_name.find("::type") != std::string::npos;
				should_reparse = has_unresolved || has_template_param;
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

		// Cycle detection for trailing return type re-parsing: when evaluating a
		// function's decltype trailing return type, encountering the same function
		// (by name) again creates infinite recursion (e.g. __niter_base whose return
		// type contains __niter_base itself).  Track by function name — pointer-based
		// tracking is unreliable here because the registry vector may grow between
		// the outer and inner call, subtly shifting addresses.  Returning nullopt
		// causes the caller to try the next overload (the non-recursive base case).
		static thread_local std::unordered_set<std::string_view> trailing_return_in_progress;
		if (trailing_return_in_progress.count(saved_mangled_name)) {
			FLASH_LOG(Templates, Debug, "Cycle detected in trailing return type for '", template_name, "' (mangled: '", saved_mangled_name, "'), returning auto to break cycle");
			return std::nullopt;
		}
		trailing_return_in_progress.insert(saved_mangled_name);
		struct TrailingReturnGuard {
			std::unordered_set<std::string_view>& set;
			std::string_view key;
			~TrailingReturnGuard() { set.erase(key); }
		} trailing_return_guard{trailing_return_in_progress, saved_mangled_name};
		
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
			// instead of staying as UserDefined, which would cause toString() to return "?"
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
				rt.set_reference_qualifier(ReferenceQualifier::RValueReference);  // Set rvalue reference
			} else if (is_punctuator_or_operator && is_amp) {
				advance();  // Consume &
				rt.set_reference_qualifier(ReferenceQualifier::LValueReference);  // Set lvalue reference
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
		// NOTE: is_incomplete_instantiation_ on placeholder types is informational —
		// it indicates the type was created with dependent/unresolved args during
		// template definition. During SFINAE re-parse with concrete args, the placeholder
		// may still be referenced even though it was resolved. SFINAE rejection is
		// handled by parse failures in parse_type_specifier, not by this flag.
		if (return_type.is<TypeSpecifierNode>()) {
			const TypeSpecifierNode& type_spec = return_type.as<TypeSpecifierNode>();
			
			if (type_spec.type() == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
				
				if (type_info.is_incomplete_instantiation_) {
					FLASH_LOG_FORMAT(Templates, Debug, "SFINAE: Return type still has incomplete instantiation placeholder: {}", StringTable::getStringView(type_info.name()));
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
		new_return_type.set_reference_qualifier(orig_return_type.reference_qualifier());
		
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
							param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);
						} else if (arg_type.is_rvalue_reference()) {
							// Deduced type is rvalue reference (e.g., int&&)
							// Applying && gives int&& && which collapses to int&&
							param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);  // rvalue reference
						} else {
							// Deduced type is non-reference (e.g., int from literal)
							// Applying && gives int&&
							param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);  // rvalue reference
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
				ASTNode param_type;
				if (orig_param_type.type() == Type::Auto && arg_type_index < arg_types.size()) {
					// Abbreviated function template parameter (concept auto / auto):
					// use the deduced argument type as the concrete instantiated parameter type.
					//
					// For plain `auto value` called with int: deduced type is int, no pointer levels.
					// For `auto* p` called with int*: orig has 1 pointer level from the declaration,
					// and deduced_arg_type has 1 pointer level from the argument. The deduced type
					// already accounts for the full type (int*), so we use its pointer levels.
					// However, if the original declaration adds EXTRA pointer levels beyond what
					// deduction provides (e.g., `auto** pp` called with int*), we must preserve
					// those additional levels from orig_param_type.
					const TypeSpecifierNode& deduced_arg_type = arg_types[arg_type_index];
					CVQualifier cv = static_cast<CVQualifier>(
						static_cast<uint8_t>(deduced_arg_type.cv_qualifier()) |
						static_cast<uint8_t>(orig_param_type.cv_qualifier()));
					param_type = emplace_node<TypeSpecifierNode>(
						deduced_arg_type.type(),
						TypeQualifier::None,
						deduced_arg_type.size_in_bits(),
						Token(),
						cv
					);
					param_type.as<TypeSpecifierNode>().set_type_index(deduced_arg_type.type_index());
					// Copy pointer levels from the deduced argument type
					for (const auto& ptr_level : deduced_arg_type.pointer_levels()) {
						param_type.as<TypeSpecifierNode>().add_pointer_level(ptr_level.cv_qualifier);
					}
					// If the original declaration has MORE pointer levels than the deduced type
					// (e.g., `auto** pp` where deduced type is int*), append the extra levels.
					// This handles patterns like `concept auto* p` or `auto** pp`.
					if (orig_param_type.pointer_depth() > deduced_arg_type.pointer_depth()) {
						const auto& orig_levels = orig_param_type.pointer_levels();
						for (size_t pl = deduced_arg_type.pointer_depth(); pl < orig_param_type.pointer_depth(); ++pl) {
							param_type.as<TypeSpecifierNode>().add_pointer_level(orig_levels[pl].cv_qualifier);
						}
					}
				} else {
					auto [subst_type, subst_type_index] = substitute_template_parameter(
						orig_param_type, template_params, template_args_as_type_args
					);
					param_type = emplace_node<TypeSpecifierNode>(
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
				}

				// Handle forwarding references using the deduced argument type (if available)
				if (orig_param_type.is_rvalue_reference() && arg_type_index < arg_types.size()) {
					const TypeSpecifierNode& arg_type = arg_types[arg_type_index];
					if (arg_type.is_lvalue_reference()) {
						param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);
					} else if (arg_type.is_rvalue_reference()) {
						param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);  // rvalue reference
					} else if (arg_type.is_reference()) {
						param_type.as<TypeSpecifierNode>().set_reference_qualifier(arg_type.reference_qualifier());
					} else {
						param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);  // T && → T&&
					}
				} else if (orig_param_type.is_lvalue_reference()) {
					param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::LValueReference);
				} else if (orig_param_type.is_rvalue_reference()) {
					param_type.as<TypeSpecifierNode>().set_reference_qualifier(ReferenceQualifier::RValueReference);
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

		// Cycle detection: if this exact instantiation (same mangled name = same parameter
		// types) is already being re-parsed on this thread, return early to break the cycle.
		// Using the mangled name instead of just the template name means legitimately-different
		// recursive instantiations (e.g. var_sum<int,int,int> called from var_sum<int,int,int,int>)
		// are NOT blocked — only truly recursive calls to the exact same specialisation are.
		static thread_local std::unordered_set<std::string_view> body_reparse_in_progress;
		std::string_view cycle_key = proper_mangled_name.view();
		if (body_reparse_in_progress.count(cycle_key)) {
			FLASH_LOG(Templates, Debug, "Cycle detected in body re-parsing for '", template_name, "' (mangled: '", cycle_key, "'), skipping body to break cycle");
			pack_param_info_ = std::move(saved_outer_pack_param_info);
			return ASTNode(&new_func_ref);
		}
		body_reparse_in_progress.insert(cycle_key);
		struct BodyReparseGuard {
			std::unordered_set<std::string_view>& set;
			std::string_view key;
			~BodyReparseGuard() { set.erase(key); }
		} body_reparse_guard{body_reparse_in_progress, cycle_key};

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
					subst.substituted_type.ref_qualifier = arg.type_specifier->reference_qualifier();
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

		if (!block_result.is_error() && block_result.node().has_value()) {
			// After parsing, we need to substitute template parameters in the body
			// This is essential for features like fold expressions that need AST transformation
			// Note: pack_param_info_ is still active here so PackExpansionExprNode expansion works
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

		// Restore pack parameter info (after substitution so PackExpansionExprNode can use it)
		has_parameter_packs_ = saved_has_parameter_packs;
		pack_param_info_ = std::move(saved_outer_pack_param_info);
		
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
