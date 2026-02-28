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
	
	// Early check: skip concepts - they are not class templates and should not be instantiated here
	// Concepts like same_as, convertible_to are stored in the concept registry, not the template registry
	{
		// Try both unqualified and with std:: prefix
		if (gConceptRegistry.hasConcept(template_name)) {
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping try_instantiate_class_template for concept '{}'", template_name);
			return std::nullopt;
		}
		// Also check without namespace prefix (e.g., "std::same_as" -> "same_as")
		size_t last_colon_pos = template_name.rfind("::");
		if (last_colon_pos != std::string_view::npos) {
			std::string_view simple_name = template_name.substr(last_colon_pos + 2);
			if (gConceptRegistry.hasConcept(simple_name)) {
				FLASH_LOG_FORMAT(Templates, Debug, "Skipping try_instantiate_class_template for concept '{}'", template_name);
				return std::nullopt;
			}
		}
	}
	
	// Check if any template arguments are dependent (contain template parameters)
	// If so, we cannot instantiate the template yet - it's a dependent type
	for (const auto& arg : template_args) {
		if (arg.is_dependent) {
			FLASH_LOG_FORMAT(Templates, Debug, "Skipping instantiation of {} - template arguments are dependent", template_name);
			
			// Register a placeholder TypeInfo for the dependent instantiated name
			// so that extractBaseTemplateName() can identify it via TypeInfo metadata
			// without needing string parsing (find('$')).
			std::string_view inst_name = get_instantiated_class_name(template_name, template_args);
			StringHandle inst_handle = StringTable::getOrInternStringHandle(inst_name);
			if (gTypesByName.find(inst_handle) == gTypesByName.end()) {
				auto& type_info = gTypeInfo.emplace_back();
				type_info.type_ = Type::UserDefined;
				type_info.type_index_ = gTypeInfo.size() - 1;
				type_info.type_size_ = 0;
				type_info.name_ = inst_handle;
				auto template_args_info = convertToTemplateArgInfo(template_args);
				type_info.setTemplateInstantiationInfo(
					QualifiedIdentifier::fromQualifiedName(template_name, gSymbolTable.get_current_namespace_handle()),
					template_args_info);
				gTypesByName[inst_handle] = &type_info;
				FLASH_LOG_FORMAT(Templates, Debug, "Registered dependent placeholder '{}' with base template '{}'", inst_name, template_name);
			}
			
			// Return success (nullopt) but don't actually instantiate
			// The type will be resolved during actual template instantiation
			return std::nullopt;
		}
	}
	
	// Check TypeIndex-based instantiation cache for O(1) lookup
	// This uses TypeIndex instead of string keys to avoid ambiguity with type names containing underscores
	std::string_view normalized_template_name = template_name;
	if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
		normalized_template_name = template_name.substr(last_colon + 2);
	}
	StringHandle template_name_handle = StringTable::getOrInternStringHandle(normalized_template_name);
	auto cache_key = FlashCpp::makeInstantiationKey(template_name_handle, template_args);
	auto cached = gTemplateRegistry.getInstantiation(cache_key);
	if (cached.has_value()) {
		FLASH_LOG_FORMAT(Templates, Debug, "Cache hit for '{}' with {} args", template_name, template_args.size());
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
	
	// Helper lambda to resolve a deferred bitfield width from non-type template parameters
	auto resolve_bitfield_width = [&](
		const StructMemberDecl& member_decl,
		const std::vector<ASTNode>& params,
		const std::vector<TemplateTypeArg>& args) -> std::optional<size_t> {
		if (member_decl.bitfield_width.has_value()) return member_decl.bitfield_width;
		if (!member_decl.bitfield_width_expr.has_value()) return std::nullopt;
		std::unordered_map<TypeIndex, TemplateTypeArg> type_sub_map;
		std::unordered_map<std::string_view, int64_t> nontype_sub_map;
		for (size_t pi = 0; pi < params.size() && pi < args.size(); ++pi) {
			if (!params[pi].is<TemplateParameterNode>()) continue;
			const auto& tparam = params[pi].as<TemplateParameterNode>();
			if (tparam.kind() == TemplateParameterKind::NonType && args[pi].is_value)
				nontype_sub_map[tparam.name()] = args[pi].value;
		}
		ASTNode substituted = substitute_template_params_in_expression(
			*member_decl.bitfield_width_expr, type_sub_map, nontype_sub_map);
		ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
		auto eval_result = ConstExpr::Evaluator::evaluate(substituted, eval_ctx);
		if (eval_result.success() && eval_result.as_int() >= 0)
			return static_cast<size_t>(eval_result.as_int());
		return std::nullopt;
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
															TemplateTypeArg arg(val ? 1LL : 0LL, Type::Bool);
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
						filled_args_for_pattern_match.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL, Type::Bool));
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
			ASTNode& pattern_node = *pattern_match_opt;
		
		// Handle both StructDeclarationNode (top-level partial specialization) and
		// TemplateClassDeclarationNode (member template partial specialization)
		StructDeclarationNode* pattern_struct_ptr = nullptr;
		if (pattern_node.is<StructDeclarationNode>()) {
			pattern_struct_ptr = &pattern_node.as<StructDeclarationNode>();
		} else if (pattern_node.is<TemplateClassDeclarationNode>()) {
			// Member template partial specialization - extract the inner struct
			pattern_struct_ptr = &pattern_node.as<TemplateClassDeclarationNode>().class_decl_node();
		} else {
			FLASH_LOG(Templates, Error, "Pattern node is not a StructDeclarationNode or TemplateClassDeclarationNode");
			return std::nullopt;
		}
		
		StructDeclarationNode& pattern_struct = *pattern_struct_ptr;
		FLASH_LOG(Templates, Debug, "Pattern struct name: ", pattern_struct.name());
		
		// Register the mapping from instantiated name to pattern name
		// This allows member alias lookup to find the correct specialization
		gTemplateRegistry.register_instantiation_pattern(instantiated_name, pattern_struct.name());
		
		// Get template parameters from the pattern (partial specialization), NOT the primary template
		// The pattern stores its own template parameters (e.g., <typename First, typename... Rest>)
		std::vector<ASTNode> pattern_template_params;
		auto patterns_it = gTemplateRegistry.specialization_patterns_.find(template_name);
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
			// Check ALL template overloads to find one with named parameters
			// Forward declarations like `template<typename...> class tuple;` register with
			// anonymous names (e.g., __anon_type_64), while definitions have real names (e.g., _Elements).
			// Prefer the definition's parameters for correct sizeof...() resolution.
			const auto* all_tmpls = gTemplateRegistry.lookupAllTemplates(template_name);
			if (all_tmpls) {
				const TemplateClassDeclarationNode* best = nullptr;
				for (const auto& tmpl_node : *all_tmpls) {
					if (tmpl_node.is<TemplateClassDeclarationNode>()) {
						const auto& tmpl_class = tmpl_node.as<TemplateClassDeclarationNode>();
						if (!best) {
							best = &tmpl_class;
						} else {
							// Prefer template with named parameters (not __anon_type_)
							bool current_has_anon = false;
							bool best_has_anon = false;
							for (const auto& param : tmpl_class.template_parameters()) {
								if (param.is<TemplateParameterNode>() && param.as<TemplateParameterNode>().name().starts_with("__anon_type_"))
									current_has_anon = true;
							}
							for (const auto& param : best->template_parameters()) {
								if (param.is<TemplateParameterNode>() && param.as<TemplateParameterNode>().name().starts_with("__anon_type_"))
									best_has_anon = true;
							}
							if (best_has_anon && !current_has_anon)
								best = &tmpl_class;
						}
					}
				}
				if (best) {
					pattern_template_params = std::vector<ASTNode>(best->template_parameters().begin(),
					                                               best->template_parameters().end());
				}
			}
		}
		const std::vector<ASTNode>& template_params = pattern_template_params;
		
		// Push class template pack info for specialization path
		ClassTemplatePackGuard spec_pack_guard(class_template_pack_stack_);
		bool has_spec_pack_info = false;
		{
			std::vector<ClassTemplatePackInfo> pack_infos;
			size_t non_variadic_count = 0;
			for (size_t i = 0; i < template_params.size(); ++i) {
				if (template_params[i].is<TemplateParameterNode>()) {
					const auto& tparam = template_params[i].as<TemplateParameterNode>();
					if (tparam.is_variadic()) {
						size_t pack_size = template_args.size() >= non_variadic_count 
							? template_args.size() - non_variadic_count : 0;
						pack_infos.push_back({tparam.name(), pack_size});
					} else {
						non_variadic_count++;
					}
				}
			}
			if (!pack_infos.empty()) {
				spec_pack_guard.push(std::move(pack_infos));
				has_spec_pack_info = true;
			}
		}
		
		// Create a new struct with the instantiated name
		// Copy members from the pattern, substituting template parameters
		// For now, if members use template parameters, we substitute them
		
		// Create struct type info first
		TypeInfo& struct_type_info = add_struct_type(instantiated_name);
		
		// Store template instantiation metadata for O(1) lookup (Phase 6)
		struct_type_info.setTemplateInstantiationInfo(
			QualifiedIdentifier::fromQualifiedName(template_name, gSymbolTable.get_current_namespace_handle()),
			convertToTemplateArgInfo(template_args)
		);
		
		// Register class template pack sizes in persistent registry for specializations
		// Only register if this specialization actually has variadic parameters
		if (has_spec_pack_info) {
			class_template_pack_registry_[instantiated_name] = class_template_pack_stack_.back();
		}
		
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
			
			// WORKAROUND: If the base class name is an incomplete template instantiation, it was instantiated
			// during pattern parsing with template parameters. We need to re-instantiate
			// it with the concrete template arguments.
			// Use TypeInfo metadata to detect incomplete instantiations and extract the base template name.
			StringHandle base_name_handle = StringTable::getOrInternStringHandle(base_name_str);
			auto incomplete_type_it = gTypesByName.find(base_name_handle);
			bool base_is_incomplete = incomplete_type_it != gTypesByName.end()
				&& incomplete_type_it->second->is_incomplete_instantiation_;
			if (base_is_incomplete && incomplete_type_it->second->isTemplateInstantiation()) {
				std::string_view base_template_name = StringTable::getStringView(
					incomplete_type_it->second->baseTemplateName());
				
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
			
			ReferenceQualifier ref_qual = type_spec.reference_qualifier();
			
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
				ref_qual,
				ref_qual != ReferenceQualifier::None ? get_type_size_bits(member_type) : 0,
				false,
				{},
				static_cast<int>(ptr_depth),
				resolve_bitfield_width(member_decl, template_params, template_args)
			);
		}
		
		// Copy member functions from pattern
		for (StructMemberFunctionDecl& mem_func : pattern_struct.member_functions()) {
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
			} else if (mem_func.function_declaration.is<TemplateFunctionDeclarationNode>()) {
				// Member function template (e.g., template<typename _Up, typename... _Args> void construct(...))
				// Add as-is without return type substitution - the template will handle it when instantiated
				const TemplateFunctionDeclarationNode& tmpl_func = mem_func.function_declaration.as<TemplateFunctionDeclarationNode>();
				const FunctionDeclarationNode& inner_func = tmpl_func.function_decl_node();
				StringHandle func_name_handle = inner_func.decl_node().identifier_token().handle();
				struct_info->addMemberFunction(
					func_name_handle,
					mem_func.function_declaration,
					mem_func.access,
					mem_func.is_virtual,
					mem_func.is_pure_virtual,
					mem_func.is_override,
					mem_func.is_final
				);
			} else {
				FunctionDeclarationNode& orig_func = mem_func.function_declaration.as<FunctionDeclarationNode>();
				DeclarationNode& orig_decl = orig_func.decl_node();
				
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
						static_member.is_const,
						static_member.reference_qualifier,
						static_member.pointer_depth
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
					static_member.is_const,
					static_member.reference_qualifier,
					static_member.pointer_depth
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
		auto patterns_it_for_alias = gTemplateRegistry.specialization_patterns_.find(template_name);
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
		for (StructMemberFunctionDecl& mem_func : pattern_struct.member_functions()) {
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
			} else if (mem_func.function_declaration.is<TemplateFunctionDeclarationNode>()) {
				// Member function template - add as-is without creating new node
				// The template will be instantiated on demand when called
				instantiated_struct_ref.add_member_function(
					mem_func.function_declaration,
					mem_func.access
				);
			} else {
				FunctionDeclarationNode& orig_func = mem_func.function_declaration.as<FunctionDeclarationNode>();
				auto new_func_node = emplace_node<FunctionDeclarationNode>(
					orig_func.decl_node(),  // Reuse declaration
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
		
		// Register in cache for O(1) lookup on future instantiations
		gTemplateRegistry.registerInstantiation(cache_key, instantiated_struct);
		
		return instantiated_struct;  // Return the struct node for code generation
		}
	}

	// No specialization found - use the primary template
	ASTNode template_node;
	{
		PROFILE_TEMPLATE_LOOKUP();
		auto template_opt = gTemplateRegistry.lookupTemplate(template_name);
		if (!template_opt.has_value()) {
			// If we're inside a template body, the template might be referencing itself
			// (self-referential templates like __ratio_add_impl). In this case, the template
			// hasn't been registered yet because we're still parsing its body.
			// Check if the name matches the struct currently being defined.
			if (parsing_template_body_ || !current_template_param_names_.empty()) {
				// Check struct_parsing_context_stack_ for self-reference
				for (auto it = struct_parsing_context_stack_.rbegin(); it != struct_parsing_context_stack_.rend(); ++it) {
					std::string_view struct_name = it->struct_name;
					// Compare with both unqualified and potentially qualified names
					if (struct_name == template_name) {
						FLASH_LOG_FORMAT(Templates, Debug, "Self-referential template '{}' in body - deferring", template_name);
						return std::nullopt;
					}
					// Also try stripping namespace prefix from struct_name
					size_t colon_pos = struct_name.rfind("::");
					if (colon_pos != std::string_view::npos) {
						std::string_view unqualified = struct_name.substr(colon_pos + 2);
						if (unqualified == template_name) {
							FLASH_LOG_FORMAT(Templates, Debug, "Self-referential template '{}' in body - deferring", template_name);
							return std::nullopt;
						}
					}
				}
			}
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
	
	// Push class template pack info for sizeof...() resolution in member function templates
	// This RAII guard ensures the pack info is available during the entire instantiation scope
	ClassTemplatePackGuard class_pack_guard(class_template_pack_stack_);
	if (has_parameter_pack) {
		std::vector<ClassTemplatePackInfo> pack_infos;
		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
			if (param.is_variadic()) {
				size_t pack_size = template_args.size() >= non_variadic_param_count 
					? template_args.size() - non_variadic_param_count : 0;
				pack_infos.push_back({param.name(), pack_size});
				FLASH_LOG(Templates, Debug, "Registered class template pack '", param.name(), "' with size ", pack_size);
			}
		}
		if (!pack_infos.empty()) {
			class_pack_guard.push(std::move(pack_infos));
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
		
		// Track size before processing to detect if a value was pushed.
		// Every non-variadic iteration MUST push exactly one element so that
		// filled_template_args[j] stays in sync with template_params[j].
		size_t size_before = filled_template_args.size();
		
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
			for (size_t j = 0; j < i && j < template_params.size() && j < filled_template_args.size(); ++j) {
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
						
						if (is_dependent && !filled_template_args.empty()) {
							
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
														filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL, Type::Bool));
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
					filled_template_args.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL, Type::Bool));
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
															filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL, Type::Bool));
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
			
			// NonType fallback: if no handler above pushed a value, try ConstExprEvaluator
			if (filled_template_args.size() == size_before) {
				if (substituted_default_node.is<ExpressionNode>()) {
					ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
					auto eval_result = ConstExpr::Evaluator::evaluate(substituted_default_node, eval_ctx);
					if (eval_result.success()) {
						filled_template_args.push_back(TemplateTypeArg(eval_result.as_int()));
						FLASH_LOG(Templates, Debug, "Evaluated non-type default via ConstExprEvaluator: ", eval_result.as_int());
					}
				}
			}
		}
		
		// Catch-all: ensure filled_template_args grows by exactly 1 per non-variadic
		// parameter so that filled_template_args[j] stays in sync with template_params[j].
		// This covers: Type defaults whose node isn't TypeSpecifierNode, NonType defaults
		// that no handler could evaluate, and any other unhandled parameter kind.
		if (filled_template_args.size() == size_before) {
			if (param.kind() == TemplateParameterKind::Type) {
				// Push a void-like placeholder type
				TemplateTypeArg placeholder;
				placeholder.base_type = Type::Void;
				filled_template_args.push_back(placeholder);
				FLASH_LOG(Templates, Warning, "Could not resolve type default for param ", i,
				          " of '", template_name, "', using placeholder");
			} else {
				filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(0)));
				FLASH_LOG(Templates, Warning, "Could not evaluate default for param ", i,
				          " of '", template_name, "', using 0");
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
				// Regular scalar parameter (type or non-type)
				if (arg_index < template_args_to_use.size()) {
					name_substitution_map[param_name] = template_args_to_use[arg_index];
					FLASH_LOG(Templates, Debug, "Added substitution: ", param_name, " -> base_type=", (int)template_args_to_use[arg_index].base_type, " type_index=", template_args_to_use[arg_index].type_index, " is_value=", template_args_to_use[arg_index].is_value);
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
	// QualifiedIdentifier captures both the namespace and unqualified name.
	// When template_name is unqualified, derive namespace from class_decl.name()
	// (the template declaration's struct, which stores the full qualified name).
	{
		NamespaceHandle fallback_ns = gSymbolTable.get_current_namespace_handle();
		if (template_name.find("::") == std::string_view::npos) {
			// Unqualified template_name — derive namespace from the class declaration name
			std::string_view decl_name = StringTable::getStringView(class_decl.name());
			if (size_t pos = decl_name.rfind("::"); pos != std::string_view::npos) {
				// decl_name is qualified (e.g. "std::vector"), resolve from global scope
				fallback_ns = QualifiedIdentifier::fromQualifiedName(decl_name, NamespaceRegistry::GLOBAL_NAMESPACE).namespace_handle;
			}
		}
		struct_type_info.setTemplateInstantiationInfo(
			QualifiedIdentifier::fromQualifiedName(template_name, fallback_ns),
			convertToTemplateArgInfo(template_args_to_use)
		);
	}
	
	// Register class template pack sizes in persistent registry for member function template lookup
	if (has_parameter_pack) {
		std::vector<ClassTemplatePackInfo> pack_infos;
		for (size_t i = 0; i < template_params.size(); ++i) {
			const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
			if (param.is_variadic()) {
				size_t pack_size = template_args_to_use.size() >= non_variadic_param_count 
					? template_args_to_use.size() - non_variadic_param_count : 0;
				pack_infos.push_back({param.name(), pack_size});
			}
		}
		if (!pack_infos.empty()) {
			class_template_pack_registry_[instantiated_name] = std::move(pack_infos);
		}
	}
	
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
			
			// Use name_substitution_map (which correctly handles all param kinds) to resolve
			ensure_substitution_maps();
			auto subst_it = name_substitution_map.find(base_class_name);
			bool found = false;
			if (subst_it != name_substitution_map.end()) {
				const TemplateTypeArg& concrete_arg = subst_it->second;
				
				// Validate that the concrete type is a struct/class
				if (concrete_arg.type_index >= gTypeInfo.size()) {
					FLASH_LOG(Templates, Error, "Template argument for base class has invalid type_index: ", concrete_arg.type_index);
				} else {
					const TypeInfo& concrete_type = gTypeInfo[concrete_arg.type_index];
					if (concrete_type.type_ != Type::Struct) {
						FLASH_LOG(Templates, Error, "Template argument '", concrete_type.name_, "' for base class must be a struct/class type");
					} else if (concrete_type.struct_info_ && concrete_type.struct_info_->is_final) {
						FLASH_LOG(Templates, Error, "Cannot inherit from final class '", concrete_type.name_, "'");
					} else {
						// Add the resolved base class
						struct_info->addBaseClass(StringTable::getStringView(concrete_type.name_), concrete_arg.type_index, base.access, base.is_virtual);
						FLASH_LOG(Templates, Debug, "Resolved template parameter base '", base_class_name, "' to concrete type '", StringTable::getStringView(concrete_type.name_), "' with type_index=", concrete_arg.type_index);
						found = true;
					}
				}
			}
			
			if (!found) {
				// Check if this is a variadic pack parameter (e.g., struct Combined : Bases...)
				// Pack params are in pack_substitution_map, not name_substitution_map
				ensure_substitution_maps();
				StringHandle base_name_handle = StringTable::getOrInternStringHandle(base_class_name);
				auto pack_it = pack_substitution_map.find(base_name_handle);
				if (pack_it != pack_substitution_map.end()) {
					for (const TemplateTypeArg& pack_arg : pack_it->second) {
						if (pack_arg.type_index < gTypeInfo.size()) {
							const TypeInfo& concrete_type = gTypeInfo[pack_arg.type_index];
							if (concrete_type.type_ == Type::Struct &&
							    !(concrete_type.struct_info_ && concrete_type.struct_info_->is_final)) {
								struct_info->addBaseClass(StringTable::getStringView(concrete_type.name_), pack_arg.type_index, base.access, base.is_virtual);
								FLASH_LOG(Templates, Debug, "Expanded pack base '", base_class_name, "' -> '", StringTable::getStringView(concrete_type.name_), "'");
								found = true;
							}
						}
					}
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
							subst.ref_qualifier = type_spec.reference_qualifier();
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
									inst_arg.ref_qualifier = type_spec.reference_qualifier();
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
										subst.ref_qualifier = type_spec.reference_qualifier();
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
		substituted_type_spec.set_reference_qualifier(type_spec.reference_qualifier());
		
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
		ReferenceQualifier ref_qual = type_spec.reference_qualifier();
	
		// For reference members, we need to pass the size of the referenced type, not the pointer size
		size_t referenced_size_bits = 0;
		if (ref_qual != ReferenceQualifier::None) {
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
			ref_qual,
			referenced_size_bits,
			false,
			{},
			static_cast<int>(type_spec.pointer_depth()),
			resolve_bitfield_width(member_decl, template_params, template_args_to_use)
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
					static_member.is_const,
					static_member.reference_qualifier,
					static_member.pointer_depth
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
			
			// General fallback: use ExpressionSubstitutor to substitute any remaining template
			// parameters in the initializer. This handles cases like V + W where V and W are
			// non-type template parameters that the specific handlers above didn't cover.
			if (substituted_initializer.has_value()) {
				std::unordered_map<std::string_view, TemplateTypeArg> param_map;
				for (size_t pi = 0; pi < template_params.size() && pi < template_args_to_use.size(); ++pi) {
					if (template_params[pi].is<TemplateParameterNode>()) {
						const TemplateParameterNode& param = template_params[pi].as<TemplateParameterNode>();
						param_map[param.name()] = template_args_to_use[pi];
					}
				}
				if (!param_map.empty()) {
					ExpressionSubstitutor substitutor(param_map, *this);
					substituted_initializer = substitutor.substitute(substituted_initializer.value());
					FLASH_LOG(Templates, Debug, "Applied general ExpressionSubstitutor to static member initializer");
					
					// Try to evaluate the substituted expression to a constant value
					// This turns expressions like "1 + 2" into a single NumericLiteralNode(3)
					if (substituted_initializer.has_value() && substituted_initializer->is<ExpressionNode>()) {
						ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
						eval_ctx.parser = this;
						auto eval_result = ConstExpr::Evaluator::evaluate(*substituted_initializer, eval_ctx);
						if (eval_result.success()) {
							int64_t val = eval_result.as_int();
							std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(val)).commit();
							Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
							substituted_initializer = emplace_node<ExpressionNode>(
								NumericLiteralNode(num_token, static_cast<unsigned long long>(val), Type::Int, TypeQualifier::None, 32)
							);
							FLASH_LOG(Templates, Debug, "Evaluated substituted static member initializer to: ", val);
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
				static_member.is_const,
				static_member.reference_qualifier,
				static_member.pointer_depth
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
			static_member.is_const,
			static_member.reference_qualifier,
			static_member.pointer_depth
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
				
				ReferenceQualifier ref_qual = substituted_type_spec.reference_qualifier();
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
					ref_qual,
					ref_qual != ReferenceQualifier::None ? get_type_size_bits(substituted_type_spec.type()) : 0,
					false,
					{},
					static_cast<int>(substituted_type_spec.pointer_depth()),
					member_decl.bitfield_width
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
							static_member.is_const,
							static_member.reference_qualifier,
							static_member.pointer_depth
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
								static_member.is_const,
								static_member.reference_qualifier,
								static_member.pointer_depth
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

	// Process out-of-line nested class definitions
	// These are patterns like: template<typename T> struct Outer<T>::Inner { T data; };
	// The definition was saved during parsing and is now re-parsed with template parameter substitution.
	auto ool_nested_classes = gTemplateRegistry.getOutOfLineNestedClasses(template_name);
	FLASH_LOG(Templates, Debug, "Processing ", ool_nested_classes.size(), " out-of-line nested class definitions for ", template_name);
	for (const auto& ool_nested : ool_nested_classes) {
		// Full specializations (template<>) store concrete args — skip if they don't match
		// this instantiation's arguments (e.g., Wrapper<int>::Nested shouldn't apply to Wrapper<float>).
		if (!ool_nested.specialization_args.empty() &&
		    (ool_nested.specialization_args.size() != template_args_to_use.size() ||
		     !std::equal(ool_nested.specialization_args.begin(), ool_nested.specialization_args.end(),
		                 template_args_to_use.begin()))) {
			continue;
		}

		std::string_view nested_name = StringTable::getStringView(ool_nested.nested_class_name);
		auto qualified_name = StringTable::getOrInternStringHandle(
			StringBuilder().append(instantiated_name).append("::"sv).append(nested_name));
		
		// Check if already registered - skip only if it has actual members (from inline definition)
		// Forward-declared nested classes are registered with no members; those need to be replaced.
		auto existing_it = gTypesByName.find(qualified_name);
		if (existing_it != gTypesByName.end()) {
			TypeInfo* existing_nested_type = existing_it->second;
			if (existing_nested_type->getStructInfo() && !existing_nested_type->getStructInfo()->members.empty()) {
				FLASH_LOG(Templates, Debug, "Out-of-line nested class already has members: ", StringTable::getStringView(qualified_name));
				continue;
			}
			FLASH_LOG(Templates, Debug, "Replacing forward-declared nested class: ", StringTable::getStringView(qualified_name));
		}
		
		// Save current lexer position and parser state
		SaveHandle saved_pos = save_token_position();
		auto saved_template_body = parsing_template_body_;
		auto saved_template_class = parsing_template_class_;
		auto saved_param_names = current_template_param_names_;
		auto saved_delayed_bodies = std::move(delayed_function_bodies_);
		delayed_function_bodies_.clear();
		
		// Set up template parsing context so template parameter types resolve correctly
		parsing_template_body_ = true;
		parsing_template_class_ = true;
		current_template_param_names_ = ool_nested.template_param_names;

		// Restore lexer to the saved position (at the struct/class keyword).
		// Push the instantiated template onto struct_parsing_context_stack_ so that
		// parse_struct_declaration() builds the correct qualified name (e.g., "Wrapper$hash::Nested")
		restore_lexer_position_only(ool_nested.body_start);
		
		struct_parsing_context_stack_.push_back({
			StringTable::getStringView(instantiated_name),
			nullptr,  // struct_node — not needed; parse_struct_declaration() creates its own
			struct_info.get(),
			gSymbolTable.get_current_namespace_handle(),
			{}
		});
		
		// Reuse parse_struct_declaration() which handles everything: type registration,
		// base class parsing, constructors, destructors, using declarations, member
		// functions, data members, layout computation, StructTypeInfo finalization, etc.
		auto nested_result = parse_struct_declaration();
		
		struct_parsing_context_stack_.pop_back();
		
		if (nested_result.is_error()) {
			FLASH_LOG(Templates, Warning, "Failed to parse out-of-line nested class: ",
			          StringTable::getStringView(qualified_name));
		} else {
			FLASH_LOG(Templates, Debug, "Parsed out-of-line nested class via parse_struct_declaration(): ",
			          StringTable::getStringView(qualified_name));
		}
		
		// Restore parser state
		current_template_param_names_ = saved_param_names;
		parsing_template_body_ = saved_template_body;
		parsing_template_class_ = saved_template_class;
		delayed_function_bodies_ = std::move(saved_delayed_bodies);
		restore_lexer_position_only(saved_pos);
	}

	// Fix up struct members whose types were unresolved nested classes.
	// During member processing above, nested classes haven't been instantiated yet,
	// so members of type "Wrapper::Nested" (with size 0) remain unresolved.
	// Now that nested classes are registered as "Wrapper$hash::Nested", update those members.
	{
		StructTypeInfo* si = struct_info.get();
		if (si) {
			bool had_fixup = false;
			for (auto& member : si->members) {
				if (member.size == 0 && member.type_index < gTypeInfo.size()) {
					const TypeInfo& mem_type_info = gTypeInfo[member.type_index];
					std::string_view mem_type_name = StringTable::getStringView(mem_type_info.name());
					// Check if this is a nested class of the current template (e.g., "Wrapper::Nested")
					if (mem_type_name.starts_with(template_name) && mem_type_name.size() > template_name.size() + 2 &&
					    mem_type_name.substr(template_name.size(), 2) == "::") {
						std::string_view nested_name = mem_type_name.substr(template_name.size() + 2);
						StringBuilder sb;
						StringHandle resolved_handle = StringTable::getOrInternStringHandle(
							sb.append(instantiated_name).append("::").append(nested_name).commit());
						auto resolved_it = gTypesByName.find(resolved_handle);
						if (resolved_it != gTypesByName.end()) {
							const TypeInfo* resolved_type = resolved_it->second;
							member.type = resolved_type->type_;
							member.type_index = resolved_type->type_index_;
							if (resolved_type->getStructInfo()) {
								member.size = resolved_type->getStructInfo()->total_size;
								member.alignment = resolved_type->getStructInfo()->alignment;
							}
							had_fixup = true;
							FLASH_LOG(Templates, Debug, "Fixed nested class member '", StringTable::getStringView(member.name),
							          "': ", mem_type_name, " -> ", StringTable::getStringView(resolved_handle),
							          " (size=", member.size, ")");
						}
					}
				}
			}

			// Recalculate struct layout from scratch after nested class member fixup
			if (had_fixup) {
				size_t new_total = 0;
				size_t new_alignment = 1;
				for (auto& member : si->members) {
					size_t eff_align = member.alignment;
					if (si->pack_alignment > 0 && si->pack_alignment < eff_align) {
						eff_align = si->pack_alignment;
					}
					member.offset = si->is_union ? 0 : ((new_total + eff_align - 1) & ~(eff_align - 1));
					new_total = member.offset + member.size;
					new_alignment = std::max(new_alignment, eff_align);
				}
				si->total_size = (new_total + new_alignment - 1) & ~(new_alignment - 1);
				si->alignment = new_alignment;
				struct_type_info.type_size_ = si->total_size;
				FLASH_LOG(Templates, Debug, "Re-laid out struct ", instantiated_name,
				          " after nested class fixup, total_size=", si->total_size);
			}
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
				gTemplateRegistry.register_alias_template(inst_alias_name, *alias_opt);
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
			if (use_lazy_instantiation &&
			    instantiated_name.view().find("::") == std::string_view::npos &&
			    StringTable::getStringView(class_decl.name()).find("::") == std::string_view::npos &&
			    (func_decl.get_definition().has_value() || func_decl.has_template_body_position())) {
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
				substituted_return_type.set_reference_qualifier(return_type_spec.reference_qualifier());

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
						substituted_param_type.set_reference_qualifier(param_type_spec.reference_qualifier());

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
				new_func_ref.set_is_static(func_decl.is_static());
				new_func_ref.set_linkage(func_decl.linkage());
				new_func_ref.set_calling_convention(func_decl.calling_convention());
				new_func_ref.set_is_implicit(func_decl.is_implicit());

				// Add the signature-only function to the instantiated struct
				if (mem_func.is_operator_overload) {
					instantiated_struct_ref.add_operator_overload(mem_func.operator_symbol, new_func_node, mem_func.access);
				} else {
					instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
				}
				
				// Also add to struct_info so it can be found during codegen
				if (mem_func.is_operator_overload) {
					struct_info_ptr->addOperatorOverload(mem_func.operator_symbol, new_func_node, mem_func.access,
						mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
				} else {
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
				substituted_return_type.set_reference_qualifier(return_type_spec.reference_qualifier());

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
						substituted_param_type.set_reference_qualifier(param_type_spec.reference_qualifier());

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
					FLASH_LOG(Templates, Debug, "Function has definition, using parsed body");
					body_to_substitute = func_decl.get_definition();
				} else if (func_decl.has_template_body_position()) {
					// Re-parse the function body from saved position
					// This is needed for member struct templates where body parsing is deferred
					FLASH_LOG(Templates, Debug, "Function has template body position, re-parsing");
					
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
						type_info.reference_qualifier_ = template_args_to_use[i].is_rvalue_reference() ? ReferenceQualifier::RValueReference
							: (template_args_to_use[i].is_lvalue_reference() ? ReferenceQualifier::LValueReference : ReferenceQualifier::None);
						
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
							converted_template_args.push_back(TemplateArgument::makeType(ttype_arg.base_type, ttype_arg.type_index));
						}
					}

					FLASH_LOG(Templates, Debug, "About to substitute template parameters in function body for struct: ", StringTable::getStringView(instantiated_name));
					
					// Push struct parsing context so that get_class_template_pack_size can find pack info in the registry
					// This is needed for sizeof...(Pack) to work in eager member function body substitution
					StructParsingContext struct_ctx;
					struct_ctx.struct_name = StringTable::getStringView(instantiated_name);
					struct_ctx.struct_node = nullptr;
					struct_ctx.local_struct_info = nullptr;
					struct_parsing_context_stack_.push_back(struct_ctx);
					
					FLASH_LOG(Templates, Debug, "Pushed struct context: ", struct_ctx.struct_name);

					try {
						ASTNode substituted_body = substituteTemplateParameters(
							*body_to_substitute,
							template_params,
							converted_template_args
						);
						new_func_ref.set_definition(substituted_body);
						FLASH_LOG(Templates, Debug, "Successfully substituted function body");
					} catch (const std::exception& e) {
						struct_parsing_context_stack_.pop_back();  // Clean up on error
						FLASH_LOG(Templates, Error, "Exception during template parameter substitution for function ", 
						          decl.identifier_token().value(), ": ", e.what());
						throw;
					} catch (...) {
						struct_parsing_context_stack_.pop_back();  // Clean up on error
						FLASH_LOG(Templates, Error, "Unknown exception during template parameter substitution for function ", 
						          decl.identifier_token().value());
						throw;
					}
					
					// Pop struct parsing context
					struct_parsing_context_stack_.pop_back();
					FLASH_LOG(Templates, Debug, "Popped struct context");
				}

				// Copy function specifiers from original
				new_func_ref.set_is_constexpr(func_decl.is_constexpr());
				new_func_ref.set_is_consteval(func_decl.is_consteval());
				new_func_ref.set_is_constinit(func_decl.is_constinit());
				new_func_ref.set_noexcept(func_decl.is_noexcept());
				new_func_ref.set_is_variadic(func_decl.is_variadic());
				new_func_ref.set_is_static(func_decl.is_static());
				new_func_ref.set_linkage(func_decl.linkage());
				new_func_ref.set_calling_convention(func_decl.calling_convention());
				new_func_ref.set_is_implicit(func_decl.is_implicit());

				// Add the substituted function to the instantiated struct
				if (mem_func.is_operator_overload) {
					instantiated_struct_ref.add_operator_overload(mem_func.operator_symbol, new_func_node, mem_func.access);
				} else {
					instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
				}
				
				// Also add to struct_info so it can be found during codegen
				// Phase 7B: Intern function name and use StringHandle overload
				if (mem_func.is_operator_overload) {
					struct_info_ptr->addOperatorOverload(mem_func.operator_symbol, new_func_node, mem_func.access,
						mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
				} else {
					StringHandle func_name_handle = decl.identifier_token().handle();
					FLASH_LOG(Templates, Debug, "Adding member function '", StringTable::getStringView(func_name_handle), 
					          "' to struct_info for ", instantiated_name, ", parent_struct_name='", new_func_ref.parent_struct_name(), "'");
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
				substituted_return_type.set_reference_qualifier(return_type_spec.reference_qualifier());

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
						substituted_param_type.set_reference_qualifier(param_type_spec.reference_qualifier());

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
				new_func_ref.set_is_static(func_decl.is_static());
				new_func_ref.set_linkage(func_decl.linkage());
				new_func_ref.set_calling_convention(func_decl.calling_convention());
				new_func_ref.set_is_implicit(func_decl.is_implicit());

				// Add the substituted function to the instantiated struct
				if (mem_func.is_operator_overload) {
					instantiated_struct_ref.add_operator_overload(mem_func.operator_symbol, new_func_node, mem_func.access);
				} else {
					instantiated_struct_ref.add_member_function(new_func_node, mem_func.access);
				}
				
				// Also add to struct_info so it can be found during codegen
				// Phase 7B: Intern function name and use StringHandle overload
				if (mem_func.is_operator_overload) {
					struct_info_ptr->addOperatorOverload(mem_func.operator_symbol, new_func_node, mem_func.access,
						mem_func.is_virtual, mem_func.is_pure_virtual, mem_func.is_override, mem_func.is_final);
				} else {
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
							substituted_param_type.set_reference_qualifier(param_type_spec.reference_qualifier());

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
			// Member template functions need outer template parameters substituted
			// while keeping inner template parameters (e.g., auto → _T0) unchanged.
			// For example, in template<class _It, class _Sent> struct subrange:
			//   subrange(convertible_to<_It> auto __i, _Sent __s)
			// becomes a TemplateFunctionDeclarationNode with inner param _T0.
			// When instantiating subrange<int*, sentinel>, we need to substitute
			// _It→int* and _Sent→sentinel in the parameters, keeping _T0 as-is.
			const TemplateFunctionDeclarationNode& template_func = 
				mem_func.function_declaration.as<TemplateFunctionDeclarationNode>();
			
			FLASH_LOG(Templates, Debug, "Copying member template function to instantiated class with outer param substitution");
			
			const FunctionDeclarationNode& func_decl = 
				template_func.function_declaration().as<FunctionDeclarationNode>();
			const DeclarationNode& decl_node = func_decl.decl_node();
			
			// Substitute outer class template parameters in function parameter types
			// so that e.g. _Sent becomes sentinel_t when the class is instantiated
			bool needs_substitution = false;
			// Check return type
			{
				const auto& rtype = decl_node.type_node().as<TypeSpecifierNode>();
				if (rtype.type() == Type::UserDefined) {
					needs_substitution = true;
				}
			}
			// Check parameter types
			if (!needs_substitution) {
				for (const auto& param : func_decl.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const auto& ptype = param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
						if (ptype.type() == Type::UserDefined) {
							needs_substitution = true;
							break;
						}
					}
				}
			}
			
			if (needs_substitution) {
				// Create a new inner function with substituted non-auto parameter types
				const TypeSpecifierNode& return_type_spec = decl_node.type_node().as<TypeSpecifierNode>();
				auto [ret_type, ret_type_index] = substitute_template_parameter(
					return_type_spec, template_params, template_args_to_use);
				
				ASTNode new_return_type = emplace_node<TypeSpecifierNode>(
					ret_type, return_type_spec.qualifier(),
					get_type_size_bits(ret_type), return_type_spec.token(), return_type_spec.cv_qualifier());
				auto& new_return_spec = new_return_type.as<TypeSpecifierNode>();
				new_return_spec.set_type_index(ret_type_index);
				for (const auto& pl : return_type_spec.pointer_levels())
					new_return_spec.add_pointer_level(pl.cv_qualifier);
				new_return_spec.set_reference_qualifier(return_type_spec.reference_qualifier());
				
				auto [new_decl_node, new_decl_ref] = emplace_node_ref<DeclarationNode>(
					new_return_type, decl_node.identifier_token());
				auto [new_func_node, new_func_ref] = emplace_node_ref<FunctionDeclarationNode>(
					new_decl_ref);
				
				// Copy parameter nodes with outer template parameter substitution
				for (const auto& param : func_decl.parameter_nodes()) {
					if (param.is<DeclarationNode>()) {
						const DeclarationNode& param_decl = param.as<DeclarationNode>();
						const TypeSpecifierNode& param_type_spec = param_decl.type_node().as<TypeSpecifierNode>();
						
						Type new_param_type = param_type_spec.type();
						TypeIndex new_param_type_index = param_type_spec.type_index();
						
						// Only substitute UserDefined types (not Auto, which is inner template)
						if (new_param_type == Type::UserDefined) {
							auto [subst_type, subst_idx] = substitute_template_parameter(
								param_type_spec, template_params, template_args_to_use);
							new_param_type = subst_type;
							new_param_type_index = subst_idx;
						}
						
						ASTNode new_param_type_node = emplace_node<TypeSpecifierNode>(
							new_param_type, param_type_spec.qualifier(),
							get_type_size_bits(new_param_type), Token(), param_type_spec.cv_qualifier());
						auto& new_param_spec = new_param_type_node.as<TypeSpecifierNode>();
						new_param_spec.set_type_index(new_param_type_index);
						for (const auto& pl : param_type_spec.pointer_levels())
							new_param_spec.add_pointer_level(pl.cv_qualifier);
						new_param_spec.set_reference_qualifier(param_type_spec.reference_qualifier());
						
						auto new_param_decl = emplace_node<DeclarationNode>(
							new_param_type_node, param_decl.identifier_token());
						// Copy default value if present
						if (param_decl.has_default_value()) {
							new_param_decl.as<DeclarationNode>().set_default_value(param_decl.default_value());
						}
						new_func_ref.add_parameter_node(new_param_decl);
					}
				}
				
				// Copy function specifiers
				new_func_ref.set_noexcept(func_decl.is_noexcept());
				new_func_ref.set_is_constexpr(func_decl.is_constexpr());
				new_func_ref.set_is_consteval(func_decl.is_consteval());
				new_func_ref.set_is_deleted(func_decl.is_deleted());
				new_func_ref.set_is_variadic(func_decl.is_variadic());
				new_func_ref.set_is_static(func_decl.is_static());
				if (func_decl.get_definition().has_value())
					new_func_ref.set_definition(*func_decl.get_definition());
				if (func_decl.has_template_body_position())
					new_func_ref.set_template_body_position(func_decl.template_body_position());
				// Copy trailing return type position for SFINAE resolution
				if (func_decl.has_trailing_return_type_position())
					new_func_ref.set_trailing_return_type_position(func_decl.trailing_return_type_position());
				
				// Create new TemplateFunctionDeclarationNode with inner template params
				auto new_template_func = emplace_node<TemplateFunctionDeclarationNode>(
					template_func.template_parameters(),
					new_func_node,
					template_func.requires_clause()
				);
				
				instantiated_struct_ref.add_member_function(new_template_func, mem_func.access);
				
				// Register with qualified name
				StringBuilder qualified_name_builder;
				qualified_name_builder.append(StringTable::getStringView(instantiated_name))
				                     .append("::")
				                     .append(decl_node.identifier_token().value());
				StringHandle qualified_name_handle = StringTable::getOrInternStringHandle(qualified_name_builder.commit());
				
				gTemplateRegistry.registerTemplate(qualified_name_handle, new_template_func);
				gTemplateRegistry.registerTemplate(decl_node.identifier_token().handle(), new_template_func);
				
				// Register outer template parameter bindings
				{
					OuterTemplateBinding outer_binding;
					for (const auto& tp : template_params) {
						if (tp.is<TemplateParameterNode>()) {
							outer_binding.param_names.push_back(tp.as<TemplateParameterNode>().nameHandle());
						}
					}
					outer_binding.param_args = template_args_to_use;
					gTemplateRegistry.registerOuterTemplateBinding(qualified_name_handle, std::move(outer_binding));
					FLASH_LOG(Templates, Debug, "Registered outer template bindings for ", StringTable::getStringView(qualified_name_handle));
				}
			} else {
				// No substitution needed - copy as-is
				instantiated_struct_ref.add_member_function(
					mem_func.function_declaration,
					mem_func.access
				);
				
				// Register with qualified name
				StringBuilder qualified_name_builder;
				qualified_name_builder.append(StringTable::getStringView(instantiated_name))
				                     .append("::")
				                     .append(decl_node.identifier_token().value());
				StringHandle qualified_name_handle = StringTable::getOrInternStringHandle(qualified_name_builder.commit());
				
				gTemplateRegistry.registerTemplate(qualified_name_handle, mem_func.function_declaration);
				gTemplateRegistry.registerTemplate(decl_node.identifier_token().handle(), mem_func.function_declaration);
				
				// Register outer template parameter bindings
				{
					OuterTemplateBinding outer_binding;
					for (const auto& tp : template_params) {
						if (tp.is<TemplateParameterNode>()) {
							outer_binding.param_names.push_back(tp.as<TemplateParameterNode>().nameHandle());
						}
					}
					outer_binding.param_args = template_args_to_use;
					gTemplateRegistry.registerOuterTemplateBinding(qualified_name_handle, std::move(outer_binding));
					FLASH_LOG(Templates, Debug, "Registered outer template bindings for ", StringTable::getStringView(qualified_name_handle));
				}
			}
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
		// Check if this is a nested template (member function template of a class template)
		// Pattern: template<typename T> template<typename U> T Container<T>::convert(U u) { ... }
		if (!out_of_line_member.inner_template_params.empty()) {
			// This is a nested template out-of-line definition
			// Find the matching TemplateFunctionDeclarationNode in the instantiated struct
			// and set the body_start on its inner FunctionDeclarationNode
			const FunctionDeclarationNode& ool_func = out_of_line_member.function_node.as<FunctionDeclarationNode>();
			const DeclarationNode& ool_decl = ool_func.decl_node();
			std::string_view ool_func_name = ool_decl.identifier_token().value();

			FLASH_LOG(Templates, Debug, "Processing nested template out-of-line member: ", ool_func_name);

			bool found = false;
			for (auto& mem_func : instantiated_struct_ref.member_functions()) {
				if (mem_func.function_declaration.is<TemplateFunctionDeclarationNode>()) {
					auto& inst_template_func = mem_func.function_declaration.as<TemplateFunctionDeclarationNode>();
					auto& inst_func_decl = inst_template_func.function_decl_node();
					if (inst_func_decl.decl_node().identifier_token().value() == ool_func_name) {
						// Set the body position from the out-of-line definition
						inst_func_decl.set_template_body_position(out_of_line_member.body_start);
						FLASH_LOG(Templates, Debug, "Set body position on nested template member: ", ool_func_name);
						found = true;
						break;
					}
				}
			}

			if (!found) {
				FLASH_LOG(Templates, Warning, "Nested template out-of-line member '", ool_func_name,
				          "' not found in instantiated struct");
			}
			continue;
		}

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
					false,  // is_const
					type_spec.reference_qualifier(),
					static_cast<int>(type_spec.pointer_depth())
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
						static_member.is_const,
						static_member.reference_qualifier,
						static_member.pointer_depth
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
	
	// Register in cache for O(1) lookup on future instantiations
	gTemplateRegistry.registerInstantiation(cache_key, instantiated_struct);
	
	// Return the instantiated struct node for code generation
	return instantiated_struct;
}

// Try to instantiate a member function template during a member function call
// This is called when parsing obj.method(args) where method is a template
