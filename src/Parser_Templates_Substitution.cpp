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

					// When a non-type param (e.g., _Size) receives a Type argument due to
					// dependent expressions like sizeof(_Tp), skip the substitution to avoid
					// creating broken identifiers like "user_defined".
					if (tparam.kind() == TemplateParameterKind::NonType && arg.kind != TemplateArgument::Kind::Value) {
						break;  // Leave unsubstituted
					}

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
					
					// Skip substitution when non-type param gets a dependent Type argument
					if (tparam.kind() == TemplateParameterKind::NonType && arg.kind != TemplateArgument::Kind::Value) {
						break;  // Leave unsubstituted
					}
					
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
		
		// Check if this IdentifierNode is a dependent template placeholder (e.g., __cmp_cat_id$hash)
		// These are created during template body parsing for variable template references like __cmp_cat_id<_Ts>
		// We need to re-instantiate the variable template with the substituted type args
		if (std::holds_alternative<IdentifierNode>(expr)) {
			const IdentifierNode& id_node = std::get<IdentifierNode>(expr);
			std::string_view id_name = id_node.name();
			
			// Only check for dependent placeholders if the name contains '$' (the hash separator)
			if (id_name.find('$') != std::string_view::npos) {
			// Look up the type info for this identifier
			StringHandle id_handle = StringTable::getOrInternStringHandle(id_name);
			auto type_it = gTypesByName.find(id_handle);
			if (type_it != gTypesByName.end() && type_it->second->isTemplateInstantiation()) {
				const TypeInfo* placeholder_type = type_it->second;
				std::string_view base_template = StringTable::getStringView(placeholder_type->baseTemplateName());
				
				// Check if this is a variable template
				auto var_template_opt = gTemplateRegistry.lookupVariableTemplate(base_template);
				if (var_template_opt.has_value()) {
					// Get the template args from the placeholder and substitute them
					const auto& placeholder_args = placeholder_type->templateArgs();
					std::vector<TemplateTypeArg> new_args;
					bool any_substituted = false;
					
					for (const auto& parg : placeholder_args) {
						TemplateTypeArg arg;
						arg.base_type = parg.base_type;
						arg.type_index = parg.type_index;
						arg.ref_qualifier = parg.ref_qualifier;
						arg.pointer_depth = parg.pointer_depth;
						arg.cv_qualifier = parg.cv_qualifier;
						
						// Check if this arg is a template parameter that should be substituted
						if (parg.type_index < gTypeInfo.size()) {
							std::string_view arg_type_name = StringTable::getStringView(gTypeInfo[parg.type_index].name());
							for (size_t p = 0; p < template_params.size() && p < template_args.size(); ++p) {
								if (!template_params[p].is<TemplateParameterNode>()) continue;
								const TemplateParameterNode& tparam = template_params[p].as<TemplateParameterNode>();
								if (tparam.name() == arg_type_name) {
									// Substitute with the concrete type
									const TemplateArgument& concrete_arg = template_args[p];
									if (concrete_arg.kind == TemplateArgument::Kind::Type) {
										arg.base_type = concrete_arg.type_value;
										arg.type_index = concrete_arg.type_index;
										arg.is_dependent = false;
										any_substituted = true;
									}
									break;
								}
							}
						}
						new_args.push_back(arg);
					}
					
					if (any_substituted) {
						auto result = try_instantiate_variable_template(base_template, new_args);
						if (result.has_value()) {
							// The variable template was instantiated. Return an IdentifierNode
							// that references the instantiated variable (not the VariableDeclarationNode itself)
							if (result->is<VariableDeclarationNode>()) {
								const auto& var_decl = result->as<VariableDeclarationNode>();
								Token ref_token = var_decl.declaration().identifier_token();
								return emplace_node<ExpressionNode>(IdentifierNode(ref_token));
							}
							return *result;
						}
					}
				}
			}
			} // end of '$' check
		}
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
				const ASTNode& arg = func_call.arguments()[i];
				// Check if this argument is a PackExpansionExprNode that needs to be expanded
				// into multiple arguments (e.g., func(identity(args)...) -> func(identity(args_0), identity(args_1), ...))
				bool expanded = false;
				if (arg.is<ExpressionNode>()) {
					const ExpressionNode& arg_expr = arg.as<ExpressionNode>();
					if (std::holds_alternative<PackExpansionExprNode>(arg_expr)) {
						expanded = expandPackExpansionArgs(
							std::get<PackExpansionExprNode>(arg_expr), template_params, template_args, substituted_args);
					}
				}
				if (!expanded) {
					substituted_args.push_back(substituteTemplateParameters(arg, template_params, template_args));
				}
			}
			
			// Check if function name contains a dependent template hash (Base$hash::member)
			// that needs to be resolved with concrete template arguments
			std::string_view func_name = func_call.called_from().value();
			if (func_name.empty()) func_name = func_call.function_declaration().identifier_token().value();
			size_t scope_pos = func_name.empty() ? std::string_view::npos : func_name.find("::");
			std::string_view base_template_name;
			if (scope_pos != std::string_view::npos) {
				base_template_name = extractBaseTemplateName(func_name.substr(0, scope_pos));
			}
			if (!base_template_name.empty() && scope_pos != std::string_view::npos) {
				std::string_view member_name = func_name.substr(scope_pos + 2);
				
				// Build concrete template arguments from the substitution context
				std::vector<TemplateTypeArg> inst_args;
				for (size_t i = 0; i < template_params.size() && i < template_args.size(); ++i) {
					const TemplateArgument& arg = template_args[i];
					if (arg.kind == TemplateArgument::Kind::Type) {
						TemplateTypeArg type_arg;
						type_arg.base_type = arg.type_value;
						type_arg.type_index = arg.type_index;
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
			
			ASTNode new_func_call = emplace_node<ExpressionNode>(FunctionCallNode(func_call.function_declaration(), std::move(substituted_args), func_call.called_from()));
			// Copy mangled name if present (important for template instantiation)
			if (func_call.has_mangled_name()) {
				std::get<FunctionCallNode>(new_func_call.as<ExpressionNode>()).set_mangled_name(func_call.mangled_name());
			}
			// Substitute and copy template arguments (important for variable templates like __is_ratio_v<T>)
			if (func_call.has_template_arguments()) {
				std::vector<ASTNode> substituted_template_args;
				substituted_template_args.reserve(func_call.template_arguments().size());
				for (const auto& targ : func_call.template_arguments()) {
					substituted_template_args.push_back(substituteTemplateParameters(targ, template_params, template_args));
				}
				std::get<FunctionCallNode>(new_func_call.as<ExpressionNode>()).set_template_arguments(std::move(substituted_template_args));
			}
			if (func_call.has_qualified_name()) {
				std::get<FunctionCallNode>(new_func_call.as<ExpressionNode>()).set_qualified_name(func_call.qualified_name());
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
		
			std::vector<ASTNode> pack_values;
		
			// Handle complex pack expressions like (__cmp_cat_id<_Ts> | ...)
			// where the pack is inside a variable template invocation, not a simple identifier
			if (fold.has_complex_pack_expr()) {
				// Find the variadic template parameter
				size_t variadic_param_idx = SIZE_MAX;
				size_t non_variadic_count = 0;
				for (size_t p = 0; p < template_params.size(); ++p) {
					if (template_params[p].is<TemplateParameterNode>()) {
						const auto& tparam = template_params[p].as<TemplateParameterNode>();
						if (tparam.is_variadic()) {
							variadic_param_idx = p;
						} else {
							non_variadic_count++;
						}
					}
				}
				
				size_t num_pack_elements = 0;
				if (variadic_param_idx != SIZE_MAX && template_args.size() >= non_variadic_count) {
					num_pack_elements = template_args.size() - non_variadic_count;
				}
				
				FLASH_LOG(Templates, Debug, "Complex fold expansion: num_pack_elements=", num_pack_elements);
				
				if (num_pack_elements == 0) {
					// C++17: empty unary fold is allowed only for &&, || and comma
					// For other operators, return identity values
					std::string_view op = fold.op();
					if (op == "&&") {
						Token bool_token(Token::Type::Keyword, "true"sv, fold.get_token().line(), fold.get_token().column(), fold.get_token().file_index());
						return emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, true));
					} else if (op == "||") {
						Token bool_token(Token::Type::Keyword, "false"sv, fold.get_token().line(), fold.get_token().column(), fold.get_token().file_index());
						return emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, false));
					} else if (op == ",") {
						Token void_token(Token::Type::Literal, "0"sv, 0, 0, 0);
						return emplace_node<ExpressionNode>(NumericLiteralNode(
							void_token, 0ULL, Type::Void, TypeQualifier::None, 0));
					}
					FLASH_LOG(Templates, Warning, "Complex fold expression with empty pack and operator '", op, "'");
					return node;
				}
				
				// For each pack element, substitute the variadic parameter in the complex expression
				for (size_t i = 0; i < num_pack_elements; ++i) {
					// Create a single-element template params/args pair for the variadic parameter
					std::vector<ASTNode> single_param = { template_params[variadic_param_idx] };
					std::vector<TemplateArgument> single_arg = { template_args[non_variadic_count + i] };
					
					// Also include the non-variadic parameters so they get substituted too
					std::vector<ASTNode> subst_params;
					std::vector<TemplateArgument> subst_args;
					for (size_t p = 0; p < template_params.size(); ++p) {
						if (template_params[p].is<TemplateParameterNode>()) {
							const auto& tparam = template_params[p].as<TemplateParameterNode>();
							if (tparam.is_variadic()) {
								// Create a non-variadic version of this parameter for single substitution
								TemplateParameterNode single_tparam(tparam.nameHandle(), tparam.token());
								// Don't set variadic - we're substituting one element at a time
								subst_params.push_back(emplace_node<TemplateParameterNode>(single_tparam));
								subst_args.push_back(template_args[non_variadic_count + i]);
							} else if (p < template_args.size()) {
								subst_params.push_back(template_params[p]);
								subst_args.push_back(template_args[p]);
							}
						}
					}
					
					ASTNode substituted = substituteTemplateParameters(*fold.pack_expr(), subst_params, subst_args);
					pack_values.push_back(substituted);
				}
			} else {
				// Simple pack name case: pack_name refers to a function parameter pack (like "args")
				// or a non-type template parameter pack (like "Bs" in (Bs && ...))
				size_t num_pack_elements = count_pack_elements(fold.pack_name());
				
				FLASH_LOG(Templates, Debug, "Fold expansion: pack_name='", fold.pack_name(), "' num_pack_elements=", num_pack_elements);
			
				if (num_pack_elements == 0) {
					// Fallback: check template_params/template_args for non-type parameter packs
					// This handles patterns like template<unsigned... args> constexpr unsigned f() { return (args | ...); }
					std::optional<size_t> pack_param_idx;
					size_t non_variadic_count = 0;
					for (size_t p = 0; p < template_params.size(); ++p) {
						if (template_params[p].is<TemplateParameterNode>()) {
							const auto& tparam = template_params[p].as<TemplateParameterNode>();
							if (tparam.is_variadic() && tparam.name() == fold.pack_name()) {
								pack_param_idx = p;
							} else if (!tparam.is_variadic()) {
								non_variadic_count++;
							}
						}
					}
					
					if (pack_param_idx.has_value() && template_args.size() >= non_variadic_count) {
						size_t pack_size = template_args.size() - non_variadic_count;
						
						// Check if all pack elements are values (non-type parameters)
						bool all_values = true;
						std::vector<int64_t> pack_int_values;
						for (size_t i = non_variadic_count; i < template_args.size(); ++i) {
							if (template_args[i].kind == TemplateArgument::Kind::Value) {
								pack_int_values.push_back(template_args[i].int_value);
							} else {
								all_values = false;
								break;
							}
						}
						
						if (all_values && !pack_int_values.empty()) {
							// Direct evaluation for non-type parameter pack folds
							auto fold_result = ConstExpr::evaluate_fold_expression(fold.op(), pack_int_values);
							if (fold_result.has_value()) {
								std::string_view op = fold.op();
								if (op == "&&" || op == "||") {
									Token bool_token(Token::Type::Keyword, *fold_result ? "true"sv : "false"sv, 0, 0, 0);
									return emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, *fold_result != 0));
								} else {
									// Determine the result type from the variadic parameter's declared type
									// e.g., template<unsigned... args> -> Type::UnsignedInt, 32 bits
									Type result_type = Type::Int;
									int result_size_bits = 32;
									if (pack_param_idx.has_value()) {
										const auto& tparam = template_params[*pack_param_idx].as<TemplateParameterNode>();
										if (tparam.has_type() && tparam.type_node().is<TypeSpecifierNode>()) {
											const TypeSpecifierNode& param_type_spec = tparam.type_node().as<TypeSpecifierNode>();
											result_type = param_type_spec.type();
											result_size_bits = get_type_size_bits(result_type);
										}
									}
									std::string_view val_str = StringBuilder().append(static_cast<uint64_t>(*fold_result)).commit();
									Token num_token(Token::Type::Literal, val_str, 0, 0, 0);
									return emplace_node<ExpressionNode>(NumericLiteralNode(
										num_token, static_cast<unsigned long long>(*fold_result), result_type, TypeQualifier::None, result_size_bits));
								}
							}
						} else if (pack_size == 0) {
							// Empty pack - return identity value per C++17
							std::string_view op = fold.op();
							if (op == "&&") {
								Token bool_token(Token::Type::Keyword, "true"sv, fold.get_token().line(), fold.get_token().column(), fold.get_token().file_index());
								return emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, true));
							} else if (op == "||") {
								Token bool_token(Token::Type::Keyword, "false"sv, fold.get_token().line(), fold.get_token().column(), fold.get_token().file_index());
								return emplace_node<ExpressionNode>(BoolLiteralNode(bool_token, false));
							}
						}
					}
					
					// Also check pack_param_info_ as another fallback
					if (num_pack_elements == 0) {
						auto pack_size = get_pack_size(fold.pack_name());
						if (pack_size.has_value()) {
							num_pack_elements = *pack_size;
						}
					}
					
					if (num_pack_elements == 0) {
						FLASH_LOG(Templates, Warning, "Fold expression pack '", fold.pack_name(), "' has no elements");
						return node;
					}
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
			}
		
			if (pack_values.empty()) {
				FLASH_LOG(Templates, Warning, "Fold expression pack is empty");
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
			FLASH_LOG(Templates, Debug, "*** SizeofPackNode handler entered for pack: '", pack_name, "'");
			
			// Count pack elements using the helper function (works when symbol table scope is active)
			size_t num_pack_elements = count_pack_elements(pack_name);
			
			// Fallback: if count_pack_elements returns 0 (scope may have been exited),
			// try to calculate from template_params/template_args by finding the variadic parameter
			bool found_variadic = false;
			if (num_pack_elements == 0 && !template_args.empty()) {
				// The pack_name is the function parameter name (e.g., "rest")
				// We need to find the corresponding variadic template parameter (e.g., "Rest")
				// The mapping: function param type uses the template param name
				// IMPORTANT: Only match the variadic parameter whose name matches pack_name.
				// Without this check, a member function template with its own variadic params
				// (e.g., Args...) would incorrectly match when sizeof... asks about the class
				// template's pack (e.g., Elements...).
				size_t non_variadic_count = 0;
				for (size_t i = 0; i < template_params.size(); ++i) {
					if (template_params[i].is<TemplateParameterNode>()) {
						const auto& tparam = template_params[i].as<TemplateParameterNode>();
						if (tparam.is_variadic() && tparam.name() == pack_name) {
							found_variadic = true;
						} else if (!tparam.is_variadic()) {
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
			
			// If still not found, check class template pack context
			// This handles sizeof...(_Elements) in member function templates of class templates
			// where _Elements is the class template's parameter pack
			if (!found_variadic) {
				FLASH_LOG(Templates, Debug, "Trying to find pack '", pack_name, "' in class template pack context");
				auto class_pack_size = get_class_template_pack_size(pack_name);
				if (class_pack_size.has_value()) {
					FLASH_LOG(Templates, Debug, "Found pack '", pack_name, "' with size ", *class_pack_size);
					found_variadic = true;
					num_pack_elements = *class_pack_size;
				} else {
					FLASH_LOG(Templates, Debug, "Pack '", pack_name, "' not found in class template pack context");
				}
			}
			
			// If pack name not found, check if it's a known template parameter from an enclosing
			// class template context (e.g., sizeof...(_Elements) in a member function of tuple<_Elements...>).
			// If so, treat as template-dependent and return unchanged.
			// If truly unknown, throw an error.
			if (!found_variadic) {
				// Check if we're inside a template body and the pack name is a known template parameter
				bool is_known_template_param = false;
				if (parsing_template_body_) {
					for (const auto& param_name : current_template_param_names_) {
						if (StringTable::getStringView(param_name) == pack_name) {
							is_known_template_param = true;
							break;
						}
					}
				}
				// Also check if any class template in the registry has this pack name
				if (!is_known_template_param) {
					for (const auto& [key, infos] : class_template_pack_registry_) {
						for (const auto& info : infos) {
							if (info.pack_name == pack_name) {
								is_known_template_param = true;
								break;
							}
						}
						if (is_known_template_param) break;
					}
				}
				// Also check if the pack name is a template parameter of an enclosing class template
				// (e.g., sizeof...(_Elements) inside a member function template of tuple<_Elements...>)
				if (!is_known_template_param && !struct_parsing_context_stack_.empty()) {
					for (auto sit = struct_parsing_context_stack_.rbegin(); sit != struct_parsing_context_stack_.rend() && !is_known_template_param; ++sit) {
						std::string_view struct_name = sit->struct_name;
						// Try multiple lookup candidates following unqualified lookup rules:
						// direct name, template base name for instantiated classes, and each enclosing namespace.
						std::vector<std::string_view> base_names_to_try;
						base_names_to_try.reserve(2);
						std::vector<std::string_view> names_to_try;
						names_to_try.reserve(8);
						auto add_name_to_try = [&names_to_try](std::string_view name) {
							if (name.empty()) {
								return;
							}
							for (const auto existing : names_to_try) {
								if (existing == name) {
									return;
								}
							}
							names_to_try.push_back(name);
						};
						auto add_base_name_to_try = [&base_names_to_try](std::string_view name) {
							if (name.empty()) {
								return;
							}
							for (const auto existing : base_names_to_try) {
								if (existing == name) {
									return;
								}
							}
							base_names_to_try.push_back(name);
						};

						add_base_name_to_try(struct_name);
						std::string_view base_tmpl_name = extractBaseTemplateName(struct_name);
						if (!base_tmpl_name.empty()) {
							add_base_name_to_try(base_tmpl_name);
						}
						for (std::string_view base_name : base_names_to_try) {
							add_name_to_try(base_name);
						}

						NamespaceHandle ns = sit->namespace_handle.isValid() ? sit->namespace_handle : gSymbolTable.get_current_namespace_handle();
						NamespaceHandle walk_ns = ns;
						while (walk_ns.isValid() && !walk_ns.isGlobal()) {
							for (std::string_view base_name : base_names_to_try) {
								StringHandle qualified = gNamespaceRegistry.buildQualifiedIdentifier(
									walk_ns, StringTable::getOrInternStringHandle(base_name));
								add_name_to_try(StringTable::getStringView(qualified));
							}
							walk_ns = gNamespaceRegistry.getParent(walk_ns);
						}
						
						for (size_t ni = 0; ni < names_to_try.size() && !is_known_template_param; ++ni) {
							// Check ALL overloads, not just the first one
							const auto* all_tmpls = gTemplateRegistry.lookupAllTemplates(names_to_try[ni]);
							if (all_tmpls) {
								for (const auto& tmpl_node : *all_tmpls) {
									if (is_known_template_param) break;
									if (tmpl_node.is<TemplateClassDeclarationNode>()) {
										const auto& tmpl_class = tmpl_node.as<TemplateClassDeclarationNode>();
										for (const auto& param : tmpl_class.template_parameters()) {
											if (param.is<TemplateParameterNode>()) {
												const auto& tparam = param.as<TemplateParameterNode>();
												if (tparam.is_variadic()) {
													// Match by name, or match if the stored name is anonymous
													// (from forward declarations like `template<typename...> class tuple;`)
													if (tparam.name() == pack_name || tparam.name().starts_with("__anon_type_")) {
														is_known_template_param = true;
														break;
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
				if (is_known_template_param) {
					FLASH_LOG(Templates, Debug, "sizeof...(", pack_name, ") is from enclosing class template - treating as template-dependent");
					return node;
				}
				FLASH_LOG(Parser, Error, "'" , pack_name, "' does not refer to the name of a parameter pack");
				throw std::runtime_error("'" + std::string(pack_name) + "' does not refer to the name of a parameter pack");
			}
			
			// Create an integer literal with the pack size
			FLASH_LOG(Templates, Debug, "*** Replacing sizeof...(", pack_name, ") with literal: ", num_pack_elements);
			StringBuilder pack_size_builder;
			std::string_view pack_size_str = pack_size_builder.append(num_pack_elements).commit();
			Token literal_token(Token::Type::Literal, pack_size_str, 
			                   sizeof_pack.sizeof_token().line(), sizeof_pack.sizeof_token().column(), 
			                   sizeof_pack.sizeof_token().file_index());
			ASTNode result = emplace_node<ExpressionNode>(
				NumericLiteralNode(literal_token, static_cast<unsigned long long>(num_pack_elements), 
				                  Type::Int, TypeQualifier::None, 32));
			FLASH_LOG(Templates, Debug, "*** Created NumericLiteralNode, returning");
			return result;
		} else if (std::holds_alternative<StaticCastNode>(expr)) {
			// static_cast<Type>(expr) - recursively substitute in both target type and expression
			const StaticCastNode& cast_node = std::get<StaticCastNode>(expr);
			ASTNode substituted_type = substituteTemplateParameters(cast_node.target_type(), template_params, template_args);
			ASTNode substituted_expr = substituteTemplateParameters(cast_node.expr(), template_params, template_args);
			return emplace_node<ExpressionNode>(StaticCastNode(substituted_type, substituted_expr, cast_node.cast_token()));
		} else if (std::holds_alternative<DynamicCastNode>(expr)) {
			const DynamicCastNode& cast_node = std::get<DynamicCastNode>(expr);
			ASTNode substituted_type = substituteTemplateParameters(cast_node.target_type(), template_params, template_args);
			ASTNode substituted_expr = substituteTemplateParameters(cast_node.expr(), template_params, template_args);
			return emplace_node<ExpressionNode>(DynamicCastNode(substituted_type, substituted_expr, cast_node.cast_token()));
		} else if (std::holds_alternative<ConstCastNode>(expr)) {
			const ConstCastNode& cast_node = std::get<ConstCastNode>(expr);
			ASTNode substituted_type = substituteTemplateParameters(cast_node.target_type(), template_params, template_args);
			ASTNode substituted_expr = substituteTemplateParameters(cast_node.expr(), template_params, template_args);
			return emplace_node<ExpressionNode>(ConstCastNode(substituted_type, substituted_expr, cast_node.cast_token()));
		} else if (std::holds_alternative<ReinterpretCastNode>(expr)) {
			const ReinterpretCastNode& cast_node = std::get<ReinterpretCastNode>(expr);
			ASTNode substituted_type = substituteTemplateParameters(cast_node.target_type(), template_params, template_args);
			ASTNode substituted_expr = substituteTemplateParameters(cast_node.expr(), template_params, template_args);
			return emplace_node<ExpressionNode>(ReinterpretCastNode(substituted_type, substituted_expr, cast_node.cast_token()));
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

		// Substitute arguments (with PackExpansionExprNode handling)
		ChunkedVector<ASTNode> substituted_args;
		for (size_t i = 0; i < func_call.arguments().size(); ++i) {
			const ASTNode& arg = func_call.arguments()[i];
			bool expanded = false;
			if (arg.is<ExpressionNode>()) {
				const ExpressionNode& arg_expr = arg.as<ExpressionNode>();
				if (std::holds_alternative<PackExpansionExprNode>(arg_expr)) {
					expanded = expandPackExpansionArgs(
						std::get<PackExpansionExprNode>(arg_expr), template_params, template_args, substituted_args);
				}
			}
			if (!expanded) {
				substituted_args.push_back(substituteTemplateParameters(arg, template_params, template_args));
			}
		}

		// For now, don't substitute the function declaration itself
		// Create new function call with substituted arguments
		ASTNode new_func_call = emplace_node<FunctionCallNode>(func_call.function_declaration(), std::move(substituted_args), func_call.called_from());
		// Copy mangled name if present (important for template instantiation)
		if (func_call.has_mangled_name()) {
			new_func_call.as<FunctionCallNode>().set_mangled_name(func_call.mangled_name());
		}
		// Substitute and copy template arguments (important for variable templates like __is_ratio_v<T>)
		if (func_call.has_template_arguments()) {
			std::vector<ASTNode> substituted_template_args;
			substituted_template_args.reserve(func_call.template_arguments().size());
			for (const auto& targ : func_call.template_arguments()) {
				substituted_template_args.push_back(substituteTemplateParameters(targ, template_params, template_args));
			}
			new_func_call.as<FunctionCallNode>().set_template_arguments(std::move(substituted_template_args));
		}
		if (func_call.has_qualified_name()) {
			new_func_call.as<FunctionCallNode>().set_qualified_name(func_call.qualified_name());
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
		
		ASTNode new_var_node = emplace_node<VariableDeclarationNode>(var_decl.declaration_node(), initializer, var_decl.storage_class());
		VariableDeclarationNode& new_var = new_var_node.as<VariableDeclarationNode>();
		
		// Preserve constexpr/constinit flags
		if (var_decl.is_constexpr()) new_var.set_is_constexpr(true);
		if (var_decl.is_constinit()) new_var.set_is_constinit(true);
		
		// For constexpr variables with substituted initializers, update the symbol table
		// so that subsequent if constexpr conditions can look up the concrete value
		if (var_decl.is_constexpr() && initializer.has_value()) {
			std::string_view var_name = var_decl.declaration().identifier_token().value();
			gSymbolTable.insert(var_name, new_var_node);
		}
		
		return new_var_node;

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
			eval_ctx.parser = this;
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

// Expand a PackExpansionExprNode into multiple substituted arguments for function calls.
// For each pack element, the pattern expression is cloned with the pack identifier replaced,
// then template parameters are substituted.
bool Parser::expandPackExpansionArgs(
	const PackExpansionExprNode& pack_expansion,
	const std::vector<ASTNode>& template_params,
	const std::vector<TemplateArgument>& template_args,
	ChunkedVector<ASTNode>& out_args) {

	const ASTNode& pattern = pack_expansion.pattern();

	// Find the variadic template parameter and count non-variadic params
	size_t variadic_param_idx = SIZE_MAX;
	size_t non_variadic_count = 0;
	for (size_t p = 0; p < template_params.size(); ++p) {
		if (template_params[p].is<TemplateParameterNode>()) {
			const auto& tparam = template_params[p].as<TemplateParameterNode>();
			if (tparam.is_variadic()) variadic_param_idx = p;
			else non_variadic_count++;
		}
	}

	size_t num_pack_elements = 0;
	if (variadic_param_idx != SIZE_MAX && template_args.size() >= non_variadic_count) {
		num_pack_elements = template_args.size() - non_variadic_count;
	}

	// Also check pack_param_info_ for function parameter packs
	std::string_view func_pack_name;
	for (const auto& pack_info : pack_param_info_) {
		if (pack_info.pack_size > 0) {
			func_pack_name = pack_info.original_name;
			if (num_pack_elements == 0) num_pack_elements = pack_info.pack_size;
			break;
		}
	}

	if (num_pack_elements == 0) return false;

	FLASH_LOG(Templates, Debug, "Expanding PackExpansionExprNode in function call args: ", num_pack_elements, " elements");
	for (size_t pi = 0; pi < num_pack_elements; ++pi) {
		// Build substitution params for this single pack element
		std::vector<ASTNode> subst_params;
		std::vector<TemplateArgument> subst_args;
		for (size_t p = 0; p < template_params.size(); ++p) {
			if (!template_params[p].is<TemplateParameterNode>()) continue;
			const auto& tparam = template_params[p].as<TemplateParameterNode>();
			if (tparam.is_variadic()) {
				TemplateParameterNode single_tparam(tparam.nameHandle(), tparam.token());
				subst_params.push_back(emplace_node<TemplateParameterNode>(single_tparam));
				subst_args.push_back(template_args[non_variadic_count + pi]);
			} else if (p < template_args.size()) {
				subst_params.push_back(template_params[p]);
				subst_args.push_back(template_args[p]);
			}
		}

		// Replace the function parameter pack identifier (e.g., "args") with
		// the expanded element name (e.g., "args_0") in the pattern before substitution
		ASTNode expanded_pattern = replacePackIdentifierInExpr(pattern, func_pack_name, pi);
		ASTNode substituted = substituteTemplateParameters(expanded_pattern, subst_params, subst_args);
		out_args.push_back(substituted);
	}
	return true;
}

// Replace a pack parameter identifier in an expression pattern with its expanded element name.
// For example, given pattern `identity(args)` and pack_name="args", element_index=2,
// this returns `identity(args_2)`.
// Recursively walks the expression tree to find and replace IdentifierNodes matching pack_name.
ASTNode Parser::replacePackIdentifierInExpr(const ASTNode& expr, std::string_view pack_name, size_t element_index) {
	if (!expr.has_value() || pack_name.empty()) return expr;

	// Handle ExpressionNode variant
	if (expr.is<ExpressionNode>()) {
		const ExpressionNode& expr_variant = expr.as<ExpressionNode>();

		if (std::holds_alternative<IdentifierNode>(expr_variant)) {
			const IdentifierNode& id = std::get<IdentifierNode>(expr_variant);
			if (id.name() == pack_name) {
				StringBuilder expanded_name;
				expanded_name.append(pack_name);
				expanded_name.append('_');
				expanded_name.append(element_index);
				std::string_view expanded_sv = expanded_name.commit();
				Token new_token(Token::Type::Identifier, expanded_sv, 0, 0, 0);
				return emplace_node<ExpressionNode>(IdentifierNode(new_token));
			}
			return expr;
		}

		if (std::holds_alternative<FunctionCallNode>(expr_variant)) {
			const FunctionCallNode& call = std::get<FunctionCallNode>(expr_variant);
			ChunkedVector<ASTNode> new_args;
			for (size_t i = 0; i < call.arguments().size(); ++i) {
				new_args.push_back(replacePackIdentifierInExpr(call.arguments()[i], pack_name, element_index));
			}
			ASTNode new_call = emplace_node<ExpressionNode>(
				FunctionCallNode(call.function_declaration(), std::move(new_args), call.called_from()));
			if (call.has_template_arguments()) {
				std::vector<ASTNode> new_template_args;
				for (const auto& ta : call.template_arguments()) {
					new_template_args.push_back(replacePackIdentifierInExpr(ta, pack_name, element_index));
				}
				std::get<FunctionCallNode>(new_call.as<ExpressionNode>()).set_template_arguments(std::move(new_template_args));
			}
			if (call.has_mangled_name()) {
				std::get<FunctionCallNode>(new_call.as<ExpressionNode>()).set_mangled_name(call.mangled_name());
			}
			return new_call;
		}

		if (std::holds_alternative<BinaryOperatorNode>(expr_variant)) {
			const BinaryOperatorNode& binop = std::get<BinaryOperatorNode>(expr_variant);
			ASTNode new_lhs = replacePackIdentifierInExpr(binop.get_lhs(), pack_name, element_index);
			ASTNode new_rhs = replacePackIdentifierInExpr(binop.get_rhs(), pack_name, element_index);
			return emplace_node<ExpressionNode>(BinaryOperatorNode(binop.get_token(), new_lhs, new_rhs));
		}

		if (std::holds_alternative<UnaryOperatorNode>(expr_variant)) {
			const UnaryOperatorNode& unop = std::get<UnaryOperatorNode>(expr_variant);
			ASTNode new_operand = replacePackIdentifierInExpr(unop.get_operand(), pack_name, element_index);
			return emplace_node<ExpressionNode>(UnaryOperatorNode(unop.get_token(), new_operand, unop.is_prefix()));
		}

		if (std::holds_alternative<ConstructorCallNode>(expr_variant)) {
			const ConstructorCallNode& ctor = std::get<ConstructorCallNode>(expr_variant);
			ChunkedVector<ASTNode> new_args;
			for (size_t i = 0; i < ctor.arguments().size(); ++i) {
				new_args.push_back(replacePackIdentifierInExpr(ctor.arguments()[i], pack_name, element_index));
			}
			return emplace_node<ExpressionNode>(ConstructorCallNode(ctor.type_node(), std::move(new_args), ctor.called_from()));
		}

		if (std::holds_alternative<StaticCastNode>(expr_variant)) {
			const StaticCastNode& cast = std::get<StaticCastNode>(expr_variant);
			ASTNode new_expr = replacePackIdentifierInExpr(cast.expr(), pack_name, element_index);
			return emplace_node<ExpressionNode>(StaticCastNode(cast.target_type(), new_expr, cast.cast_token()));
		}

		if (std::holds_alternative<DynamicCastNode>(expr_variant)) {
			const DynamicCastNode& cast = std::get<DynamicCastNode>(expr_variant);
			ASTNode new_expr = replacePackIdentifierInExpr(cast.expr(), pack_name, element_index);
			return emplace_node<ExpressionNode>(DynamicCastNode(cast.target_type(), new_expr, cast.cast_token()));
		}

		if (std::holds_alternative<ConstCastNode>(expr_variant)) {
			const ConstCastNode& cast = std::get<ConstCastNode>(expr_variant);
			ASTNode new_expr = replacePackIdentifierInExpr(cast.expr(), pack_name, element_index);
			return emplace_node<ExpressionNode>(ConstCastNode(cast.target_type(), new_expr, cast.cast_token()));
		}

		if (std::holds_alternative<ReinterpretCastNode>(expr_variant)) {
			const ReinterpretCastNode& cast = std::get<ReinterpretCastNode>(expr_variant);
			ASTNode new_expr = replacePackIdentifierInExpr(cast.expr(), pack_name, element_index);
			return emplace_node<ExpressionNode>(ReinterpretCastNode(cast.target_type(), new_expr, cast.cast_token()));
		}

		// For other variant types, return as-is
		return expr;
	}

	// Handle direct FunctionCallNode
	if (expr.is<FunctionCallNode>()) {
		const FunctionCallNode& call = expr.as<FunctionCallNode>();
		ChunkedVector<ASTNode> new_args;
		for (size_t i = 0; i < call.arguments().size(); ++i) {
			new_args.push_back(replacePackIdentifierInExpr(call.arguments()[i], pack_name, element_index));
		}
		return emplace_node<FunctionCallNode>(call.function_declaration(), std::move(new_args), call.called_from());
	}

	return expr;
}
