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
	substituted_return_type.set_reference_qualifier(return_type_spec.reference_qualifier());

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
			substituted_param_type.set_reference_qualifier(param_type_spec.reference_qualifier());

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
		FLASH_LOG(Templates, Debug, "Lazy member function body: re-parsing from saved position");
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
			type_info.reference_qualifier_ = lazy_info.template_args[i].is_rvalue_reference() ? ReferenceQualifier::RValueReference
				: (lazy_info.template_args[i].is_lvalue_reference() ? ReferenceQualifier::LValueReference : ReferenceQualifier::None);
			
			gTypesByName.emplace(type_info.name(), &type_info);
			template_scope.addParameter(&type_info);
		}

		// Save current position and parsing context
		SaveHandle current_pos = save_token_position();
		const FunctionDeclarationNode* saved_current_function = current_function_;
		
		// When re-parsing a lazy member function body with concrete types,
		// we're no longer in a dependent template context. Set parsing_template_body_
		// to false so that constant expressions like sizeof(int) are evaluated.
		bool saved_parsing_template_body = parsing_template_body_;
		parsing_template_body_ = false;

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
		parsing_template_body_ = saved_parsing_template_body;
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
				converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type, ttype_arg.type_index));
			}
		}

		// Push struct parsing context so that get_class_template_pack_size can find pack info in the registry
		// This is needed for sizeof...(Pack) to work in lazy member function bodies
		StructParsingContext struct_ctx;
		struct_ctx.struct_name = StringTable::getStringView(lazy_info.instantiated_class_name);
		struct_ctx.struct_node = nullptr;
		struct_ctx.local_struct_info = nullptr;
		struct_parsing_context_stack_.push_back(struct_ctx);
		
		try {
			ASTNode substituted_body = substituteTemplateParameters(
				*body_to_substitute,
				lazy_info.template_params,
				converted_template_args
			);
			new_func_ref.set_definition(substituted_body);
		} catch (const std::exception& e) {
			struct_parsing_context_stack_.pop_back();  // Clean up on error
			FLASH_LOG(Templates, Error, "Exception during lazy template parameter substitution for function ", 
			          decl.identifier_token().value(), ": ", e.what());
			throw;
		} catch (...) {
			struct_parsing_context_stack_.pop_back();  // Clean up on error
			FLASH_LOG(Templates, Error, "Unknown exception during lazy template parameter substitution for function ", 
			          decl.identifier_token().value());
			throw;
		}
		
		// Pop struct parsing context
		struct_parsing_context_stack_.pop_back();
	}

	// Copy function properties
	new_func_ref.set_is_constexpr(func_decl.is_constexpr());
	new_func_ref.set_is_consteval(func_decl.is_consteval());
	new_func_ref.set_is_constinit(func_decl.is_constinit());
	new_func_ref.set_noexcept(func_decl.is_noexcept());
	new_func_ref.set_is_variadic(func_decl.is_variadic());
	new_func_ref.set_is_static(func_decl.is_static());
	new_func_ref.set_linkage(func_decl.linkage());
	new_func_ref.set_calling_convention(func_decl.calling_convention());

	// Compute and set the proper mangled name for code generation
	// This is essential so that FunctionCallNode can carry the correct mangled name
	// and codegen can resolve the correct function for each template instantiation
	compute_and_set_mangled_name(new_func_ref);

	// Add the instantiated function to the AST so it gets visited during codegen
	// This is safe now that the StringBuilder bug is fixed
	ast_nodes_.push_back(new_func_node);
	
	// Also update the StructTypeInfo to replace the signature-only function with the full definition
	// Find the struct in gTypesByName
	auto struct_it = gTypesByName.find(lazy_info.instantiated_class_name);
	if (struct_it != gTypesByName.end()) {
		TypeInfo* struct_type_info = struct_it->second;
		StructTypeInfo* struct_info = struct_type_info->getStructInfo();
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
	
	StructTypeInfo* struct_info = type_it->second->getStructInfo();
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
		
		// Add member to nested struct info
		nested_struct_info->addMember(
			member_name_handle,
			substituted_type,
			substituted_type_index,
			member_size,
			member_alignment,
			member_decl.access,
			std::nullopt,  // No default initializer for now
			type_spec.reference_qualifier(),
			member_size * 8,
			false,  // is_array
			{},     // array_dimensions
			static_cast<int>(type_spec.pointer_depth()),
			member_decl.bitfield_width
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
