std::string_view Parser::get_instantiated_class_name(std::string_view template_name, const std::vector<TemplateTypeArg>& template_args) {
	if (size_t last_colon = template_name.rfind("::"); last_colon != std::string_view::npos) {
		template_name = template_name.substr(last_colon + 2);
	}
	auto result = FlashCpp::generateInstantiatedNameFromArgs(template_name, template_args);
	return result;
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
				new_type.set_reference_qualifier(arg.ref_qualifier);
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
							new_type.set_reference_qualifier(arg.ref_qualifier);
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
				new_type.set_reference_qualifier(arg.ref_qualifier);
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
	
	// Build resolved args list — apply template_param_substitutions_ to all args once
	// Do this BEFORE the dependency check so that dependent args that have substitutions
	// available (e.g., _R1 -> ratio<1,2>) get resolved first.
	std::vector<TemplateTypeArg> resolved_args;
	resolved_args.reserve(template_args.size());
	for (const auto& original_arg : template_args) {
		TemplateTypeArg arg = original_arg;
		if (arg.is_dependent && arg.dependent_name.isValid()) {
			// Try to resolve dependent arg using template_param_substitutions_
			std::string_view dep_name = arg.dependent_name.view();
			for (const auto& subst : template_param_substitutions_) {
				if (subst.is_type_param && subst.param_name == dep_name && !subst.substituted_type.is_dependent) {
					FLASH_LOG(Templates, Debug, "Resolving dependent template parameter '", dep_name, 
					          "' with concrete type ", subst.substituted_type.toString());
					arg = subst.substituted_type;
					break;
				}
			}
		}
		if (!arg.is_dependent && (arg.base_type == Type::UserDefined || arg.base_type == Type::Struct) && 
		    arg.type_index < gTypeInfo.size()) {
			std::string_view type_name = StringTable::getStringView(gTypeInfo[arg.type_index].name());
			for (const auto& subst : template_param_substitutions_) {
				if (subst.is_type_param && subst.param_name == type_name) {
					FLASH_LOG(Templates, Debug, "Substituting template parameter '", type_name, 
					          "' with concrete type ", subst.substituted_type.toString());
					arg = subst.substituted_type;
					break;
				}
			}
		}
		resolved_args.push_back(arg);
	}
	
	// Check if any template argument is still dependent after substitution
	// If so, we cannot instantiate - this happens when we're inside a template body
	for (size_t i = 0; i < resolved_args.size(); ++i) {
		const auto& arg = resolved_args[i];
		if (arg.is_dependent) {
			FLASH_LOG(Templates, Debug, "Skipping variable template '", template_name, 
			          "' instantiation - arg[", i, "] is dependent: ", arg.toString());
			return std::nullopt;
		}
	}
	
	// Structural pattern matching: find the best matching partial specialization
	// Uses TemplatePattern::matches() which handles qualifier matching, multi-arg,
	// and proper template parameter deduction without string-based pattern keys.
	auto structural_match = gTemplateRegistry.findVariableTemplateSpecialization(simple_template_name, resolved_args);
	// Also try qualified name if simple name didn't match
	if (!structural_match.has_value() && template_name != simple_template_name) {
		structural_match = gTemplateRegistry.findVariableTemplateSpecialization(template_name, resolved_args);
	}
	
	if (structural_match.has_value() && structural_match->node.is<TemplateVariableDeclarationNode>()) {
		FLASH_LOG(Templates, Debug, "Found variable template partial specialization via structural match");
		const TemplateVariableDeclarationNode& spec_template = structural_match->node.as<TemplateVariableDeclarationNode>();
		const VariableDeclarationNode& spec_var_decl = spec_template.variable_decl_node();
		const Token& orig_token = spec_var_decl.declaration().identifier_token();
		std::string_view persistent_name = FlashCpp::generateInstantiatedNameFromArgs(simple_template_name, template_args);
		
		if (gSymbolTable.lookup(persistent_name).has_value()) {
			return gSymbolTable.lookup(persistent_name);
		}
		
		const DeclarationNode& spec_decl = spec_var_decl.declaration();
		ASTNode spec_type = spec_decl.type_node();
		
		std::optional<ASTNode> init_expr;
		if (spec_var_decl.initializer().has_value()) {
			const auto& spec_params = spec_template.template_parameters();
			if (!spec_params.empty()) {
				// Build deduced args from the structural match substitutions.
				// TemplatePattern::matches() already deduced T→int by stripping
				// pattern qualifiers, so we use those substitutions directly.
				std::vector<TemplateArgument> converted_args;
				converted_args.reserve(spec_params.size());
				for (const auto& param : spec_params) {
					if (param.is<TemplateParameterNode>()) {
						const TemplateParameterNode& tp = param.as<TemplateParameterNode>();
						auto it = structural_match->substitutions.find(tp.nameHandle());
						if (it != structural_match->substitutions.end()) {
							converted_args.push_back(toTemplateArgument(it->second));
						} else {
							// Fallback: use resolved arg with qualifiers stripped
							if (converted_args.size() < resolved_args.size()) {
								FLASH_LOG(Templates, Debug, "Deduction fallback for param '",
								          tp.name(), "': using arg[", converted_args.size(), "] with qualifiers stripped");
								TemplateTypeArg deduced = resolved_args[converted_args.size()];
								deduced.ref_qualifier = ReferenceQualifier::None;
								deduced.pointer_depth = 0;
								deduced.pointer_cv_qualifiers.clear();
								deduced.is_array = false;
								converted_args.push_back(toTemplateArgument(deduced));
							} else {
								FLASH_LOG(Templates, Warning, "Cannot deduce param '",
								          tp.name(), "': no substitution and no remaining args");
							}
						}
					}
				}
				init_expr = substituteTemplateParameters(
					*spec_var_decl.initializer(), spec_params, converted_args);
				spec_type = substituteTemplateParameters(
					spec_type, spec_params, converted_args);
			} else {
				init_expr = *spec_var_decl.initializer();
			}
		} else if (spec_decl.type_node().is<TypeSpecifierNode>() &&
		           spec_decl.type_node().as<TypeSpecifierNode>().type() == Type::Bool) {
			Token true_token(Token::Type::Keyword, "true"sv, orig_token.line(), orig_token.column(), orig_token.file_index());
			init_expr = emplace_node<ExpressionNode>(BoolLiteralNode(true_token, true));
		}
		
		auto decl_node = emplace_node<DeclarationNode>(spec_type,
			Token(Token::Type::Identifier, persistent_name, orig_token.line(), orig_token.column(), orig_token.file_index()));
		
		auto var_decl_node = emplace_node<VariableDeclarationNode>(decl_node, init_expr, StorageClass::None);
		var_decl_node.as<VariableDeclarationNode>().set_is_constexpr(true);
		gSymbolTable.insertGlobal(persistent_name, var_decl_node);
		ast_nodes_.insert(ast_nodes_.begin(), var_decl_node);
		return var_decl_node;
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
				substituted_type.set_reference_qualifier(arg.ref_qualifier);
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
						
						// The struct name might be a mangled template instantiation (hash-based)
						// Extract the base template name from metadata
						std::string_view template_name_to_lookup = struct_name_view;
						std::string_view base_name = extractBaseTemplateName(struct_name_view);
						if (!base_name.empty()) {
							template_name_to_lookup = base_name;
							FLASH_LOG(Templates, Debug, "Phase 3: Extracted template name: '", template_name_to_lookup, "'");
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
	ASTNode& spec_node
) {
	// Generate the instantiated class name
	std::string_view instantiated_name = get_instantiated_class_name(template_name, template_args);
	FLASH_LOG(Templates, Debug, "instantiate_full_specialization called for: ", instantiated_name);
	
	if (!spec_node.is<StructDeclarationNode>()) {
		FLASH_LOG(Templates, Error, "Full specialization is not a StructDeclarationNode");
		return std::nullopt;
	}
	
	StructDeclarationNode& spec_struct = spec_node.as<StructDeclarationNode>();
	
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
		QualifiedIdentifier::fromQualifiedName(template_name, gSymbolTable.get_current_namespace_handle()),
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
			type_spec.reference_qualifier(),
			type_spec.reference_qualifier() != ReferenceQualifier::None ? get_type_size_bits(member_type) : 0,
			false,
			{},
			static_cast<int>(type_spec.pointer_depth()),
			member_decl.bitfield_width
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
	for (auto& mem_func : spec_struct.member_functions()) {
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
			FunctionDeclarationNode& orig_func = mem_func.function_declaration.as<FunctionDeclarationNode>();
			
			// Create a NEW FunctionDeclarationNode with the instantiated struct name
			auto new_func_node = emplace_node<FunctionDeclarationNode>(
				orig_func.decl_node(),
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
