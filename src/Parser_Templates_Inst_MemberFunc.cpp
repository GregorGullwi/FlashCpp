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
	
	// If not found and struct_name looks like an instantiated template (e.g., has_foo$a1b2c3),
	// try the base template class name (e.g., has_foo::method)
	if (!template_opt.has_value()) {
		std::string_view base_name = extractBaseTemplateName(struct_name);
		if (!base_name.empty()) {
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
				template_args.push_back(TemplateArgument::makeType(arg_types[arg_index].type(), arg_types[arg_index].type_index()));
				arg_index++;
			} else {
				// Not enough arguments - use first argument type
				template_args.push_back(TemplateArgument::makeType(arg_types[0].type(), arg_types[0].type_index()));
			}
		} else {
			// Non-type parameter - not yet supported
			return std::nullopt;
		}
	}

	// Check if we already have this instantiation
	auto key = FlashCpp::makeInstantiationKey(qualified_name, template_args);

	auto existing_inst = gTemplateRegistry.getInstantiation(key);
	if (existing_inst.has_value()) {
		return *existing_inst;  // Return existing instantiation
	}

	return instantiate_member_function_template_core(
		struct_name, member_name, qualified_name, template_node, template_args, key);
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
		ASTNode& spec_node = *specialization_opt;
		if (spec_node.is<FunctionDeclarationNode>()) {
			FunctionDeclarationNode& spec_func = spec_node.as<FunctionDeclarationNode>();
			
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
						struct_node_ptr = &struct_symbol_opt->as<StructDeclarationNode>();
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
	
	// Look up ALL template overloads in the registry for SFINAE support
	const std::vector<ASTNode>* all_templates = gTemplateRegistry.lookupAllTemplates(qualified_name.view());
	
	// If not found and struct_name looks like an instantiated template (e.g., has_foo$a1b2c3),
	// try the base template class name (e.g., has_foo::method)
	if (!all_templates || all_templates->empty()) {
		std::string_view base_class_name = extractBaseTemplateName(struct_name);
		if (!base_class_name.empty()) {
			StringBuilder base_qualified_name_sb;
			base_qualified_name_sb.append(base_class_name).append("::").append(member_name);
			StringHandle base_qualified_name = StringTable::getOrInternStringHandle(base_qualified_name_sb);
			all_templates = gTemplateRegistry.lookupAllTemplates(base_qualified_name.view());
			FLASH_LOG(Templates, Debug, "Trying base template class lookup: ", base_qualified_name.view());
		}
	}
	
	if (!all_templates || all_templates->empty()) {
		return std::nullopt;  // Not a template
	}

	// Loop over all overloads for SFINAE support
	for (const auto& template_node : *all_templates) {
		if (!template_node.is<TemplateFunctionDeclarationNode>()) {
			continue;  // Not a function template
		}

		const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
		const std::vector<ASTNode>& template_params = template_func.template_parameters();
		const FunctionDeclarationNode& func_decl = template_func.function_decl_node();

		// Convert TemplateTypeArg to TemplateArgument (preserving type_index for struct types)
		std::vector<TemplateArgument> template_args;
		for (const auto& type_arg : template_type_args) {
			template_args.push_back(toTemplateArgument(type_arg));
		}

		// Check if we already have this instantiation
		auto key = FlashCpp::makeInstantiationKey(qualified_name, template_args);

		auto existing_inst = gTemplateRegistry.getInstantiation(key);
		if (existing_inst.has_value()) {
			return *existing_inst;  // Return existing instantiation
		}

		// SFINAE for trailing return type: always re-parse when trailing position is available
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
			// Add inner template params (the member function template's own params, e.g. U)
			for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				Type concrete_type = template_args[i].type_value;
				auto& type_info = gTypeInfo.emplace_back(
					StringTable::getOrInternStringHandle(tparam.name()),
					concrete_type, gTypeInfo.size(),
					getTypeSizeFromTemplateArgument(template_args[i]));
				gTypesByName.emplace(type_info.name(), &type_info);
				sfinae_scope.addParameter(&type_info);
				sfinae_type_map_[type_info.name()] = template_args[i].type_index;
			}
			// Add outer template params (from enclosing class template, e.g. T→int)
			const OuterTemplateBinding* outer_binding = gTemplateRegistry.getOuterTemplateBinding(qualified_name.view());
			if (outer_binding) {
				for (size_t i = 0; i < outer_binding->param_names.size() && i < outer_binding->param_args.size(); ++i) {
					std::string_view outer_param_name = StringTable::getStringView(outer_binding->param_names[i]);
					Type outer_concrete_type = outer_binding->param_args[i].base_type;
					uint32_t outer_size = (outer_binding->param_args[i].type_index != 0 && outer_binding->param_args[i].type_index < gTypeInfo.size())
						? gTypeInfo[outer_binding->param_args[i].type_index].type_size_
						: get_type_size_bits(outer_concrete_type);
					auto& outer_type_info = gTypeInfo.emplace_back(
						StringTable::getOrInternStringHandle(outer_param_name), outer_concrete_type, gTypeInfo.size(), outer_size);
					gTypesByName.emplace(outer_type_info.name(), &outer_type_info);
					sfinae_scope.addParameter(&outer_type_info);
					sfinae_type_map_[outer_type_info.name()] = outer_binding->param_args[i].type_index;
				}
			}

			auto return_type_result = parse_type_specifier();
			gSymbolTable.exit_scope();
			restore_lexer_position_only(sfinae_pos);
			in_sfinae_context_ = prev_sfinae_context;
			parsing_template_body_ = prev_parsing_template_body;
			current_template_param_names_ = std::move(prev_template_param_names);
			sfinae_type_map_ = std::move(prev_sfinae_type_map);

			if (return_type_result.is_error() || !return_type_result.node().has_value()) {
				continue;  // SFINAE: this overload's return type failed, try next
			}
		}

		auto result = instantiate_member_function_template_core(
			struct_name, member_name, qualified_name, template_node, template_args, key);
		if (result.has_value()) {
			return result;
		}
	}

	return std::nullopt;
}

std::optional<ASTNode> Parser::instantiate_member_function_template_core(
	std::string_view struct_name, std::string_view member_name,
	StringHandle qualified_name,
	const ASTNode& template_node,
	const std::vector<TemplateArgument>& template_args,
	const FlashCpp::TemplateInstantiationKey& key) {

	const TemplateFunctionDeclarationNode& template_func = template_node.as<TemplateFunctionDeclarationNode>();
	const std::vector<ASTNode>& template_params = template_func.template_parameters();
	const FunctionDeclarationNode& func_decl = template_func.function_decl_node();
	const OuterTemplateBinding* outer_binding = gTemplateRegistry.getOuterTemplateBinding(qualified_name.view());

	// Generate mangled name for the instantiation
	std::string_view mangled_name = gTemplateRegistry.mangleTemplateName(member_name, template_args);

	// Get the original function's declaration
	const DeclarationNode& orig_decl = func_decl.decl_node();

	// Helper lambda to resolve a UserDefined type against both inner and outer template params
	// Also tracks which inner template parameter index corresponds to each auto parameter
	// so that we know which template argument supplies the concrete type for each auto param.
	size_t auto_param_index = 0;
	auto resolve_template_type = [&](Type type, TypeIndex type_index) -> std::pair<Type, TypeIndex> {
		if (type == Type::Auto) {
			// Abbreviated function template parameter (concept auto / auto):
			// Map this to the corresponding inner template parameter's argument type.
			// Inner template params for auto are named _T0, _T1, etc.
			if (auto_param_index < template_args.size()) {
				const auto& arg = template_args[auto_param_index];
				auto_param_index++;
				return { arg.type_value, arg.type_index };
			}
			return { type, type_index };
		}
		if (type == Type::UserDefined && type_index < gTypeInfo.size()) {
			const TypeInfo& ti = gTypeInfo[type_index];
			std::string_view tn = StringTable::getStringView(ti.name());

			// Check inner template params first
			for (size_t i = 0; i < template_params.size(); ++i) {
				const TemplateParameterNode& tparam = template_params[i].as<TemplateParameterNode>();
				if (tparam.name() == tn && i < template_args.size()) {
					return { template_args[i].type_value, template_args[i].type_index };
				}
			}
			// Check outer template params (e.g., T→int from class template)
			if (outer_binding) {
				for (size_t i = 0; i < outer_binding->param_names.size() && i < outer_binding->param_args.size(); ++i) {
					if (StringTable::getStringView(outer_binding->param_names[i]) == tn) {
						const auto& arg = outer_binding->param_args[i];
						return { arg.base_type, arg.type_index };
					}
				}
			}
		}
		return { type, type_index };
	};

	// Substitute the return type if it's a template parameter
	const TypeSpecifierNode& return_type_spec = orig_decl.type_node().as<TypeSpecifierNode>();
	auto [return_type, return_type_index] = resolve_template_type(return_type_spec.type(), return_type_spec.type_index());

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
	
	// Copy pointer levels and set type_index from the resolved type
	auto& substituted_return_type_spec = substituted_return_type.as<TypeSpecifierNode>();
	substituted_return_type_spec.set_type_index(return_type_index);
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

			auto [param_type, param_type_index] = resolve_template_type(param_type_spec.type(), param_type_spec.type_index());

			// Create the substituted parameter type specifier
			auto substituted_param_type = emplace_node<TypeSpecifierNode>(
				param_type,
				TypeQualifier::None,
				get_type_size_bits(param_type),
				Token()
			);
			
			// Copy pointer levels and set type_index from the resolved type
			auto& substituted_param_type_spec = substituted_param_type.as<TypeSpecifierNode>();
			substituted_param_type_spec.set_type_index(param_type_index);
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
		compute_and_set_mangled_name(new_func_ref);
		ast_nodes_.push_back(new_func_node);
		gTemplateRegistry.registerInstantiation(key, new_func_node);
		return new_func_node;
	}

	// Temporarily add the concrete types to the type system with template parameter names
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
		template_scope.addParameter(&type_info);
	}

	// Also add outer template parameter bindings (e.g., T→int from class template)
	if (outer_binding) {
		for (size_t i = 0; i < outer_binding->param_names.size() && i < outer_binding->param_args.size(); ++i) {
			std::string_view outer_param_name = StringTable::getStringView(outer_binding->param_names[i]);
			Type outer_concrete_type = outer_binding->param_args[i].base_type;
			uint32_t outer_size = (outer_binding->param_args[i].type_index != 0 && outer_binding->param_args[i].type_index < gTypeInfo.size())
				? gTypeInfo[outer_binding->param_args[i].type_index].type_size_
				: get_type_size_bits(outer_concrete_type);
			auto& outer_type_info = gTypeInfo.emplace_back(
				StringTable::getOrInternStringHandle(outer_param_name), outer_concrete_type, gTypeInfo.size(), outer_size);
			gTypesByName.emplace(outer_type_info.name(), &outer_type_info);
			template_scope.addParameter(&outer_type_info);
		}
		FLASH_LOG(Templates, Debug, "Added ", outer_binding->param_names.size(), " outer template param bindings for body parsing");
	}

	// Save current position
	SaveHandle current_pos = save_token_position();

	// Restore to the function body start (lexer only - keep AST nodes from previous instantiations)
	restore_lexer_position_only(func_decl.template_body_position());

	// Look up the struct type info
	auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name));
	if (struct_type_it == gTypesByName.end()) {
		FLASH_LOG(Templates, Debug, "Struct type not found: ", struct_name);
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

	// Push class template pack info so sizeof...() from the enclosing class template
	// can be resolved during member function template body parsing.
	// E.g., sizeof...(_Elements) inside a member function template of tuple<int, float>.
	ClassTemplatePackGuard member_pack_guard(class_template_pack_stack_);
	{
		auto pack_it = class_template_pack_registry_.find(StringTable::getOrInternStringHandle(struct_name));
		if (pack_it != class_template_pack_registry_.end()) {
			member_pack_guard.push(pack_it->second);
		}
	}

	// Parse the function body
	auto block_result = parse_block();
	if (!block_result.is_error() && block_result.node().has_value()) {
		// Substitute template parameters in the body (handles sizeof..., fold expressions, etc.)
		ASTNode substituted_body = substituteTemplateParameters(
			*block_result.node(),
			template_params,
			template_args
		);
		new_func_ref.set_definition(substituted_body);
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
	saved_tokens_[current_pos].ast_nodes_size_ = ast_nodes_.size();

	// Compute and set the proper mangled name (Itanium/MSVC) for code generation
	compute_and_set_mangled_name(new_func_ref);
	
	// Register the instantiation
	gTemplateRegistry.registerInstantiation(key, new_func_node);

	return new_func_node;
}

// Instantiate a lazy member function on-demand
// This performs the template parameter substitution that was deferred during lazy registration
