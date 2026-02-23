#include "ExpressionSubstitutor.h"
#include "Parser.h"
#include "TemplateInstantiationHelper.h"
#include "Log.h"

ASTNode ExpressionSubstitutor::substitute(const ASTNode& expr) {
	if (!expr.has_value()) {
		return expr;
	}

	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor::substitute: checking node type: ", expr.type_name());

	// Check if this is an ExpressionNode variant
	if (expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_variant = expr.as<ExpressionNode>();
		
		// Use std::visit to dispatch to the correct handler based on variant type
		auto& substitutor = *this;
		return std::visit([&substitutor](auto&& node) -> ASTNode {
			using T = std::decay_t<decltype(node)>;
			
			FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing variant type");
			
			if constexpr (std::is_same_v<T, ConstructorCallNode>) {
				FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Dispatching to substituteConstructorCall");
				return substitutor.substituteConstructorCall(node);
			} else if constexpr (std::is_same_v<T, FunctionCallNode>) {
				FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Dispatching to substituteFunctionCall");
				return substitutor.substituteFunctionCall(node);
			} else if constexpr (std::is_same_v<T, BinaryOperatorNode>) {
				return substitutor.substituteBinaryOp(node);
			} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
				return substitutor.substituteUnaryOp(node);
			} else if constexpr (std::is_same_v<T, IdentifierNode>) {
				return substitutor.substituteIdentifier(node);
			} else if constexpr (std::is_same_v<T, QualifiedIdentifierNode>) {
				return substitutor.substituteQualifiedIdentifier(node);
			} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
				return substitutor.substituteMemberAccess(node);
			} else if constexpr (std::is_same_v<T, SizeofExprNode>) {
				FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Dispatching to substituteSizeofExpr");
				return substitutor.substituteSizeofExpr(node);
			} else if constexpr (std::is_same_v<T, NumericLiteralNode> ||
			                     std::is_same_v<T, BoolLiteralNode> || 
			                     std::is_same_v<T, StringLiteralNode>) {
				// Literals don't need substitution - return as ASTNode
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(node);
				return ASTNode(&new_expr);
			} else {
				// For other types, return as-is wrapped in ExpressionNode
				FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Unhandled expression variant type, returning as-is");
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(node);
				return ASTNode(&new_expr);
			}
		}, expr_variant);
	}

	// Handle direct node types (not wrapped in variant)
	if (expr.is<ConstructorCallNode>()) {
		return substituteConstructorCall(expr.as<ConstructorCallNode>());
	}
	else if (expr.is<FunctionCallNode>()) {
		return substituteFunctionCall(expr.as<FunctionCallNode>());
	}
	else if (expr.is<BinaryOperatorNode>()) {
		return substituteBinaryOp(expr.as<BinaryOperatorNode>());
	}
	else if (expr.is<UnaryOperatorNode>()) {
		return substituteUnaryOp(expr.as<UnaryOperatorNode>());
	}
	else if (expr.is<IdentifierNode>()) {
		return substituteIdentifier(expr.as<IdentifierNode>());
	}
	else if (expr.is<SizeofExprNode>()) {
		return substituteSizeofExpr(expr.as<SizeofExprNode>());
	}
	else if (expr.is<NumericLiteralNode>()) {
		// Literals don't need substitution
		return expr;
	}
	else if (expr.is<BoolLiteralNode>()) {
		return expr;
	}
	else if (expr.is<StringLiteralNode>()) {
		return expr;
	}
	else {
		// For any other node type, return as-is
		FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Unknown expression type: ", expr.type_name());
		return expr;
	}
}

ASTNode ExpressionSubstitutor::substituteConstructorCall(const ConstructorCallNode& ctor) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing constructor call");
	
	// Get the type being constructed
	const ASTNode& type_node = ctor.type_node();
	
	if (!type_node.is<TypeSpecifierNode>()) {
		FLASH_LOG(Templates, Warning, "ExpressionSubstitutor: Constructor type node is not TypeSpecifierNode");
		// Return wrapped in ExpressionNode
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(ctor);
		return ASTNode(&new_expr);
	}
	
	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	
	// Substitute template parameters in the type
	TypeSpecifierNode substituted_type = substituteInType(type_spec);
	
	// Create new ConstructorCallNode with substituted type
	// First, we need to copy the arguments
	ChunkedVector<ASTNode> substituted_args;
	for (size_t i = 0; i < ctor.arguments().size(); ++i) {
		substituted_args.push_back(substitute(ctor.arguments()[i]));
	}
	
	// Create new TypeSpecifierNode and ConstructorCallNode
	TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(substituted_type);
	ConstructorCallNode& new_ctor = gChunkedAnyStorage.emplace_back<ConstructorCallNode>(
		ASTNode(&new_type),
		std::move(substituted_args),
		ctor.called_from()
	);
	
	// Wrap in ExpressionNode
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_ctor);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteFunctionCall(const FunctionCallNode& call) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing function call");
	FLASH_LOG(Templates, Debug, "  has_mangled_name: ", call.has_mangled_name());
	FLASH_LOG(Templates, Debug, "  has_template_arguments: ", call.has_template_arguments());
	
	const DeclarationNode& decl_node = call.function_declaration();
	std::string_view func_name = call.called_from().value();
	if (func_name.empty()) {
		func_name = decl_node.identifier_token().value();
	}
	std::vector<ASTNode> explicit_template_arg_nodes;
	
	// Check if this function call has explicit template arguments (e.g., base_trait<T>())
	if (call.has_template_arguments()) {
		explicit_template_arg_nodes = call.template_arguments();
		FLASH_LOG(Templates, Debug, "  Found ", explicit_template_arg_nodes.size(), " template argument nodes");
		
		FLASH_LOG(Templates, Debug, "  Function name: ", func_name);
		
		// Check if any arguments are pack expansions
		bool has_pack_expansion = false;
		for (const ASTNode& arg_node : explicit_template_arg_nodes) {
			std::string_view pack_name;
			if (isPackExpansion(arg_node, pack_name)) {
				has_pack_expansion = true;
				break;
			}
		}
		
		std::vector<TemplateTypeArg> substituted_template_args;
		
		if (has_pack_expansion) {
			// Use pack expansion logic
			FLASH_LOG(Templates, Debug, "  Template arguments contain pack expansion, expanding...");
			substituted_template_args = expandPacksInArguments(explicit_template_arg_nodes);
			FLASH_LOG(Templates, Debug, "  After pack expansion: ", substituted_template_args.size(), " arguments");
		} else {
			// Original logic for non-pack arguments
			// Substitute template parameters in the template arguments
			for (const ASTNode& arg_node : explicit_template_arg_nodes) {
				FLASH_LOG(Templates, Debug, "  Checking template argument node, has_value: ", arg_node.has_value(), " type: ", arg_node.type_name());
			
			// Template arguments can be stored as TypeSpecifierNode for type arguments
			if (arg_node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& type_spec = arg_node.as<TypeSpecifierNode>();
				FLASH_LOG(Templates, Debug, "    Template argument is TypeSpecifierNode: type=", (int)type_spec.type(), " type_index=", type_spec.type_index());
				
				// First, check if type_index points to a template parameter in gTypeInfo
				std::string_view type_name = "";
				if (type_spec.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					type_name = StringTable::getStringView(type_info.name());
					FLASH_LOG(Templates, Debug, "    Type name from gTypeInfo: ", type_name);
				}
				
				// Check if this type_name is in our substitution map (indicating it's a template parameter)
				auto it = param_map_.find(type_name);
				if (it != param_map_.end()) {
					// This is a template parameter - substitute it
					FLASH_LOG(Templates, Debug, "    Substituting template parameter: ", type_name, " -> type_index=", it->second.type_index);
					substituted_template_args.push_back(it->second);
				}
				// Check if this is a template parameter type (Type::Template)
				else if (type_spec.type() == Type::Template) {
					// This is a template parameter - we need to substitute it
					// The type_index should point to a template parameter
					FLASH_LOG(Templates, Debug, "    Type is Template, looking up in substitution map");
					
					// For template parameters, we need to look them up by name
					// The type_spec.type_index() should tell us which parameter it is
					// But we need the name to do the substitution
					// Let's check if we can find it in gTypeInfo
					if (type_spec.type_index() < gTypeInfo.size()) {
						const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
						std::string_view param_name = StringTable::getStringView(type_info.name());
						FLASH_LOG(Templates, Debug, "    Template parameter name: ", param_name);
						
						auto it2 = param_map_.find(param_name);
						if (it2 != param_map_.end()) {
							FLASH_LOG(Templates, Debug, "    Substituting: ", param_name, " -> type_index=", it2->second.type_index);
							substituted_template_args.push_back(it2->second);
						} else {
							FLASH_LOG(Templates, Warning, "    Template parameter not found in substitution map: ", param_name);
							// Return as-is if we can't substitute
							ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(call);
							return ASTNode(&new_expr);
						}
					}
				} else {
					// Non-template type argument - use it directly
					FLASH_LOG(Templates, Debug, "    Template argument is concrete type, using directly");
					TemplateTypeArg arg(type_spec);
					substituted_template_args.push_back(arg);
				}
			}
			// Check if this is a TemplateParameterReferenceNode that needs substitution
			else if (arg_node.is<ExpressionNode>()) {
				const ExpressionNode& expr_variant = arg_node.as<ExpressionNode>();
				bool is_template_param_ref = std::visit([](const auto& inner) -> bool {
					using T = std::decay_t<decltype(inner)>;
					return std::is_same_v<T, TemplateParameterReferenceNode>;
				}, expr_variant);
				
				if (is_template_param_ref) {
					// This is a template parameter reference - substitute it
					const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr_variant);
					std::string_view param_name = tparam_ref.param_name().view();
					FLASH_LOG(Templates, Debug, "    Template argument is parameter reference: ", param_name);
					
					auto it = param_map_.find(param_name);
					if (it != param_map_.end()) {
						FLASH_LOG(Templates, Debug, "    Substituting: ", param_name, " -> type_index=", it->second.type_index);
						substituted_template_args.push_back(it->second);
					} else {
						FLASH_LOG(Templates, Warning, "    Template parameter not found in substitution map: ", param_name);
						// Return as-is if we can't substitute
						ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(call);
						return ASTNode(&new_expr);
					}
				} else {
					// Non-dependent template argument - evaluate to get the type
					FLASH_LOG(Templates, Debug, "    Template argument is non-dependent expression");
					// For now, skip - we'll handle this later if needed
				}
			} else {
				FLASH_LOG(Templates, Debug, "    Template argument is unknown type");
			}
		}
		} // End of else block for non-pack arguments
		
		// If no arguments were collected but we have pack substitutions available, use them
		if (substituted_template_args.empty() && !pack_map_.empty() && !call.has_template_arguments()) {
			FLASH_LOG(Templates, Debug, "  Using pack substitution map to recover template arguments for function call");
			for (const auto& pack_entry : pack_map_) {
				substituted_template_args.insert(substituted_template_args.end(), pack_entry.second.begin(), pack_entry.second.end());
				break; // Use the first available pack substitution
			}
		}

		// Now we have substituted template arguments - instantiate the template
		if (!substituted_template_args.empty()) {
			FLASH_LOG(Templates, Debug, "  Attempting to instantiate template: ", func_name, " with ", substituted_template_args.size(), " arguments");

			// First try function template instantiation to obtain accurate return type
			if (auto instantiated_template = parser_.try_instantiate_template_explicit(func_name, substituted_template_args)) {
				if (instantiated_template->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = instantiated_template->as<FunctionDeclarationNode>();

					ChunkedVector<ASTNode> substituted_args_nodes;
					for (size_t i = 0; i < call.arguments().size(); ++i) {
						substituted_args_nodes.push_back(substitute(call.arguments()[i]));
					}

					FunctionCallNode& new_call = gChunkedAnyStorage.emplace_back<FunctionCallNode>(
						const_cast<DeclarationNode&>(func_decl.decl_node()),
						std::move(substituted_args_nodes),
						call.called_from()
					);
					std::vector<ASTNode> substituted_template_arg_nodes;
					substituted_template_arg_nodes.reserve(explicit_template_arg_nodes.size());
					for (const auto& arg_node : explicit_template_arg_nodes) {
						substituted_template_arg_nodes.push_back(substitute(arg_node));
					}
					new_call.set_template_arguments(std::move(substituted_template_arg_nodes));

					ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_call);
					return ASTNode(&new_expr);
				}
			}
			
			// Try variable template instantiation before class template
			auto var_template_node = parser_.try_instantiate_variable_template(func_name, substituted_template_args);
			if (var_template_node.has_value()) {
				FLASH_LOG(Templates, Debug, "  Successfully instantiated variable template: ", func_name);
				// Variable template instantiation returns the variable declaration node
				// We want to return the initializer expression
				if (var_template_node->is<VariableDeclarationNode>()) {
					const VariableDeclarationNode& var_decl = var_template_node->as<VariableDeclarationNode>();
					if (var_decl.initializer().has_value()) {
						return var_decl.initializer().value();
					}
				}
				// If not a variable declaration or no initializer, return as-is
				return *var_template_node;
			}
			
			auto instantiated_node = parser_.try_instantiate_class_template(func_name, substituted_template_args, true);
			if (instantiated_node.has_value() && instantiated_node->is<StructDeclarationNode>()) {
				const StructDeclarationNode& class_decl = instantiated_node->as<StructDeclarationNode>();
				StringHandle instantiated_name = class_decl.name();
				
				// Look up the type index for the instantiated template
				auto type_it = gTypesByName.find(instantiated_name);
				if (type_it != gTypesByName.end()) {
					TypeIndex new_type_index = type_it->second->type_index_;
					
					FLASH_LOG(Templates, Debug, "  Successfully instantiated template with type_index=", new_type_index);
					
					// Create a TypeSpecifierNode for the instantiated type
					TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
						Type::Struct, new_type_index, 64, Token{}, CVQualifier::None
					);
					
					// Create a ConstructorCallNode instead of FunctionCallNode
					ChunkedVector<ASTNode> substituted_args_nodes;
					for (size_t i = 0; i < call.arguments().size(); ++i) {
						substituted_args_nodes.push_back(substitute(call.arguments()[i]));
					}
					
					ConstructorCallNode& new_ctor = gChunkedAnyStorage.emplace_back<ConstructorCallNode>(
						ASTNode(&new_type),
						std::move(substituted_args_nodes),
						call.called_from()
					);
					
					// Wrap in ExpressionNode
					ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_ctor);
					return ASTNode(&new_expr);
				} else {
					FLASH_LOG(Templates, Warning, "  Instantiated template not found in gTypesByName: ", instantiated_name.view());
				}
			} else {
				FLASH_LOG(Templates, Warning, "  Failed to instantiate template: ", func_name);
			}
		}
	}

	// Handle calls without stored template arguments by using pack substitutions when available
	if (!call.has_template_arguments() && !pack_map_.empty()) {
		std::vector<TemplateTypeArg> substituted_template_args;
		for (const auto& pack_entry : pack_map_) {
			substituted_template_args.insert(substituted_template_args.end(), pack_entry.second.begin(), pack_entry.second.end());
			break; // Use the first available pack substitution
		}

		if (!substituted_template_args.empty()) {
			FLASH_LOG(Templates, Debug, "  Attempting to instantiate template (pack-only): ", func_name, " with ", substituted_template_args.size(), " arguments");
			if (auto instantiated_template = parser_.try_instantiate_template_explicit(func_name, substituted_template_args)) {
				if (instantiated_template->is<FunctionDeclarationNode>()) {
					const FunctionDeclarationNode& func_decl = instantiated_template->as<FunctionDeclarationNode>();

					ChunkedVector<ASTNode> substituted_args_nodes;
					for (size_t i = 0; i < call.arguments().size(); ++i) {
						substituted_args_nodes.push_back(substitute(call.arguments()[i]));
					}

					FunctionCallNode& new_call = gChunkedAnyStorage.emplace_back<FunctionCallNode>(
						const_cast<DeclarationNode&>(func_decl.decl_node()),
						std::move(substituted_args_nodes),
						call.called_from()
					);
					ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_call);
					return ASTNode(&new_expr);
				}
			}
		}
	}
	
	// Check if this is actually a template constructor call like base_trait<T>()
	// In this case, the function_declaration might have template information
	FLASH_LOG(Templates, Debug, "  DeclarationNode identifier: ", decl_node.identifier_token().value());
	
	// Check the type_node - it might contain template information
	ASTNode type_node = decl_node.type_node();
	if (type_node.has_value() && type_node.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
		FLASH_LOG(Templates, Debug, "  TypeSpecifierNode: type=", (int)type_spec.type(), " type_index=", type_spec.type_index());
		
		// If this is a struct type, it might be a template instantiation
		if (type_spec.type() == Type::Struct && type_spec.type_index() < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
			std::string_view type_name = StringTable::getStringView(type_info.name());
			FLASH_LOG(Templates, Debug, "  Type name: ", type_name);
			
			// Try to substitute template arguments in this type
			TypeSpecifierNode substituted_type = substituteInType(type_spec);
			
			// If substitution happened, create a new constructor call
			// Check if the type_index changed
			if (substituted_type.type_index() != type_spec.type_index()) {
				FLASH_LOG(Templates, Debug, "  Type was substituted, creating ConstructorCallNode");
				
				// Create a ConstructorCallNode instead of FunctionCallNode
				ChunkedVector<ASTNode> substituted_args_nodes;
				for (size_t i = 0; i < call.arguments().size(); ++i) {
					substituted_args_nodes.push_back(substitute(call.arguments()[i]));
				}
				
				TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(substituted_type);
				ConstructorCallNode& new_ctor = gChunkedAnyStorage.emplace_back<ConstructorCallNode>(
					ASTNode(&new_type),
					std::move(substituted_args_nodes),
					call.called_from()
				);
				
				// Wrap in ExpressionNode
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_ctor);
				return ASTNode(&new_expr);
			}
		}
	}
	
	// If not a template constructor call or no substitution needed, check if the function itself is a template
	// Look up the function in the symbol table
	std::string_view template_func_name = decl_node.identifier_token().value();
	auto symbol_opt = gSymbolTable.lookup(template_func_name);
	
	if (symbol_opt.has_value() && symbol_opt->is<TemplateFunctionDeclarationNode>()) {
		FLASH_LOG(Templates, Debug, "  Function is a template: ", template_func_name);
		
		// This is a template function call - we need to instantiate it
		// Try to deduce template arguments from the substituted function arguments
		ChunkedVector<ASTNode> substituted_args;
		for (size_t i = 0; i < call.arguments().size(); ++i) {
			substituted_args.push_back(substitute(call.arguments()[i]));
		}
		
		// Use shared helper to deduce template arguments from constructor call patterns
		std::vector<TemplateTypeArg> deduced_template_args = TemplateInstantiationHelper::deduceTemplateArgsFromCall(substituted_args);
		
		// If we deduced template arguments, try to instantiate the template function
		if (!deduced_template_args.empty()) {
			// Use shared helper to try instantiation with various name variations
			auto instantiated_opt = TemplateInstantiationHelper::tryInstantiateTemplateFunction(
				parser_, template_func_name, template_func_name, deduced_template_args);
			
			if (instantiated_opt.has_value() && instantiated_opt->is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode& instantiated_func = instantiated_opt->as<FunctionDeclarationNode>();
				FLASH_LOG(Templates, Debug, "  Successfully instantiated template function");
				
				// Create a new FunctionCallNode with the instantiated function
				FunctionCallNode& new_call = gChunkedAnyStorage.emplace_back<FunctionCallNode>(
					const_cast<DeclarationNode&>(instantiated_func.decl_node()),
					std::move(substituted_args),
					call.called_from()
				);
				
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_call);
				return ASTNode(&new_expr);
			} else {
				FLASH_LOG(Templates, Warning, "  Failed to instantiate template function: ", template_func_name);
			}
		}
	}
	
	// If not a template function call or instantiation failed, check for dependent qualified names
	// Pattern: Base$dependentHash::member(args) — needs re-instantiation with concrete types
	// This happens when a template body is re-parsed: Base<T>::member() creates Base$hash1::member
	// during initial template parsing, but the actual instantiation is Base$hash2.
	size_t dollar_pos = func_name.find('$');
	size_t scope_pos = func_name.find("::");
	if (dollar_pos != std::string_view::npos && scope_pos != std::string_view::npos && dollar_pos < scope_pos) {
		std::string_view base_template_name = func_name.substr(0, dollar_pos);
		std::string_view member_name = func_name.substr(scope_pos + 2);
		
		// Collect concrete template arguments from param_map_
		std::vector<TemplateTypeArg> inst_args;
		if (!template_param_order_.empty()) {
			for (std::string_view param_name : template_param_order_) {
				auto it = param_map_.find(param_name);
				if (it != param_map_.end()) {
					inst_args.push_back(it->second);
				}
			}
		} else {
			for (const auto& [param_name, param_arg] : param_map_) {
				inst_args.push_back(param_arg);
			}
		}
		for (const auto& [pack_name, pack_args] : pack_map_) {
			inst_args.insert(inst_args.end(), pack_args.begin(), pack_args.end());
		}
		
		if (!inst_args.empty()) {
			// Instantiate the template with concrete types
			parser_.try_instantiate_class_template(base_template_name, inst_args, true);
			std::string_view instantiated_name = parser_.get_instantiated_class_name(base_template_name, inst_args);
			
			// Build the corrected qualified function name
			StringBuilder new_func_name_builder;
			new_func_name_builder.append(instantiated_name).append("::").append(member_name);
			std::string_view new_func_name = new_func_name_builder.commit();
			
			FLASH_LOG(Templates, Debug, "  Substituted qualified function name: ", func_name, " -> ", new_func_name);
			
			// Create a new token with the corrected name
			Token new_func_token(Token::Type::Identifier, new_func_name,
				call.called_from().line(), call.called_from().column(), call.called_from().file_index());
			
			// Substitute arguments
			ChunkedVector<ASTNode> substituted_args;
			for (size_t i = 0; i < call.arguments().size(); ++i) {
				substituted_args.push_back(substitute(call.arguments()[i]));
			}
			
			// Create new forward declaration with corrected name
			auto& fwd_type_node = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(Type::Int, TypeQualifier::None, 32, Token());
			ASTNode fwd_type_ast(&fwd_type_node);
			auto& new_decl = gChunkedAnyStorage.emplace_back<DeclarationNode>(fwd_type_ast, new_func_token);
			
			// Try to trigger lazy member function instantiation for the target
			StringHandle inst_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
			StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
			auto& lazy_registry = LazyMemberInstantiationRegistry::getInstance();
			if (lazy_registry.needsInstantiation(inst_name_handle, member_handle)) {
				auto lazy_info = lazy_registry.getLazyMemberInfo(inst_name_handle, member_handle);
				if (lazy_info.has_value()) {
					parser_.instantiateLazyMemberFunction(*lazy_info);
				}
			}
			
			FunctionCallNode& new_call = gChunkedAnyStorage.emplace_back<FunctionCallNode>(
				new_decl, std::move(substituted_args), new_func_token);
			ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_call);
			return ASTNode(&new_expr);
		}
	}
	
	// Return as-is
	FLASH_LOG(Templates, Debug, "  Returning function call as-is");
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(call);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteBinaryOp(const BinaryOperatorNode& binop) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing binary operator");
	
	// Recursively substitute in left and right operands
	ASTNode substituted_lhs = substitute(binop.get_lhs());
	ASTNode substituted_rhs = substitute(binop.get_rhs());
	
	// Create new BinaryOperatorNode with substituted operands
	BinaryOperatorNode new_binop_value(
		binop.get_token(),
		substituted_lhs,
		substituted_rhs
	);
	
	// Wrap in ExpressionNode so it can be evaluated by try_evaluate_constant_expression
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_binop_value);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteUnaryOp(const UnaryOperatorNode& unop) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing unary operator");
	
	// Recursively substitute in operand
	ASTNode substituted_operand = substitute(unop.get_operand());
	
	// Create new UnaryOperatorNode with substituted operand
	UnaryOperatorNode new_unop_value(
		unop.get_token(),
		substituted_operand,
		unop.is_prefix(),
		unop.is_builtin_addressof()
	);
	
	// Wrap in ExpressionNode so it can be evaluated by try_evaluate_constant_expression
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_unop_value);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteIdentifier(const IdentifierNode& id) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing identifier: ", id.name());
	
	// Check if this identifier is a template parameter that needs substitution
	auto it = param_map_.find(id.name());
	if (it != param_map_.end()) {
		const TemplateTypeArg& arg = it->second;
		FLASH_LOG(Templates, Debug, "  Found template parameter substitution: ", id.name(), 
			" -> type=", (int)arg.base_type, ", type_index=", arg.type_index, ", is_value=", arg.is_value);
		
		// Handle non-type template parameters (values)
		if (arg.is_value) {
			FLASH_LOG(Templates, Debug, "  Non-type template parameter, creating literal with value: ", arg.value);
			
			// Determine the type based on the template argument's base_type
			Type literal_type = arg.base_type;
			if (literal_type == Type::Template || literal_type == Type::UserDefined) {
				// For template parameters, default to int
				literal_type = Type::Int;
			}
			
			// Handle bool types specially with BoolLiteralNode
			if (literal_type == Type::Bool) {
				std::string_view bool_str = (arg.value != 0) ? "true" : "false";
				Token bool_token(Token::Type::Keyword, bool_str, 0, 0, 0);
				BoolLiteralNode& bool_literal = gChunkedAnyStorage.emplace_back<BoolLiteralNode>(
					bool_token,
					arg.value != 0
				);
				ExpressionNode& expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(bool_literal);
				return ASTNode(&expr);
			}
			
			// Create a numeric literal from the value
			std::string_view value_str = StringBuilder().append(static_cast<uint64_t>(arg.value)).commit();
			Token num_token(Token::Type::Literal, value_str, 0, 0, 0);
			
			NumericLiteralNode& literal = gChunkedAnyStorage.emplace_back<NumericLiteralNode>(
				num_token, 
				static_cast<unsigned long long>(arg.value), 
				literal_type, 
				TypeQualifier::None, 
				64
			);
			
			ExpressionNode& expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(literal);
			return ASTNode(&expr);
		}
		
		// Handle type template parameters
		// Create a TypeSpecifierNode from the template argument
		TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(
			arg.base_type,
			arg.type_index,
			64,  // Default size, will be adjusted by type system
			Token{},
			arg.cv_qualifier
		);
		
		// Add pointer levels if needed
		for (size_t i = 0; i < arg.pointer_depth; ++i) {
			new_type.add_pointer_level();
		}
		
		// Set reference qualifiers
		if (arg.is_rvalue_reference) {
			new_type.set_reference(true);  // true for rvalue reference
		} else if (arg.is_reference) {
			new_type.set_reference(false);  // false for lvalue reference
		}
		
		return ASTNode(&new_type);
	}
	
	// Not a template parameter, return as-is (wrapped in ExpressionNode so the
	// evaluator's is<ExpressionNode>() check passes)
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(id);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteQualifiedIdentifier(const QualifiedIdentifierNode& qual_id) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing qualified identifier: ", qual_id.full_name());
	
	// Qualified identifiers like R1_T::num need template parameter substitution in the namespace part
	// The namespace is stored as a mangled template name like "R1_T" (template R1 with parameter T)
	// We need to substitute T with its concrete type to get "R1_long"
	
	// Get the namespace name (e.g., "R1_T")
	std::string_view ns_name = gNamespaceRegistry.getQualifiedName(qual_id.namespace_handle());
	
	// Check if the entire namespace name is a template parameter (e.g., _R1::num where _R1 is typename _R1)
	// This handles patterns like _R1::num where _R1 is substituted with a concrete struct type
	{
		auto param_it = param_map_.find(ns_name);
		if (param_it != param_map_.end()) {
			const TemplateTypeArg& concrete_type = param_it->second;
			FLASH_LOG(Templates, Debug, "  Namespace '", ns_name, "' is a template parameter, substituting with concrete type");
			
			// The concrete type should be a struct type - get its instantiated name
			if ((concrete_type.base_type == Type::UserDefined || concrete_type.base_type == Type::Struct) &&
			    concrete_type.type_index > 0 && concrete_type.type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[concrete_type.type_index];
				StringHandle type_name_handle = type_info.name();
				
				// If this is a template instantiation, ensure it's been instantiated
				if (type_info.isTemplateInstantiation()) {
					std::string_view base_name = StringTable::getStringView(type_info.baseTemplateName());
					FLASH_LOG(Templates, Debug, "  Concrete type is template instantiation: base='", base_name, "'");
				}
				
				// Create a new QualifiedIdentifierNode with the concrete struct as namespace
				NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
					NamespaceRegistry::GLOBAL_NAMESPACE,
					type_name_handle
				);
				
				QualifiedIdentifierNode& new_qual_id = gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
					new_ns_handle,
					qual_id.identifier_token()
				);
				FLASH_LOG(Templates, Debug, "  Substituted: ", ns_name, "::", qual_id.name(), " -> ", 
				          StringTable::getStringView(type_name_handle), "::", qual_id.name());
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
				return ASTNode(&new_expr);
			}
			
			// Template parameter was found but the concrete type is not a struct/class
			// (e.g., type_index == 0 due to incomplete resolution, or base_type is Template).
			// Return as-is rather than falling through to $/_-based separator logic which
			// would incorrectly parse the namespace name as a mangled template instantiation.
			FLASH_LOG(Templates, Debug, "  Template parameter '", ns_name, 
			          "' found but concrete type is not a resolvable struct (base_type=",
			          static_cast<int>(concrete_type.base_type), ", type_index=", concrete_type.type_index,
			          "), returning as-is");
			ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
			return ASTNode(&new_expr);
		}
	}
	
	// Check if the namespace name contains template parameters
	// Old format: has underscore (e.g., "R1_T")
	// New format: has dollar sign + hash (e.g., "R1$3b360731384e1358")
	size_t separator_pos = ns_name.find('$');
	bool is_hash_based = (separator_pos != std::string_view::npos);
	
	if (!is_hash_based) {
		separator_pos = ns_name.find('_');
		if (separator_pos == std::string_view::npos) {
			// No template parameters, return as-is
			FLASH_LOG(Templates, Debug, "  No template parameters in namespace, returning as-is");
			ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
			return ASTNode(&new_expr);
		}
	}
	
	// Extract the base template name (e.g., "R1")
	std::string_view base_template_name = ns_name.substr(0, separator_pos);

	if (IS_FLASH_LOG_ENABLED(Templates, Debug)) {
		std::string_view param_part = ns_name.substr(separator_pos + 1);
		FLASH_LOG(Templates, Debug, "  Base template: ", base_template_name, ", param part: ", param_part, ", is_hash_based: ", is_hash_based);
	}

	// For hash-based names, the namespace already contains the fully instantiated name
	// We just need to ensure the template is instantiated with the concrete type arguments
	if (is_hash_based) {
		// Hash-based name - extract template arguments from param_map_ and trigger instantiation
		// Use template_param_order_ to preserve the original template parameter declaration order
		// This ensures stricter correctness by maintaining the proper template argument order
		std::vector<TemplateTypeArg> inst_args;
		
		if (!template_param_order_.empty()) {
			// Use the preserved template parameter order for stricter correctness
			for (std::string_view param_name : template_param_order_) {
				auto it = param_map_.find(param_name);
				if (it != param_map_.end()) {
					inst_args.push_back(it->second);
				}
			}
		} else {
			// Fallback: iterate param_map_ in its internal order (less correct but functional)
			for (const auto& [param_name, param_arg] : param_map_) {
				inst_args.push_back(param_arg);
			}
		}
		
		// Also check pack_map_ for variadic template arguments
		for (const auto& [pack_name, pack_args] : pack_map_) {
			inst_args.insert(inst_args.end(), pack_args.begin(), pack_args.end());
		}
		
		if (!inst_args.empty()) {
			FLASH_LOG(Templates, Debug, "  Triggering instantiation of template '", base_template_name, 
			          "' with ", inst_args.size(), " arguments");
			parser_.try_instantiate_class_template(base_template_name, inst_args, true);
			
			// After instantiation, get the actual instantiated name (with new hash based on concrete types)
			// The dependent hash in ns_name is based on template parameters (T), 
			// but we need the hash based on concrete types (long)
			std::string_view instantiated_name = parser_.get_instantiated_class_name(base_template_name, inst_args);
			
			FLASH_LOG(Templates, Debug, "  Substituted namespace: ", ns_name, " -> ", instantiated_name);
			
			// Create a new QualifiedIdentifierNode with the correct instantiated namespace
			StringHandle instantiated_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
			NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
				NamespaceRegistry::GLOBAL_NAMESPACE, 
				instantiated_name_handle
			);
			
			QualifiedIdentifierNode& new_qual_id = gChunkedAnyStorage.emplace_back<QualifiedIdentifierNode>(
				new_ns_handle,
				qual_id.identifier_token()
			);
			ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
			return ASTNode(&new_expr);
		}
		
		// No template arguments - just return as-is
		FLASH_LOG(Templates, Debug, "  Hash-based name, no template arguments to substitute");
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
		return ASTNode(&new_expr);
	}

	// Old underscore-based name - look up by parameter name
	std::string_view param_part = ns_name.substr(separator_pos + 1);
	auto param_it = param_map_.find(param_part);
	if (param_it == param_map_.end()) {
		// Parameter not found in substitution map, return as-is
		FLASH_LOG(Templates, Debug, "  Parameter not found in substitution map");
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
		return ASTNode(&new_expr);
	}
	
	FLASH_LOG_FORMAT(Templates, Warning, "Detected old underscore-based template name format, which is deprecated: {}", ns_name);
	assert(false && "Old underscore-based template names are deprecated");

	const TemplateTypeArg& substitution = param_it->second;
	
	FLASH_LOG(Templates, Debug, "  Found substitution: base_type=", (int)substitution.base_type, 
	          ", type_index=", substitution.type_index);
	
	// Get the type name for the substitution
	std::string_view subst_type_name;
	if (substitution.type_index < gTypeInfo.size() && substitution.type_index > 0) {
		subst_type_name = StringTable::getStringView(gTypeInfo[substitution.type_index].name());
	} else if (substitution.base_type != Type::UserDefined && substitution.base_type != Type::Struct) {
		// For primitive types, use the base_type name
		switch (substitution.base_type) {
			case Type::Int: subst_type_name = "int"; break;
			case Type::Long: subst_type_name = "long"; break;
			case Type::Short: subst_type_name = "short"; break;
			case Type::Char: subst_type_name = "char"; break;
			case Type::Bool: subst_type_name = "bool"; break;
			case Type::Float: subst_type_name = "float"; break;
			case Type::Double: subst_type_name = "double"; break;
			case Type::Void: subst_type_name = "void"; break;
			case Type::UnsignedInt: subst_type_name = "unsigned int"; break;
			case Type::UnsignedLong: subst_type_name = "unsigned long"; break;
			case Type::UnsignedShort: subst_type_name = "unsigned short"; break;
			case Type::UnsignedChar: subst_type_name = "unsigned char"; break;
			case Type::LongLong: subst_type_name = "long long"; break;
			case Type::UnsignedLongLong: subst_type_name = "unsigned long long"; break;
			default:
				// Can't substitute, return as-is
				FLASH_LOG(Templates, Debug, "  Unknown type, cannot substitute");
				ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
				return ASTNode(&new_expr);
		}
	} else {
		// Can't substitute, return as-is
		FLASH_LOG(Templates, Debug, "  Substitution type not found");
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(qual_id);
		return ASTNode(&new_expr);
	}
	
	// Construct the instantiated name: base_template + "_" + subst_type_name
	// e.g., "R1" + "_" + "long" = "R1_long"
	StringBuilder instantiated_name_builder;
	instantiated_name_builder.append(base_template_name);
	instantiated_name_builder.append("_"sv);
	instantiated_name_builder.append(subst_type_name);
	std::string_view instantiated_name = instantiated_name_builder.commit();
	
	FLASH_LOG(Templates, Debug, "  Substituted namespace: ", ns_name, " -> ", instantiated_name);
	
	// Ensure the template is instantiated with the concrete type argument
	// This is necessary so the evaluator can find the type in gTypesByName
	std::vector<TemplateTypeArg> inst_args;
	inst_args.push_back(substitution);
	FLASH_LOG(Templates, Debug, "  Triggering instantiation of template '", base_template_name, "'");
	parser_.try_instantiate_class_template(base_template_name, inst_args, true);
	
	// Look up or register the instantiated namespace
	StringHandle instantiated_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
	NamespaceHandle new_ns_handle = gNamespaceRegistry.getOrCreateNamespace(
		NamespaceRegistry::GLOBAL_NAMESPACE, 
		instantiated_name_handle
	);
	
	// Create a new QualifiedIdentifierNode with the substituted namespace
	QualifiedIdentifierNode new_qual_id(new_ns_handle, qual_id.identifier_token());
	
	// Wrap in ExpressionNode
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_qual_id);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteMemberAccess(const MemberAccessNode& member_access) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing member access on member: ", member_access.member_name());
	
	// Recursively substitute in the object expression
	// For expressions like "R1<T>::num", the object might be a template instantiation
	ASTNode substituted_object = substitute(member_access.object());
	
	// Create new MemberAccessNode with substituted object
	MemberAccessNode new_member_value(
		substituted_object,
		member_access.member_token(),
		member_access.is_arrow()
	);
	
	// Wrap in ExpressionNode
	ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_member_value);
	return ASTNode(&new_expr);
}

ASTNode ExpressionSubstitutor::substituteSizeofExpr(const SizeofExprNode& sizeof_expr) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Processing sizeof expression");
	
	// Get the type or expression from the sizeof
	ASTNode type_or_expr = sizeof_expr.type_or_expr();
	
	if (sizeof_expr.is_type() && type_or_expr.is<TypeSpecifierNode>()) {
		// This is sizeof(type) - substitute the type
		const TypeSpecifierNode& type_spec = type_or_expr.as<TypeSpecifierNode>();
		TypeSpecifierNode substituted_type = substituteInType(type_spec);
		
		// Create new TypeSpecifierNode
		TypeSpecifierNode& new_type = gChunkedAnyStorage.emplace_back<TypeSpecifierNode>(substituted_type);
		
		// Create new SizeofExprNode with substituted type
		SizeofExprNode& new_sizeof = gChunkedAnyStorage.emplace_back<SizeofExprNode>(
			ASTNode(&new_type),
			sizeof_expr.sizeof_token()
		);
		
		// Wrap in ExpressionNode
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_sizeof);
		return ASTNode(&new_expr);
	} else {
		// This is sizeof(expression) - substitute the expression
		ASTNode substituted_expr = substitute(type_or_expr);
		
		// Create new SizeofExprNode with substituted expression
		SizeofExprNode new_sizeof = SizeofExprNode::from_expression(substituted_expr, sizeof_expr.sizeof_token());
		SizeofExprNode& new_sizeof_ref = gChunkedAnyStorage.emplace_back<SizeofExprNode>(new_sizeof);
		
		// Wrap in ExpressionNode
		ExpressionNode& new_expr = gChunkedAnyStorage.emplace_back<ExpressionNode>(new_sizeof_ref);
		return ASTNode(&new_expr);
	}
}

ASTNode ExpressionSubstitutor::substituteLiteral(const ASTNode& literal) {
	// Literals don't need substitution
	return literal;
}

TypeSpecifierNode ExpressionSubstitutor::substituteInType(const TypeSpecifierNode& type) {
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Substituting in type");
	FLASH_LOG_FORMAT(Templates, Debug, "  Input type: base_type={}, type_index={}", (int)type.type(), type.type_index());
	
	// First, check if this is a template parameter type that needs substitution
	// Template parameters can show up as Type::Template, Type::Auto, or Type::UserDefined
	if ((type.type() == Type::Template || type.type() == Type::Auto || type.type() == Type::UserDefined) 
	    && type.type_index() < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[type.type_index()];
		std::string_view type_name = StringTable::getStringView(type_info.name());
		
		FLASH_LOG(Templates, Debug, "  Type is template parameter: ", type_name);
		
		// Look up this template parameter in our substitution map
		auto it = param_map_.find(type_name);
		if (it != param_map_.end()) {
			const TemplateTypeArg& subst = it->second;
			FLASH_LOG(Templates, Debug, "  Substituting template parameter: ", type_name, 
			          " -> base_type=", (int)subst.base_type, ", type_index=", subst.type_index);
			
			// Create a TypeSpecifierNode from the substitution
			// Determine the correct size_in_bits based on the type
			int size_in_bits = get_type_size_bits(subst.base_type);
			if (size_in_bits == 0)
			{
				switch (subst.base_type) {
					case Type::Struct:
					case Type::UserDefined:
						// For struct types, we need to look up the size from TypeInfo
						if (subst.type_index < gTypeInfo.size()) {
							const TypeInfo& ti = gTypeInfo[subst.type_index];
							if (ti.isStruct()) {
								const StructTypeInfo* si = ti.getStructInfo();
								if (si) {
									size_in_bits = si->total_size * 8;
								}
							}
						}
						break;
					default:
						size_in_bits = 64; // Default to 64 bits if unknown
						break;
				}
			}
			
			TypeSpecifierNode substituted_type(
				subst.base_type,
				subst.type_index,
				size_in_bits,
				Token{},
				subst.cv_qualifier,
				subst.reference_qualifier()
			);
			
			substituted_type.add_pointer_levels(subst.pointer_depth);
			
			return substituted_type;
		}
		
		FLASH_LOG(Templates, Warning, "  Template parameter not found in substitution map: ", type_name);
	}
	
	// Check if this is a struct/class type that might have template arguments
	if (type.type() == Type::Struct && type.type_index() < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[type.type_index()];
		std::string_view type_name = StringTable::getStringView(type_info.name());
		
		FLASH_LOG(Templates, Debug, "  Type is struct: ", type_name, " type_index=", type.type_index());
		
		// Parse the type name to see if it contains template arguments
		// Format: "name<arg1, arg2, ...>"
		size_t template_start = type_name.find('<');
		if (template_start != std::string_view::npos) {
			std::string_view base_name = type_name.substr(0, template_start);
			FLASH_LOG(Templates, Debug, "  Found template type: ", base_name);
			
			// Extract the template arguments part (between < and >)
			size_t template_end = type_name.rfind('>');
			if (template_end != std::string_view::npos && template_end > template_start) {
				std::string_view args_str = type_name.substr(template_start + 1, template_end - template_start - 1);
				FLASH_LOG(Templates, Debug, "  Template arguments string: ", args_str);
				
				// Parse the arguments and check if any need substitution
				// For now, we'll do a simple check: if any argument matches a template parameter name
				bool needs_substitution = false;
				for (const auto& [param_name, param_arg] : param_map_) {
					if (args_str.find(param_name) != std::string_view::npos) {
						needs_substitution = true;
						FLASH_LOG(Templates, Debug, "  Found template parameter ", param_name, " in arguments");
						break;
					}
				}
				
				if (needs_substitution) {
					// We need to parse and substitute the template arguments
					// For a simple case like "base_trait<T>", we substitute T
					
					// Simple parser for single template argument (most common case)
					// TODO: Handle multiple arguments separated by commas
					std::vector<TemplateTypeArg> substituted_args;
					
					// Trim whitespace
					while (!args_str.empty() && (args_str.front() == ' ' || args_str.front() == '\t')) {
						args_str.remove_prefix(1);
					}
					while (!args_str.empty() && (args_str.back() == ' ' || args_str.back() == '\t')) {
						args_str.remove_suffix(1);
					}
					
					// Check if this is a simple identifier that needs substitution
					auto it = param_map_.find(args_str);
					if (it != param_map_.end()) {
						FLASH_LOG(Templates, Debug, "  Substituting template argument: ", args_str, " -> type_index=", it->second.type_index);
						substituted_args.push_back(it->second);
						
						// Now instantiate the template with the substituted argument
						auto instantiated_node = parser_.try_instantiate_class_template(base_name, substituted_args, true);
						if (instantiated_node.has_value() && instantiated_node->is<StructDeclarationNode>()) {
							const StructDeclarationNode& class_decl = instantiated_node->as<StructDeclarationNode>();
							StringHandle instantiated_name = class_decl.name();
							
							// Look up the type index for the instantiated template
							auto type_it = gTypesByName.find(instantiated_name);
							if (type_it != gTypesByName.end()) {
								TypeIndex new_type_index = type_it->second->type_index_;
							
								FLASH_LOG(Templates, Debug, "  Successfully instantiated template: ", base_name, " with type_index=", new_type_index);
								
								// Create a new TypeSpecifierNode with the instantiated type
								return TypeSpecifierNode(Type::Struct, new_type_index, 64, Token{}, type.cv_qualifier());
							} else {
								FLASH_LOG(Templates, Warning, "  Instantiated template not found in gTypesByName: ", instantiated_name.view());
							}
						} else {
							FLASH_LOG(Templates, Warning, "  Failed to instantiate template: ", base_name);
						}
					}
				}
			}
		}
	}
	
	// If no substitution needed or failed, return a copy of the original type
	return type;
}

void ExpressionSubstitutor::ensureTemplateInstantiated(
	std::string_view template_name,
	[[maybe_unused]] const std::vector<TemplateTypeArg>& args) {
	
	FLASH_LOG(Templates, Debug, "ExpressionSubstitutor: Ensuring template instantiated: ", template_name);
	
	// TODO: Use parser to trigger template instantiation
	// This will be implemented once we integrate with the parser
}

// Helper: Check if a template argument node is a pack expansion
bool ExpressionSubstitutor::isPackExpansion(const ASTNode& arg_node, std::string_view& pack_name) {
	// Check if this is a TemplateParameterReferenceNode that refers to a pack
	if (arg_node.is<ExpressionNode>()) {
		const ExpressionNode& expr_variant = arg_node.as<ExpressionNode>();
		bool is_template_param_ref = std::visit([](const auto& inner) -> bool {
			using T = std::decay_t<decltype(inner)>;
			return std::is_same_v<T, TemplateParameterReferenceNode>;
		}, expr_variant);
		
		if (is_template_param_ref) {
			const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr_variant);
			pack_name = tparam_ref.param_name().view();
			
			// Check if this parameter is in our pack map
			if (pack_map_.find(pack_name) != pack_map_.end()) {
				FLASH_LOG(Templates, Debug, "Detected pack expansion: ", pack_name);
				return true;
			}
		}
	}
	
	// Also check TypeSpecifierNode for pack types
	if (arg_node.is<TypeSpecifierNode>()) {
		const TypeSpecifierNode& type_spec = arg_node.as<TypeSpecifierNode>();
		if (type_spec.type_index() < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
			pack_name = StringTable::getStringView(type_info.name());
			
			// Check if this type is in our pack map
			if (pack_map_.find(pack_name) != pack_map_.end()) {
				FLASH_LOG(Templates, Debug, "Detected pack expansion (TypeSpecifier): ", pack_name);
				return true;
			}
		}
	}
	
	return false;
}

// Helper: Expand pack parameters in template arguments
std::vector<TemplateTypeArg> ExpressionSubstitutor::expandPacksInArguments(
	const std::vector<ASTNode>& template_arg_nodes) {
	
	std::vector<TemplateTypeArg> expanded_args;
	
	for (const ASTNode& arg_node : template_arg_nodes) {
		std::string_view pack_name;
		
		// Check if this argument is a pack expansion
		if (isPackExpansion(arg_node, pack_name)) {
			// Expand the pack
			auto pack_it = pack_map_.find(pack_name);
			if (pack_it != pack_map_.end()) {
				FLASH_LOG(Templates, Debug, "Expanding pack: ", pack_name, " with ", pack_it->second.size(), " arguments");
				
				// Add all arguments from the pack
				for (const auto& pack_arg : pack_it->second) {
					expanded_args.push_back(pack_arg);
				}
			}
		} else {
			// Regular argument - substitute if it's a template parameter
			if (arg_node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& type_spec = arg_node.as<TypeSpecifierNode>();
				std::string_view type_name = "";
				
				if (type_spec.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_spec.type_index()];
					type_name = StringTable::getStringView(type_info.name());
				}
				
				// Check if this is a scalar template parameter
				auto it = param_map_.find(type_name);
				if (it != param_map_.end()) {
					expanded_args.push_back(it->second);
				} else {
					// Use as-is
					expanded_args.push_back(TemplateTypeArg(type_spec));
				}
			} else if (arg_node.is<ExpressionNode>()) {
				const ExpressionNode& expr_variant = arg_node.as<ExpressionNode>();
				bool is_template_param_ref = std::visit([](const auto& inner) -> bool {
					using T = std::decay_t<decltype(inner)>;
					return std::is_same_v<T, TemplateParameterReferenceNode>;
				}, expr_variant);
				
				if (is_template_param_ref) {
					const TemplateParameterReferenceNode& tparam_ref = std::get<TemplateParameterReferenceNode>(expr_variant);
					std::string_view param_name = tparam_ref.param_name().view();
					
					auto it = param_map_.find(param_name);
					if (it != param_map_.end()) {
						expanded_args.push_back(it->second);
					}
				}
			}
		}
	}
	
	return expanded_args;
}
