#include "Parser.h"
#include "IrGenerator.h"

	AstToIr::AstToIr(SymbolTable& global_symbol_table, CompileContext& context, Parser& parser)
		: global_symbol_table_(&global_symbol_table), context_(&context), parser_(&parser) {
		// Generate static member declarations for template classes before processing AST
		generateStaticMemberDeclarations();
		// Generate trivial default constructors for structs that need them
		generateTrivialDefaultConstructors();
	}

	void AstToIr::visit(const ASTNode& node) {
		// Skip empty nodes (e.g., from forward declarations)
		if (!node.has_value()) {
			return;
		}

		if (node.is<FunctionDeclarationNode>()) {
			visitFunctionDeclarationNode(node.as<FunctionDeclarationNode>());
			// Clear function context after completing a top-level function
			current_function_name_ = StringHandle();
		}
		else if (node.is<ReturnStatementNode>()) {
			visitReturnStatementNode(node.as<ReturnStatementNode>());
		}
		else if (node.is<VariableDeclarationNode>()) {
			visitVariableDeclarationNode(node);
		}
		else if (node.is<StructuredBindingNode>()) {
			visitStructuredBindingNode(node);
		}
		else if (node.is<IfStatementNode>()) {
			visitIfStatementNode(node.as<IfStatementNode>());
		}
		else if (node.is<ForStatementNode>()) {
			visitForStatementNode(node.as<ForStatementNode>());
		}
		else if (node.is<RangedForStatementNode>()) {
			visitRangedForStatementNode(node.as<RangedForStatementNode>());
		}
		else if (node.is<WhileStatementNode>()) {
			visitWhileStatementNode(node.as<WhileStatementNode>());
		}
		else if (node.is<DoWhileStatementNode>()) {
			visitDoWhileStatementNode(node.as<DoWhileStatementNode>());
		}
		else if (node.is<SwitchStatementNode>()) {
			visitSwitchStatementNode(node.as<SwitchStatementNode>());
		}
		else if (node.is<BreakStatementNode>()) {
			visitBreakStatementNode(node.as<BreakStatementNode>());
		}
		else if (node.is<ContinueStatementNode>()) {
			visitContinueStatementNode(node.as<ContinueStatementNode>());
		}
		else if (node.is<GotoStatementNode>()) {
			visitGotoStatementNode(node.as<GotoStatementNode>());
		}
		else if (node.is<LabelStatementNode>()) {
			visitLabelStatementNode(node.as<LabelStatementNode>());
		}
		else if (node.is<TryStatementNode>()) {
			visitTryStatementNode(node.as<TryStatementNode>());
		}
		else if (node.is<ThrowStatementNode>()) {
			visitThrowStatementNode(node.as<ThrowStatementNode>());
		}
		else if (node.is<SehTryExceptStatementNode>()) {
			visitSehTryExceptStatementNode(node.as<SehTryExceptStatementNode>());
		}
		else if (node.is<SehTryFinallyStatementNode>()) {
			visitSehTryFinallyStatementNode(node.as<SehTryFinallyStatementNode>());
		}
		else if (node.is<SehLeaveStatementNode>()) {
			visitSehLeaveStatementNode(node.as<SehLeaveStatementNode>());
		}
		else if (node.is<BlockNode>()) {
			visitBlockNode(node.as<BlockNode>());
		}
		else if (node.is<ExpressionNode>()) {
			visitExpressionNode(node.as<ExpressionNode>());
			emitAndClearFullExpressionTempDestructors();
		}
		else if (node.is<StructDeclarationNode>()) {
			// Clear struct context for top-level structs to prevent them from being
			// mistakenly treated as nested classes of the previous struct
			current_struct_name_ = StringHandle();
			visitStructDeclarationNode(node.as<StructDeclarationNode>());
		}
		else if (node.is<EnumDeclarationNode>()) {
			visitEnumDeclarationNode(node.as<EnumDeclarationNode>());
		}
		else if (node.is<NamespaceDeclarationNode>()) {
			visitNamespaceDeclarationNode(node.as<NamespaceDeclarationNode>());
		}
		else if (node.is<UsingDirectiveNode>()) {
			visitUsingDirectiveNode(node.as<UsingDirectiveNode>());
		}
		else if (node.is<UsingDeclarationNode>()) {
			visitUsingDeclarationNode(node.as<UsingDeclarationNode>());
		}
		else if (node.is<UsingEnumNode>()) {
			visitUsingEnumNode(node.as<UsingEnumNode>());
		}
		else if (node.is<NamespaceAliasNode>()) {
			visitNamespaceAliasNode(node.as<NamespaceAliasNode>());
		}
		else if (node.is<ConstructorDeclarationNode>()) {
			visitConstructorDeclarationNode(node.as<ConstructorDeclarationNode>());
			// Clear function context after completing a top-level constructor
			current_function_name_ = StringHandle();
		}
		else if (node.is<DestructorDeclarationNode>()) {
			visitDestructorDeclarationNode(node.as<DestructorDeclarationNode>());
			// Clear function context after completing a top-level destructor
			current_function_name_ = StringHandle();
		}
		else if (node.is<DeclarationNode>()) {
			// Forward declarations or global variable declarations
			// These are already in the symbol table, no code generation needed
			return;
		}
		else if (node.is<TypeSpecifierNode>()) {
			// Type specifier nodes can appear in the AST for forward declarations
			// No code generation needed
			return;
		}
		else if (node.is<TypedefDeclarationNode>()) {
			// Typedef declarations don't generate code - they're handled during parsing
			return;
		}
		else if (node.is<TemplateFunctionDeclarationNode>()) {
			// Template declarations produce no IR of their own; IR is generated when each
			// instantiation is visited (see try_instantiate_class_template / try_instantiate_function_template).
			return;
		}
		else if (node.is<TemplateClassDeclarationNode>()) {
			// Class template declarations produce no IR of their own; IR is generated when each
			// instantiation is visited (see try_instantiate_class_template).
			return;
		}
		else if (node.is<TemplateAliasNode>()) {
			// Template alias declarations don't generate code - they're compile-time type substitutions
			// The type is resolved during parsing when the alias is used
			return;
		}
		else if (node.is<TemplateVariableDeclarationNode>()) {
			// Template variable declarations don't generate code yet - they're stored for later instantiation
			// Instantiations are generated when the template is used with explicit template arguments
			return;
		}
		else if (node.is<ConceptDeclarationNode>()) {
			// Concept declarations don't generate code - they're compile-time constraints
			// Concepts are evaluated during template instantiation (constraint checking not yet implemented)
			return;
		}
		else if (node.is<RequiresExpressionNode>()) {
			// Requires expressions don't generate code - they're compile-time constraints
			// They are evaluated during constraint checking
			return;
		}
		else if (node.is<CompoundRequirementNode>()) {
			// Compound requirements don't generate code - they're compile-time constraints
			// They are part of requires expressions and evaluated during constraint checking
			return;
		}
		else if (node.is<LambdaExpressionNode>()) {
			// Lambda expression as a statement
			// Evaluate the lambda (creates closure instance) but discard the result
			generateLambdaExpressionIr(node.as<LambdaExpressionNode>());
			emitAndClearFullExpressionTempDestructors();
		}
		else {
			puts(node.type_name());
			assert(false && "Unhandled AST node type");
		}
	}

	void AstToIr::generateCollectedLambdas() {
		// Generate lambdas, processing newly added ones as they appear.
		// Nested lambdas are collected during body generation and will be processed
		// in subsequent iterations of this loop.
		// Example: auto maker = []() { return [](int x) { return x; }; };
		//          When generating maker's body, the inner lambda is collected
		//          and will be processed in the next iteration.

		// Process until no new lambdas are added
		size_t processed_count = 0;
		size_t deferred_scan_start = 0;
		while (true) {
			while (processed_count < collected_lambdas_.size()) {
				// Process from the end (newly added lambdas) backwards
				size_t current_size = collected_lambdas_.size();
				for (size_t i = current_size; i > processed_count; --i) {
					if (sema_) {
						sema_->normalizeInstantiatedLambdaBody(collected_lambdas_[i - 1]);
					}

					// Re-access via index after normalization to avoid any stale-
					// reference risk (the vector could theoretically reallocate).
					LambdaInfo& stored_lambda_info = collected_lambdas_[i - 1];

					// Generic lambdas are only emitted once an instantiation has provided
					// concrete deduced parameter types. Untouched generic lambdas remain in
					// the deferred list and are generated on demand after a real call site.
					if (stored_lambda_info.is_generic && stored_lambda_info.deduced_auto_types.empty()) {
						continue;
					}
					// Skip if this lambda has already been generated (prevents duplicate definitions)
					if (generated_lambda_ids_.find(stored_lambda_info.lambda_id) != generated_lambda_ids_.end()) {
						continue;
					}

					// Copy the LambdaInfo before calling generateLambdaFunctions because that
					// function may push new lambdas which can reallocate the vector and
					// invalidate any references.
					LambdaInfo lambda_info = stored_lambda_info;
					generated_lambda_ids_.insert(lambda_info.lambda_id);
					generateLambdaFunctions(lambda_info);
				}
				processed_count = current_size;
			}

			bool generated_deferred_lambda = false;
			for (size_t di = deferred_scan_start; di < collected_lambdas_.size(); ++di) {
				if (sema_) {
					sema_->normalizeInstantiatedLambdaBody(collected_lambdas_[di]);
				}
				LambdaInfo& stored_lambda_info = collected_lambdas_[di];

				if (!stored_lambda_info.is_generic || stored_lambda_info.deduced_auto_types.empty()) {
					// Only advance the scan window past non-generic lambdas.
					// Generic lambdas with empty deduced_auto_types may become
					// ready later (e.g., a call site in a subsequently generated
					// lambda body populates their deduced types), so they must
					// remain in the scan window.
					if (di == deferred_scan_start && (!stored_lambda_info.is_generic || stored_lambda_info.deduced_auto_types.empty())) {
						deferred_scan_start = di + 1;
					}
					continue;
				}
				if (generated_lambda_ids_.find(stored_lambda_info.lambda_id) != generated_lambda_ids_.end()) {
					continue;
				}

				LambdaInfo lambda_info = stored_lambda_info;
				generated_lambda_ids_.insert(lambda_info.lambda_id);
				generateLambdaFunctions(lambda_info);
				generated_deferred_lambda = true;
			}

			if (!generated_deferred_lambda && processed_count >= collected_lambdas_.size()) {
				break;
			}
		}
	}

	void AstToIr::generateCollectedLocalStructMembers() {
		for (const auto& member_info : collected_local_struct_members_) {
			// Temporarily restore context
			StringHandle saved_function = current_function_name_;
			current_struct_name_ = member_info.struct_name;
			current_function_name_ = member_info.enclosing_function_name;

			// Visit the member function
			visit(member_info.member_function_node);

			// Restore
			current_function_name_ = saved_function;
		}
	}

	std::string AstToIr::get_deferred_func_name(const ASTNode& node) const {
		if (node.is<FunctionDeclarationNode>()) {
			return std::string(node.as<FunctionDeclarationNode>().decl_node().identifier_token().value());
		}
		if (node.is<ConstructorDeclarationNode>()) {
			return std::string(StringTable::getStringView(node.as<ConstructorDeclarationNode>().struct_name())) + " constructor";
		}
		if (node.is<DestructorDeclarationNode>()) {
			return std::string(StringTable::getStringView(node.as<DestructorDeclarationNode>().struct_name())) + " destructor";
		}
		if (node.is<TemplateFunctionDeclarationNode>()) {
			const auto& tmpl = node.as<TemplateFunctionDeclarationNode>();
			if (tmpl.function_declaration().is<FunctionDeclarationNode>()) {
				return std::string(tmpl.function_declaration().as<FunctionDeclarationNode>().decl_node().identifier_token().value());
			}
		}
		return "unknown";
	}

	size_t AstToIr::generateDeferredMemberFunctions() {
		size_t processed = 0;
		size_t error_count = 0;
		while (processed < deferred_member_functions_.size()) {
		DeferredMemberFunctionInfo info = deferred_member_functions_[processed++];
			StringHandle saved_function = current_function_name_;
			auto saved_namespace = current_namespace_stack_;
			current_struct_name_ = info.struct_name;
			current_function_name_ = StringHandle();
			current_namespace_stack_ = info.namespace_stack;

			try {
			if (info.function_node.is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode& func = info.function_node.as<FunctionDeclarationNode>();
				// If the function has no body, it may be a lazily-registered template member.
				// Trigger lazy instantiation via the parser so the body becomes available.
				if (!func.get_definition().has_value() && !func.is_implicit() && parser_) {
					StringHandle member_handle = func.decl_node().identifier_token().handle();
					const bool is_const_func = func.is_const_member_function();
					if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(info.struct_name, member_handle, is_const_func)) {
						auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(info.struct_name, member_handle, is_const_func);
						if (lazy_info_opt.has_value()) {
							auto new_func_node = parser_->instantiateLazyMemberFunction(*lazy_info_opt);
							if (new_func_node.has_value() && new_func_node->is<FunctionDeclarationNode>()) {
								LazyMemberInstantiationRegistry::getInstance().markInstantiated(info.struct_name, member_handle, is_const_func);
								visitFunctionDeclarationNode(new_func_node->as<FunctionDeclarationNode>());
								current_function_name_ = saved_function;
								current_namespace_stack_ = saved_namespace;
								continue;
							}
						}
					}
				}
				visitFunctionDeclarationNode(func);
			} else if (info.function_node.is<ConstructorDeclarationNode>()) {
				visitConstructorDeclarationNode(info.function_node.as<ConstructorDeclarationNode>());
			} else if (info.function_node.is<DestructorDeclarationNode>()) {
				visitDestructorDeclarationNode(info.function_node.as<DestructorDeclarationNode>());
			} else if (info.function_node.is<TemplateFunctionDeclarationNode>()) {
				const auto& tmpl = info.function_node.as<TemplateFunctionDeclarationNode>();
				if (tmpl.function_declaration().is<FunctionDeclarationNode>()) {
					visitFunctionDeclarationNode(tmpl.function_declaration().as<FunctionDeclarationNode>());
				}
			}
			} catch (const CompileError&) {
				// Semantic errors must propagate — they are real compilation failures
				current_function_name_ = saved_function;
				current_namespace_stack_ = saved_namespace;
				throw;
			} catch (const std::exception& e) {
				std::string func_name = get_deferred_func_name(info.function_node);
				FLASH_LOG(Codegen, Error, "Deferred member function '", func_name, "' generation failed: ", e.what());
				++error_count;
			}

			current_function_name_ = saved_function;
			current_namespace_stack_ = saved_namespace;
		}
		return error_count;
	}

	void AstToIr::generateCollectedTemplateInstantiations() {
		for (const auto& inst_info : collected_template_instantiations_) {
			generateTemplateInstantiation(inst_info);
		}
	}

	void AstToIr::generateStaticMemberDeclarations() {
		auto append_bytes = [](unsigned long long value, int size_in_bits, std::vector<char>& target) {
			size_t byte_count = size_in_bits / 8;
			for (size_t i = 0; i < byte_count; ++i) {
				target.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
			}
		};

		auto evaluate_static_initializer = [&](const ASTNode& expr_node, unsigned long long& out_value, const StructTypeInfo* struct_info) -> bool {
			ConstExpr::EvaluationContext ctx(*global_symbol_table_);
			ctx.storage_duration = ConstExpr::StorageDuration::Static;
			// Enable on-demand template instantiation when static member initializers
			// reference uninstantiated template members during constexpr evaluation
			ctx.parser = parser_;
			// Set struct_info so that sizeof(T) can be resolved from template arguments in struct name
			ctx.struct_info = struct_info;
			if (struct_info) {
				if (const LazyClassInstantiationInfo* lazy_class_info =
						LazyClassInstantiationRegistry::getInstance().getLazyClassInfo(struct_info->name)) {
					ctx.template_args = lazy_class_info->template_args;
					ctx.template_param_names.reserve(lazy_class_info->template_params.size());
					for (const auto& template_param : lazy_class_info->template_params) {
						if (template_param.is<TemplateParameterNode>()) {
							ctx.template_param_names.push_back(template_param.as<TemplateParameterNode>().name());
						}
					}
				} else {
					auto struct_type_it = getTypesByNameMap().find(struct_info->name);
					if (struct_type_it != getTypesByNameMap().end() && struct_type_it->second->isTemplateInstantiation()) {
						const TypeInfo* struct_type = struct_type_it->second;
						auto param_handles = gTemplateRegistry.getTemplateParameters(struct_type->baseTemplateName());
						if (param_handles.empty()) {
							if (auto template_node_opt = gTemplateRegistry.lookupTemplate(struct_type->baseTemplateName());
								template_node_opt.has_value() && template_node_opt->is<TemplateClassDeclarationNode>()) {
								for (std::string_view param_name : template_node_opt->as<TemplateClassDeclarationNode>().template_param_names()) {
									ctx.template_param_names.push_back(param_name);
								}
							}
						}
						ctx.template_param_names.reserve(ctx.template_param_names.size() + param_handles.size());
						ctx.template_args.reserve(struct_type->templateArgs().size());
						for (StringHandle param_handle : param_handles) {
							ctx.template_param_names.push_back(StringTable::getStringView(param_handle));
						}
						for (const auto& arg_info : struct_type->templateArgs()) {
							ctx.template_args.push_back(toTemplateTypeArg(arg_info));
						}
					}
				}
			}

			auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);
			if (!eval_result.success()) {
				if (struct_info && expr_node.is<ExpressionNode>()) {
					const auto& expr = expr_node.as<ExpressionNode>();
					if (std::holds_alternative<FunctionCallNode>(expr)) {
						const auto& func_call = std::get<FunctionCallNode>(expr);
						StringHandle func_name_handle = func_call.function_declaration().identifier_token().handle();

						if (parser_ && LazyMemberInstantiationRegistry::getInstance().needsInstantiationAny(struct_info->name, func_name_handle)) {
							if (auto lazy_info = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfoAny(struct_info->name, func_name_handle)) {
								parser_->instantiateLazyMemberFunction(*lazy_info);
								LazyMemberInstantiationRegistry::getInstance().markInstantiated(struct_info->name, func_name_handle, lazy_info->identity.is_const_method);
							}
						}

						const ASTNode* member_function_node = nullptr;
						const FunctionDeclarationNode* member_function_decl = nullptr;
						for (const auto& member_func : struct_info->member_functions) {
							if (member_func.getName() != func_name_handle || !member_func.function_decl.is<FunctionDeclarationNode>()) {
								continue;
							}

							const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
							if (!func_decl.is_static() || !func_decl.get_definition().has_value() ||
								func_decl.parameter_nodes().size() != func_call.arguments().size()) {
								continue;
							}

							member_function_node = &member_func.function_decl;
							member_function_decl = &func_decl;
							break;
						}

						if (member_function_node) {
							global_symbol_table_->enter_scope(ScopeType::Block);
							global_symbol_table_->insert(func_call.function_declaration().identifier_token().value(), *member_function_node);

							ConstExpr::EvaluationContext rebound_ctx(*global_symbol_table_);
							rebound_ctx.storage_duration = ConstExpr::StorageDuration::Static;
							rebound_ctx.parser = parser_;
							rebound_ctx.template_param_names = ctx.template_param_names;
							rebound_ctx.template_args = ctx.template_args;
							if (rebound_ctx.template_param_names.empty() && rebound_ctx.template_args.empty() && member_function_decl) {
								StringBuilder qualified_name_builder;
								StringHandle qualified_name = StringTable::getOrInternStringHandle(
									qualified_name_builder
										.append(member_function_decl->parent_struct_name())
										.append("::")
										.append(member_function_decl->decl_node().identifier_token().value())
										.commit());
								if (const OuterTemplateBinding* outer_binding = gTemplateRegistry.getOuterTemplateBinding(qualified_name)) {
									rebound_ctx.template_args.assign(outer_binding->param_args.begin(), outer_binding->param_args.end());
									rebound_ctx.template_param_names.reserve(outer_binding->param_names.size());
									for (StringHandle param_name : outer_binding->param_names) {
										rebound_ctx.template_param_names.push_back(StringTable::getStringView(param_name));
									}
								}
							}

							auto rebound_result = ConstExpr::Evaluator::evaluate(expr_node, rebound_ctx);
							global_symbol_table_->exit_scope();
							if (rebound_result.success()) {
								eval_result = std::move(rebound_result);
							}
						}
					}
				}
			}
			if (!eval_result.success()) {
				return false;
			}

			if (const auto* ull_val = std::get_if<unsigned long long>(&eval_result.value)) {
				out_value = *ull_val;
				return true;
			}
			if (const auto* ll_val = std::get_if<long long>(&eval_result.value)) {
				out_value = static_cast<unsigned long long>(*ll_val);
				return true;
			}
			if (const auto* b_val = std::get_if<bool>(&eval_result.value)) {
				out_value = *b_val ? 1ULL : 0ULL;
				return true;
			}
			if (const auto* d_val = std::get_if<double>(&eval_result.value)) {
				double d = *d_val;
				out_value = static_cast<unsigned long long>(d);
				return true;
			}

			return false;
		};

		for (const auto& [type_name, type_info] : getTypesByNameMap()) {
			if (!type_info->isStruct()) {
				continue;
			}
			// Skip pattern structs - they're templates and shouldn't generate code
			if (gTemplateRegistry.isPatternStructName(type_name)) {
				continue;
			}

			// Skip structs with incomplete instantiation - they have unresolved template params
			if (type_info->is_incomplete_instantiation_) {
				FLASH_LOG(Codegen, Debug, "Skipping struct '", StringTable::getStringView(type_name), "' (incomplete instantiation)");
				continue;
			}

			// Skip if we've already processed this TypeInfo pointer
			// (same struct can be registered under multiple keys in getTypesByNameMap())
			if (processed_type_infos_.count(type_info) > 0) {
				continue;
			}
			processed_type_infos_.insert(type_info);

			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (!struct_info) {
				continue;
			}

			// Generate static members that this struct directly owns
			if (!struct_info->static_members.empty()) {
				for (const auto& static_member : struct_info->static_members) {
					bool unresolved_identifier_initializer = false;
					// Skip static members with unsubstituted template parameters, identifiers, or sizeof...
					// These are in pattern templates and should only generate code when instantiated
					if (static_member.initializer.has_value() && static_member.initializer->is<ExpressionNode>()) {
						const ExpressionNode& expr = static_member.initializer->as<ExpressionNode>();
						if (std::holds_alternative<SizeofPackNode>(expr)) {
							// This is an uninstantiated template - skip
							FLASH_LOG(Codegen, Debug, "Skipping static member '", static_member.getName(),
							"' with unsubstituted sizeof... in type '", type_name, "'");
							continue;
						}
						if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
							// Template parameter not substituted - this is a template pattern, not an instantiation
							// Skip it (instantiated versions will have NumericLiteralNode instead)
							const auto& tparam = std::get<TemplateParameterReferenceNode>(expr);
							FLASH_LOG(Codegen, Debug, "Skipping static member '", static_member.getName(),
							"' with unsubstituted template parameter '", tparam.param_name(),
							"' in type '", type_name, "'");
							continue;
						}
						// Also skip IdentifierNode that looks like an unsubstituted template parameter
						// (pattern templates may have IdentifierNode instead of TemplateParameterReferenceNode)
						if (std::holds_alternative<IdentifierNode>(expr)) {
							const auto& id = std::get<IdentifierNode>(expr);
							// If the identifier is not in the global symbol table and is a simple name (no qualified access),
							// it's likely an unsubstituted template parameter - skip it
							// Instantiated templates will have NumericLiteralNode or other concrete expressions
							auto symbol = global_symbol_table_->lookup(id.name());
							if (!symbol.has_value()) {
								// Not found in global symbol table - likely a template parameter
								FLASH_LOG(Codegen, Debug, "Skipping static member '", static_member.getName(),
								"' with identifier initializer '", id.name(),
								"' in type '", type_name, "' (identifier not in symbol table - likely template parameter)");
								unresolved_identifier_initializer = true;
							}
						}
					}

					// Build the qualified name for deduplication
					// Use type_info->name() (the canonical name) instead of type_name (the lookup key)
					// This ensures consistency when the same TypeInfo is registered under multiple names
					// (e.g., "result_true" and "detail::result_true" both point to the same TypeInfo)
					StringBuilder qualified_name_sb;
					qualified_name_sb.append(StringTable::getStringView(type_info->name())).append("::").append(static_member.getName());
					std::string_view qualified_name = qualified_name_sb.commit();
					StringHandle name_handle = StringTable::getOrInternStringHandle(qualified_name);

					// Skip if already emitted
					if (emitted_static_members_.count(name_handle) > 0) {
						continue;
					}
					emitted_static_members_.insert(name_handle);

					GlobalVariableDeclOp op;
					op.type_index = static_member.type_index;
					op.size_in_bits = SizeInBits{static_cast<int>(static_member.size * 8)};
					// If size is 0 for struct types, look up from type info
					if (!op.size_in_bits.is_set() && static_member.type_index.is_valid() && static_member.type_index.index() < getTypeInfoCount()) {
						const StructTypeInfo* member_si = getTypeInfo(static_member.type_index).getStructInfo();
						if (member_si) {
							op.size_in_bits = SizeInBits{static_cast<int>(member_si->total_size * 8)};
						}
					}
					op.var_name = name_handle;  // Phase 3: Now using StringHandle instead of string_view

					// Check if static member has an initializer
					op.is_initialized = static_member.initializer.has_value() || unresolved_identifier_initializer;
					auto zero_initialize = [&]() {
						size_t byte_count = op.size_in_bits.value / 8;
						for (size_t i = 0; i < byte_count; ++i) {
							op.init_data.push_back(0);
						}
					};
						if (unresolved_identifier_initializer) {
							FLASH_LOG(Codegen, Debug, "Initializer unresolved; zero-initializing static member '", qualified_name, "'");
							zero_initialize();
						} else if (op.is_initialized) {
							if (static_member.initializer->is<InitializerListNode>()) {
								if (static_member.type_index.category() == TypeCategory::Struct &&
									static_member.type_index.is_valid() &&
									static_member.type_index.index() < getTypeInfoCount()) {
									if (const StructTypeInfo* static_struct_info = getTypeInfo(static_member.type_index).getStructInfo()) {
										op.init_data.resize(static_struct_info->total_size, 0);
										auto eval_aggregate_leaf = [&](const ASTNode& leaf_expr, Type target_type) -> unsigned long long {
											unsigned long long leaf_value = 0;
											if (evaluate_static_initializer(leaf_expr, leaf_value, struct_info)) {
												if (typeToCategory(target_type) == TypeCategory::Float) {
													ConstExpr::EvaluationContext ctx(*global_symbol_table_);
													ctx.storage_duration = ConstExpr::StorageDuration::Static;
													ctx.parser = parser_;
													auto eval_result = ConstExpr::Evaluator::evaluate(leaf_expr, ctx);
													if (eval_result.success()) {
														float f = static_cast<float>(eval_result.as_double());
														uint32_t f_bits;
														std::memcpy(&f_bits, &f, sizeof(float));
														return f_bits;
													}
												} else if (typeToCategory(target_type) == TypeCategory::Double || typeToCategory(target_type) == TypeCategory::LongDouble) {
													ConstExpr::EvaluationContext ctx(*global_symbol_table_);
													ctx.storage_duration = ConstExpr::StorageDuration::Static;
													ctx.parser = parser_;
													auto eval_result = ConstExpr::Evaluator::evaluate(leaf_expr, ctx);
													if (eval_result.success()) {
														double d = eval_result.as_double();
														unsigned long long bits;
														std::memcpy(&bits, &d, sizeof(double));
														return bits;
													}
												}
												return leaf_value;
											}
											return 0;
										};
										fillAggregateInitData(op.init_data, *static_struct_info, static_member.initializer->as<InitializerListNode>(), eval_aggregate_leaf);
										FLASH_LOG(Codegen, Debug, "Packed aggregate initializer for static member '", qualified_name, "'");
									} else {
										FLASH_LOG(Codegen, Debug, "Static member initializer references missing struct info for '", qualified_name, "', zero-initializing");
										zero_initialize();
									}
								} else {
									// Non-struct InitializerListNode (e.g., static constexpr int x{42}).
									// Extract the single element and evaluate as a scalar.
									const auto& init_list = static_member.initializer->as<InitializerListNode>();
									if (init_list.size() == 1) {
										unsigned long long evaluated_value = 0;
										if (evaluate_static_initializer(init_list.initializers()[0], evaluated_value, struct_info)) {
											append_bytes(evaluated_value, op.size_in_bits.value, op.init_data);
											FLASH_LOG(Codegen, Debug, "Evaluated scalar brace initializer for static member '", qualified_name, "' = ", evaluated_value);
										} else {
											FLASH_LOG(Codegen, Debug, "Failed to evaluate scalar brace initializer for static member '", qualified_name, "', zero-initializing");
											zero_initialize();
										}
									} else if (init_list.size() == 0) {
										FLASH_LOG(Codegen, Debug, "Empty brace initializer for non-struct static member '", qualified_name, "', zero-initializing");
										zero_initialize();
									} else {
										FLASH_LOG(Codegen, Debug, "Multi-element initializer list for non-struct static member '", qualified_name, "', zero-initializing");
										zero_initialize();
									}
								}
							} else if (!static_member.initializer->is<ExpressionNode>()) {
								FLASH_LOG(Codegen, Debug, "Static member initializer is not an expression for '", qualified_name, "', zero-initializing (actual type: ", static_member.initializer->type_name(), ")");
								zero_initialize();
							} else {
							const ExpressionNode& init_expr = static_member.initializer->as<ExpressionNode>();

					// Check for ConstructorCallNode (e.g., T() which becomes int() after substitution)
					if (std::holds_alternative<ConstructorCallNode>(init_expr)) {
						const auto& ctor_call = std::get<ConstructorCallNode>(init_expr);
						bool evaluated_ctor = false;
						// Try constexpr evaluation for constructor calls with arguments
						if (!ctor_call.arguments().empty()) {
							const ASTNode& ctor_type_node = ctor_call.type_node();
							if (ctor_type_node.is<TypeSpecifierNode>()) {
								const TypeSpecifierNode& ctor_type_spec = ctor_type_node.as<TypeSpecifierNode>();
								TypeIndex ctor_type_index = ctor_type_spec.type_index();
								if (ctor_type_index.index() < getTypeInfoCount()) {
									const StructTypeInfo* ctor_struct_info = getTypeInfo(ctor_type_index).getStructInfo();
									if (ctor_struct_info) {
										const ConstructorDeclarationNode* matching_ctor = nullptr;
										if (parser_) {
											std::vector<TypeSpecifierNode> arg_types;
											arg_types.reserve(ctor_call.arguments().size());
											for (const auto& arg : ctor_call.arguments()) {
												auto arg_type_opt = parser_->get_expression_type(arg);
												if (!arg_type_opt.has_value()) {
													arg_types.clear();
													break;
												}
												TypeSpecifierNode arg_type = *arg_type_opt;
												adjust_argument_type_for_overload_resolution(arg, arg_type);
												arg_types.push_back(std::move(arg_type));
											}
											if (arg_types.size() == ctor_call.arguments().size()) {
												auto resolution = resolve_constructor_overload(*ctor_struct_info, arg_types, false);
												if (resolution.is_ambiguous) {
													throw CompileError("Ambiguous constructor call");
												}
												matching_ctor = resolution.selected_overload;
											}
										}
										if (!matching_ctor) {
											auto arity_resolution = resolve_constructor_overload_arity(*ctor_struct_info, ctor_call.arguments().size(), true);
											matching_ctor = arity_resolution.selected_overload;
										}
										if (matching_ctor) {
											// Evaluate arguments
											ConstExpr::EvaluationContext eval_ctx(*global_symbol_table_);
											std::unordered_map<std::string_view, ConstExpr::EvalResult> param_bindings;
											eval_ctx.local_bindings = &param_bindings;
											std::unordered_map<std::string_view, long long> param_values;
											bool args_ok = true;
											const auto& params = matching_ctor->parameter_nodes();
											for (size_t ai = 0; ai < params.size(); ++ai) {
												if (!params[ai].is<DeclarationNode>()) continue;
												const auto& param_decl = params[ai].as<DeclarationNode>();
												ConstExpr::EvalResult arg_result;
												if (ai < ctor_call.arguments().size()) {
													arg_result = ConstExpr::Evaluator::evaluate(ctor_call.arguments()[ai], eval_ctx);
												} else if (param_decl.has_default_value()) {
													arg_result = ConstExpr::Evaluator::evaluate(param_decl.default_value(), eval_ctx);
												} else {
													args_ok = false;
													break;
												}
												if (arg_result.success()) {
													param_bindings[param_decl.identifier_token().value()] = arg_result;
													param_values[param_decl.identifier_token().value()] = arg_result.as_int();
												} else {
													args_ok = false;
													break;
												}
											}
											if (args_ok) {
												// Evaluate each member's value from constructor initializer list
												size_t total_bytes = op.size_in_bits.value / 8;
												op.init_data.resize(total_bytes, 0);
												for (const auto& member : ctor_struct_info->members) {
													long long member_val = 0;
													for (const auto& mem_init : matching_ctor->member_initializers()) {
														if (mem_init.member_name == StringTable::getStringView(member.getName())) {
															// Try identifier lookup in param_values first
															if (mem_init.initializer_expr.is<ExpressionNode>()) {
																const auto& init_e = mem_init.initializer_expr.as<ExpressionNode>();
																if (const auto* identifier_ptr = std::get_if<IdentifierNode>(&init_e)) {
																	auto it = param_values.find(identifier_ptr->name());
																	if (it != param_values.end()) member_val = it->second;
																}
															}
															// Also try full constexpr eval as fallback
															auto eval_r = ConstExpr::Evaluator::evaluate(mem_init.initializer_expr, eval_ctx);
															if (eval_r.success()) member_val = eval_r.as_int();
															break;
														}
													}
													for (size_t bi = 0; bi < member.size && (member.offset + bi) < total_bytes; ++bi) {
														op.init_data[member.offset + bi] = static_cast<char>((static_cast<unsigned long long>(member_val) >> (bi * 8)) & 0xFF);
													}
												}
												evaluated_ctor = true;
												FLASH_LOG(Codegen, Debug, "Evaluated constexpr ConstructorCallNode initializer for static member '",
												qualified_name, "'");
											}
										}
									}
								}
							}
						}
						if (!evaluated_ctor) {
							FLASH_LOG(Codegen, Debug, "Processing ConstructorCallNode initializer for static member '",
							qualified_name, "' - initializing to zero");
							size_t byte_count = op.size_in_bits.value / 8;
							for (size_t i = 0; i < byte_count; ++i) {
								op.init_data.push_back(0);
							}
						}
					} else if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
						const auto& bool_lit = std::get<BoolLiteralNode>(init_expr);
						FLASH_LOG(Codegen, Debug, "Processing BoolLiteralNode initializer for static member '",
						qualified_name, "' value=", bool_lit.value() ? "true" : "false");
						unsigned long long value = bool_lit.value() ? 1ULL : 0ULL;
						size_t byte_count = op.size_in_bits.value / 8;
						for (size_t i = 0; i < byte_count; ++i) {
							op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
						}
						FLASH_LOG(Codegen, Debug, "  Wrote ", byte_count, " bytes to init_data");
					} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
						FLASH_LOG(Codegen, Debug, "Processing NumericLiteralNode initializer for static member '",
						qualified_name, "'");
						// Evaluate the initializer expression
						ExprResult init_operands = visitExpressionNode(init_expr);
						// Convert to raw bytes
						{
							unsigned long long value = 0;
							if (const auto* ull_val_ptr = std::get_if<unsigned long long>(&init_operands.value)) {
								value = *ull_val_ptr;
								FLASH_LOG(Codegen, Debug, "  Extracted uint64 value: ", value);
							} else if (const auto* d_val = std::get_if<double>(&init_operands.value)) {
								double d = *d_val;
								std::memcpy(&value, &d, sizeof(double));
								FLASH_LOG(Codegen, Debug, "  Extracted double value: ", d);
							}
							size_t byte_count = op.size_in_bits.value / 8;
							for (size_t i = 0; i < byte_count; ++i) {
								op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
							}
							FLASH_LOG(Codegen, Debug, "  Wrote ", byte_count, " bytes to init_data");
						}
					} else if (std::holds_alternative<TemplateParameterReferenceNode>(init_expr)) {
						FLASH_LOG(Codegen, Debug, "WARNING: Processing TemplateParameterReferenceNode initializer for static member '",
						qualified_name, "' - should have been substituted!");
						// Try to evaluate anyway
						ExprResult init_operands = visitExpressionNode(init_expr);
						{
							unsigned long long value = 0;
							if (const auto* ull_val = std::get_if<unsigned long long>(&init_operands.value)) {
								value = *ull_val;
							} else if (const auto* d_val_ptr = std::get_if<double>(&init_operands.value)) {
								double d = *d_val_ptr;
								std::memcpy(&value, &d, sizeof(double));
							}
							size_t byte_count = op.size_in_bits.value / 8;
							for (size_t i = 0; i < byte_count; ++i) {
								op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
							}
						}
						} else if (std::holds_alternative<IdentifierNode>(init_expr)) {
							const auto& id = std::get<IdentifierNode>(init_expr);
							FLASH_LOG(Codegen, Debug, "Processing IdentifierNode '", id.name(), "' initializer for static member '",
							qualified_name, "'");
							// For reference members, the initializer is an identifier whose address
							// should be stored via a data relocation (like &x for int& ref = x)
							if (static_member.reference_qualifier != ReferenceQualifier::None) {
								StringHandle target_handle = StringTable::getOrInternStringHandle(id.name());
								op.reloc_target = target_handle;
								// Zero-fill the slot; the linker fills the actual address
								size_t byte_count = op.size_in_bits.value / 8;
								for (size_t i = 0; i < byte_count; ++i) {
									op.init_data.push_back(0);
								}
								FLASH_LOG(Codegen, Debug, "  Set reloc_target='", id.name(), "' for reference static member");
							} else {
							// Evaluate the initializer expression
							ExprResult init_operands = visitExpressionNode(init_expr);
							{
								unsigned long long value = 0;
								if (const auto* ull_val = std::get_if<unsigned long long>(&init_operands.value)) {
									value = *ull_val;
								} else if (const auto* d_val = std::get_if<double>(&init_operands.value)) {
									double d = *d_val;
									std::memcpy(&value, &d, sizeof(double));
								}
								size_t byte_count = op.size_in_bits.value / 8;
								for (size_t i = 0; i < byte_count; ++i) {
									op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
								}
							}
							}
						} else if (std::holds_alternative<UnaryOperatorNode>(init_expr)) {
							const auto& unary = std::get<UnaryOperatorNode>(init_expr);
							// Handle address-of operator: &identifier
							if (unary.op() == "&" && unary.get_operand().is<ExpressionNode>()) {
								const ExpressionNode& inner = unary.get_operand().as<ExpressionNode>();
								if (std::holds_alternative<IdentifierNode>(inner)) {
									const auto& target_id = std::get<IdentifierNode>(inner);
									FLASH_LOG(Codegen, Debug, "Processing &", target_id.name(), " initializer for static member '",
									qualified_name, "'");
									StringHandle target_handle = StringTable::getOrInternStringHandle(target_id.name());
									op.reloc_target = target_handle;
									// Zero-fill the pointer slot; the linker fills the actual address
									size_t byte_count = op.size_in_bits.value / 8;
									for (size_t i = 0; i < byte_count; ++i) {
										op.init_data.push_back(0);
									}
								} else {
									FLASH_LOG(Codegen, Debug, "Address-of non-identifier for static member '",
									qualified_name, "' - zero-initializing");
									append_bytes(0, op.size_in_bits.value, op.init_data);
								}
							} else {
								// Other unary operators - try constexpr evaluation
								unsigned long long evaluated_value = 0;
								if (evaluate_static_initializer(*static_member.initializer, evaluated_value, struct_info)) {
									append_bytes(evaluated_value, op.size_in_bits.value, op.init_data);
								} else {
									append_bytes(0, op.size_in_bits.value, op.init_data);
								}
							}
						} else {
							unsigned long long evaluated_value = 0;
							if (evaluate_static_initializer(*static_member.initializer, evaluated_value, struct_info)) {
								FLASH_LOG(Codegen, Debug, "Evaluated constexpr initializer for static member '",
								qualified_name, "' = ", evaluated_value);
								append_bytes(evaluated_value, op.size_in_bits.value, op.init_data);
							} else {
								// Try triggering lazy instantiation for template static members
								// The initializer may contain unsubstituted template parameters
								bool resolved_via_lazy = false;
								if (parser_) {
									parser_->instantiateLazyStaticMember(struct_info->name, static_member.getName());
									// Re-lookup the member after lazy instantiation may have updated it
									const StructStaticMember* updated = struct_info->findStaticMember(static_member.getName());
									if (updated && updated->initializer.has_value()) {
										if (evaluate_static_initializer(*updated->initializer, evaluated_value, struct_info)) {
											FLASH_LOG(Codegen, Debug, "Evaluated lazy-instantiated constexpr initializer for static member '",
											qualified_name, "' = ", evaluated_value);
											append_bytes(evaluated_value, op.size_in_bits.value, op.init_data);
											resolved_via_lazy = true;
										}
									}
								}
								if (!resolved_via_lazy) {
									FLASH_LOG(Codegen, Debug, "Processing unknown expression type initializer for static member '",
									qualified_name, "' - skipping evaluation");
									// For unknown expression types, skip evaluation to avoid crashes
									// Initialize to zero as a safe default
									append_bytes(0, op.size_in_bits.value, op.init_data);
								}
							}
						}
					}
				}
				// static const/constexpr members with constant initializers go to .rodata (read-only).
				// constexpr implies const, so this covers both 'static constexpr T val = x' and
				// 'static const T val = x' (both are compile-time constants when initialized).
				if (static_member.is_const() && op.is_initialized) {
					op.is_rodata = true;
				}
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), Token()));
			}
		}

			// Also check if this struct inherits static members from base classes
			// and generate alias definitions if needed (Phase 3: Generate ALL inherited static members)
			if (!struct_info->base_classes.empty()) {
				for (const auto& base : struct_info->base_classes) {
					if (base.type_index.index() >= getTypeInfoCount()) {
						continue;
					}

					const TypeInfo& base_type = getTypeInfo(base.type_index);
					const StructTypeInfo* base_info = base_type.getStructInfo();

					// If base_type is a type alias (no struct_info), follow type_index_ to get the actual struct
					// This handles cases like `struct Test : wrapper<true_type>::type` where `::type` is a type alias
					if (!base_info && base_type.type_index_ != base.type_index && base_type.type_index_.index() < getTypeInfoCount()) {
						const TypeInfo& resolved_type = getTypeInfo(base_type.type_index_);
						base_info = resolved_type.getStructInfo();
						FLASH_LOG(Codegen, Debug, "Resolved type alias '", StringTable::getStringView(base_type.name_),
						"' to struct '", StringTable::getStringView(resolved_type.name_), "'");
					}

					// Special handling for type aliases like "bool_constant_true::type"
					// The StructTypeInfo for the type alias may have static members with unsubstituted initializers
					// In this case, we need to find the actual underlying struct and use its static members instead
					if (base_info && base.name.find("::") != std::string_view::npos) {
						// Extract the struct name before "::" (e.g., "bool_constant_true" from "bool_constant_true::type")
						auto pos = base.name.rfind("::");
						if (pos != std::string_view::npos) {
							std::string_view actual_struct_name = base.name.substr(0, pos);
							auto actual_struct_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(actual_struct_name));
							if (actual_struct_it != getTypesByNameMap().end()) {
								const StructTypeInfo* actual_info = actual_struct_it->second->getStructInfo();
								if (actual_info) {
									FLASH_LOG(Codegen, Debug, "Using actual struct '", actual_struct_name,
									"' instead of type alias '", base.name, "' for static members");
									base_info = actual_info;
								}
							}
						}
					}

					// Iterate through ALL static members in the base class hierarchy (Phase 3 fix)
					if (base_info) {
						// Collect all static members recursively from this base and its bases
						std::vector<std::pair<const StructStaticMember*, const StructTypeInfo*>> all_static_members;

						// Use a queue to traverse the inheritance hierarchy
						std::queue<const StructTypeInfo*> to_visit;
						std::unordered_set<const StructTypeInfo*> visited;
						to_visit.push(base_info);

						while (!to_visit.empty()) {
							const StructTypeInfo* current = to_visit.front();
							to_visit.pop();

							if (visited.count(current)) continue;
							visited.insert(current);

							// Add all static members from current struct
							for (const auto& static_member : current->static_members) {
								all_static_members.emplace_back(&static_member, current);
							}

							// Add base classes to queue
							for (const auto& base_spec : current->base_classes) {
								if (base_spec.type_index.index() < getTypeInfoCount()) {
									const TypeInfo& base_type_info = getTypeInfo(base_spec.type_index);
									if (const StructTypeInfo* base_struct = base_type_info.getStructInfo()) {
										to_visit.push(base_struct);
									}
								}
							}
						}

						// Generate inherited static member definitions for each one found
						for (const auto& [static_member_ptr, owner_struct] : all_static_members) {
							std::string_view member_name = StringTable::getStringView(static_member_ptr->name);

							// Generate definition for this derived class
							StringBuilder derived_qualified_name_sb;
							derived_qualified_name_sb.append(type_name).append("::").append(member_name);
							std::string_view derived_qualified_name = derived_qualified_name_sb.commit();
							StringHandle derived_name_handle = StringTable::getOrInternStringHandle(derived_qualified_name);

							// Skip if already emitted
							if (emitted_static_members_.count(derived_name_handle) > 0) {
								continue;
							}
							emitted_static_members_.insert(derived_name_handle);

							// Use the original base class name from the BaseClassSpecifier, not the resolved type
							std::string_view base_name_str = base.name;

							FLASH_LOG(Codegen, Debug, "Generating inherited static member '", member_name,
							"' for ", type_name, " from base ", base_name_str);

							GlobalVariableDeclOp alias_op;
							alias_op.type_index = static_member_ptr->type_index;
							alias_op.size_in_bits = SizeInBits{static_cast<int>(static_member_ptr->size * 8)};
							alias_op.var_name = derived_name_handle;
							alias_op.is_initialized = true;

							// Evaluate the initializer to get the value
							bool found_base_value = false;
							unsigned long long inferred_value = 0;

							if (static_member_ptr->initializer.has_value() &&
							static_member_ptr->initializer->is<ExpressionNode>()) {
								const ExpressionNode& init_expr = static_member_ptr->initializer->as<ExpressionNode>();

								if (const auto* bool_lit = std::get_if<BoolLiteralNode>(&init_expr)) {
									inferred_value = bool_lit->value() ? 1ULL : 0ULL;
									found_base_value = true;
									FLASH_LOG(Codegen, Debug, "Found bool literal value: ", bool_lit->value());
								} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
									ExprResult init_operands = visitExpressionNode(init_expr);
									if (const auto* ull_val = std::get_if<unsigned long long>(&init_operands.value)) {
										inferred_value = *ull_val;
										found_base_value = true;
										FLASH_LOG(Codegen, Debug, "Found numeric literal value: ", inferred_value);
									} else if (const auto* d_val = std::get_if<double>(&init_operands.value)) {
										double d = *d_val;
										inferred_value = static_cast<unsigned long long>(d);
										found_base_value = true;
										FLASH_LOG(Codegen, Debug, "Found double literal value: ", d);
									}
								} else if (evaluate_static_initializer(*static_member_ptr->initializer, inferred_value, owner_struct)) {
									found_base_value = true;
									FLASH_LOG(Codegen, Debug, "Evaluated constexpr initializer for inherited static member '", member_name, "'");
								}
							}

							// Write the value to init_data
							append_bytes(inferred_value, alias_op.size_in_bits.value, alias_op.init_data);

							if (!found_base_value) {
								FLASH_LOG(Codegen, Debug, "Using default zero value (no initializer found)");
							}

							ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(alias_op), Token()));
						}
					}
				}
			}
		}
	}

	void AstToIr::generateTrivialDefaultConstructors() {
		std::unordered_set<const TypeInfo*> processed;

		for (const auto& [type_name, type_info] : getTypesByNameMap()) {
			if (!type_info->isStruct()) {
				continue;
			}

			// Skip pattern structs
			if (gTemplateRegistry.isPatternStructName(type_name)) {
				continue;
			}

			// Skip structs with incomplete instantiation - they have unresolved template params
			if (type_info->is_incomplete_instantiation_) {
				FLASH_LOG(Codegen, Debug, "Skipping trivial constructor for '", StringTable::getStringView(type_name), "' (incomplete instantiation)");
				continue;
			}

			// Skip if already processed
			if (processed.count(type_info) > 0) {
				continue;
			}
			processed.insert(type_info);

			const StructTypeInfo* struct_info = type_info->getStructInfo();
			if (!struct_info) {
				continue;
			}

			// Only generate trivial constructor if explicitly marked as needing one
			// The needs_default_constructor flag is set during template instantiation
			// when a struct has no constructors but needs a default one
			if (!struct_info->needs_default_constructor) {
				continue;
			}

			// Check if there are already constructors defined
			bool has_constructor = false;
			for (const auto& mem_func : struct_info->member_functions) {
				if (mem_func.is_constructor) {
					has_constructor = true;
					break;
				}
			}

			// Generate trivial default constructor if no constructor exists and it's not deleted
			if (!has_constructor && !struct_info->isDefaultConstructorDeleted()) {
				FLASH_LOG(Codegen, Debug, "Generating trivial constructor for ", type_name);

				// Use the pattern from visitConstructorDeclarationNode
				// Create function declaration for constructor
				FunctionDeclOp ctor_decl_op;
				ctor_decl_op.function_name = type_info->name();
				ctor_decl_op.struct_name = type_info->name();
				ctor_decl_op.return_type_index = TypeIndex{0, TypeCategory::Void};
				ctor_decl_op.return_size_in_bits = SizeInBits{0};
				ctor_decl_op.return_pointer_depth = PointerDepth{};
				ctor_decl_op.linkage = Linkage::CPlusPlus;
				ctor_decl_op.is_variadic = false;
				// Trivial constructors are implicitly inline (like constructors defined inside class body)
				ctor_decl_op.is_inline = true;

				// Generate mangled name for default constructor
				// Use style-aware mangling that properly handles constructors for both MSVC and Itanium
				std::vector<TypeSpecifierNode> empty_params;  // Explicit type to avoid ambiguity
				std::vector<std::string_view> empty_namespace_path;  // Explicit type to avoid ambiguity
				std::string_view class_name = StringTable::getStringView(type_info->name());

				// Use the appropriate mangling based on the style
				if (NameMangling::g_mangling_style == NameMangling::ManglingStyle::MSVC) {
					// MSVC uses dedicated constructor mangling (??0ClassName@@...)
					ctor_decl_op.mangled_name = StringTable::getOrInternStringHandle(
						NameMangling::generateMangledNameForConstructor(class_name, empty_params, empty_namespace_path)
					);
				} else if (NameMangling::g_mangling_style == NameMangling::ManglingStyle::Itanium) {
					// Itanium uses regular mangling with class name as function name (produces C1 marker)
					// Extract the last component for func_name (handles nested classes like "Outer::Inner")
					std::string_view func_name = class_name;
					auto last_colon = class_name.rfind("::");
					if (last_colon != std::string_view::npos) {
						func_name = class_name.substr(last_colon + 2);
					}
					TypeSpecifierNode void_return(Type::Void, TypeQualifier::None, 0);
					ctor_decl_op.mangled_name = StringTable::getOrInternStringHandle(NameMangling::generateMangledName(
						func_name,
						void_return,
						empty_params,
						false,  // not variadic
						class_name,  // struct_name
						empty_namespace_path,
						Linkage::CPlusPlus,
						false  // constructors are never const
					));
				} else {
					assert(false && "Unhandled name mangling type");
				}

				ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(ctor_decl_op), Token()));

				// Call base class constructors if any
				for (const auto& base : struct_info->base_classes) {
					auto base_type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(base.name));
					if (base_type_it != getTypesByNameMap().end()) {
						// Call base constructor if the base has user-defined constructors OR needs a trivial
						// default constructor (e.g., template-instantiated class with member default-
						// initializers but no explicit constructors). hasConstructor() covers the trivial
						// constructor case (needs_default_constructor==true) that arises with template
						// base classes like Base<T> resolved to a concrete type.
						const StructTypeInfo* base_struct_info = base_type_it->second->getStructInfo();
						if (base_struct_info && base_struct_info->hasConstructor()) {
							ConstructorCallOp call_op;
							call_op.struct_name = base_type_it->second->name();
							call_op.object = StringTable::getOrInternStringHandle("this");
							// No arguments for default constructor
							fillInDefaultConstructorArguments(call_op, *base_struct_info);
							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(call_op), Token()));
						}
					}
				}

				// Combine bitfield default initializers into single per-unit stores
				// (all default values are compile-time constants, so we can pre-combine them)
				{
					std::unordered_map<size_t, unsigned long long> combined_bitfield_values;
					std::unordered_set<size_t> bitfield_offsets;
					for (const auto& member : struct_info->members) {
						if (member.bitfield_width.has_value() && member.default_initializer.has_value()) {
							bitfield_offsets.insert(member.offset);
							unsigned long long val = 0;
							ConstExpr::EvaluationContext ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*member.default_initializer, ctx);
							if (eval_result.success()) {
								if (const auto* ull_val = std::get_if<unsigned long long>(&eval_result.value)) {
									val = *ull_val;
								} else if (const auto* ll_val = std::get_if<long long>(&eval_result.value)) {
									val = static_cast<unsigned long long>(*ll_val);
								} else if (const auto* b_val = std::get_if<bool>(&eval_result.value)) {
									val = *b_val ? 1ULL : 0ULL;
								}
							}
							size_t width = *member.bitfield_width;
							unsigned long long mask = (width < 64) ? ((1ULL << width) - 1) : ~0ULL;
							combined_bitfield_values[member.offset] |= ((val & mask) << member.bitfield_bit_offset);
						}
					}

					// Emit a single combined store for each bitfield storage unit
					for (auto offset : bitfield_offsets) {
						// Find any member at this offset to get type/size info
						for (const auto& member : struct_info->members) {
							if (member.offset == offset && member.bitfield_width.has_value()) {
								MemberStoreOp combined_store;
								combined_store.value.setType(member.memberType());
								combined_store.value.size_in_bits = SizeInBits{static_cast<int>(member.size * 8)};
								combined_store.value.value = combined_bitfield_values[offset];
								combined_store.object = StringTable::getOrInternStringHandle("this");
								combined_store.member_name = member.getName();
								combined_store.offset = static_cast<int>(offset);
								combined_store.ref_qualifier = CVReferenceQualifier::None;
								combined_store.struct_type_info = nullptr;
								// No bitfield_width — write the full combined value
								ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(combined_store), Token()));
								break;
							}
						}
					}
				}

				// Initialize non-bitfield members with default initializers
				for (const auto& member : struct_info->members) {
					if (member.bitfield_width.has_value()) continue; // handled above
					if (member.default_initializer.has_value()) {
						const ASTNode& init_node = member.default_initializer.value();
						if (init_node.has_value() && init_node.is<ExpressionNode>()) {
							// Use the default member initializer
							ExprResult init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
							// Extract just the value (third element of init_operands)
							// Verify we have at least 3 elements before accessing

							IrValue member_value;
							if (const auto* temp_var = std::get_if<TempVar>(&init_operands.value)) {
								member_value = *temp_var;
							} else if (const auto* ull_val = std::get_if<unsigned long long>(&init_operands.value)) {
								member_value = *ull_val;
							} else if (const auto* d_val = std::get_if<double>(&init_operands.value)) {
								member_value = *d_val;
							} else if (const auto* string = std::get_if<StringHandle>(&init_operands.value)) {
								member_value = *string;
							} else {
								member_value = 0ULL;  // fallback
							}

							MemberStoreOp member_store;
							member_store.value.setType(member.memberType());
							member_store.value.size_in_bits = SizeInBits{static_cast<int>(member.size * 8)};
							member_store.value.value = member_value;
							member_store.object = StringTable::getOrInternStringHandle("this");
							member_store.member_name = member.getName();
							member_store.offset = static_cast<int>(member.offset);
							member_store.ref_qualifier = ((member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
							member_store.struct_type_info = nullptr;
							member_store.bitfield_width = member.bitfield_width;
							member_store.bitfield_bit_offset = member.bitfield_bit_offset;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), Token()));
						}
					}
				}

				// Emit return
				emitVoidReturn(Token());
			}
		}
	}




// ── inline private helpers (IrGenerator_Visitors_TypeInit.cpp) ──
// Helper: resolve self-referential struct types in template instantiations.
// When a template member function references its own class (e.g., const W& in W<T>::operator+=),
// the type_index may point to the unfinalized template base. This resolves it to the
// enclosing instantiated struct's type_index by mutating `type` in-place.
// Important: only resolves when the unfinalized type's name matches the base name of the
// enclosing struct — avoids incorrectly resolving outer class references in nested classes.
void AstToIr::resolveSelfReferentialType(TypeSpecifierNode& type, TypeIndex enclosing_type_index) {
	if (type.category() == TypeCategory::Struct && type.type_index().is_valid() && type.type_index().index() < getTypeInfoCount()) {
		auto& ti = getTypeInfo(type.type_index());
		if (!ti.struct_info_ || ti.struct_info_->total_size == 0) {
			if (enclosing_type_index.index() < getTypeInfoCount()) {
				// Verify this is actually a self-reference by checking that the unfinalized
				// type's name matches the base name of the enclosing struct.
				// For template instantiations: W (unfinalized) matches W$hash (enclosing)
				// For nested classes: Outer (unfinalized) does NOT match Outer::Inner (enclosing)
				auto unfinalized_name = StringTable::getStringView(ti.name());
				auto enclosing_name = StringTable::getStringView(getTypeInfo(enclosing_type_index).name());

				// Extract the base name of the enclosing struct (strip template hash and nested class prefix)
				// Template hash: "Name$hash" -> "Name"
				// Nested class: "Outer::Inner" -> "Inner"
				auto base_name = enclosing_name;
				auto last_scope = base_name.rfind("::");
				if (last_scope != std::string_view::npos) {
					base_name = base_name.substr(last_scope + 2);
				}
				auto dollar_pos = base_name.find('$');
				if (dollar_pos != std::string_view::npos) {
					base_name = base_name.substr(0, dollar_pos);
				}

				if (unfinalized_name == base_name) {
					type.set_type_index(enclosing_type_index);
				}
			}
		}
	}
}



// Helper function to resolve template parameter size from struct name
// This is used by both ConstExpr evaluator and IR generation for sizeof(T)
// where T is a template parameter in a template class member function
size_t AstToIr::resolveTemplateSizeFromStructName(std::string_view struct_name) {
	// Parse the struct name to extract template arguments
	// e.g., "Container_int" -> T = int (4 bytes), "Processor_char" -> T = char (1 byte)
	// Pointer types have "P" suffix: "Container_intP" -> T = int* (8 bytes)
	// Reference types have "R" or "RR" suffix: "Container_intR" -> T = int& (sizeof returns size of int)
	size_t underscore_pos = struct_name.rfind('_');
	if (underscore_pos == std::string_view::npos || underscore_pos + 1 >= struct_name.size()) {
		return 0;
	}

	std::string_view type_suffix = struct_name.substr(underscore_pos + 1);

	// Strip CV qualifier prefixes ('C' for const, 'V' for volatile)
	// TemplateTypeArg::toString() adds CV qualifiers as prefixes (e.g., "Cint" for const int)
	// sizeof(const T) and sizeof(volatile T) return the same size as sizeof(T)
	while (!type_suffix.empty() && (type_suffix.front() == 'C' || type_suffix.front() == 'V')) {
		type_suffix = type_suffix.substr(1);
	}

	// Check for reference types (suffix ends with 'R' or 'RR')
	// TemplateTypeArg::toString() appends "R" for lvalue reference, "RR" for rvalue reference
	// sizeof(T&) and sizeof(T&&) return the size of T, not the size of the reference itself
	if (type_suffix.size() >= 2 && type_suffix.ends_with("RR")) {
		// Rvalue reference - strip "RR" and get base type size
		type_suffix = type_suffix.substr(0, type_suffix.size() - 2);
	} else if (!type_suffix.empty() && type_suffix.back() == 'R') {
		// Lvalue reference - strip "R" and get base type size
		type_suffix = type_suffix.substr(0, type_suffix.size() - 1);
	}

	// Check for pointer types (suffix ends with 'P')
	// TemplateTypeArg::toString() appends 'P' for each pointer level
	// e.g., "intP" for int*, "intPP" for int**, etc.
	if (!type_suffix.empty() && type_suffix.back() == 'P') {
		// All pointers are 8 bytes on x64
		return 8;
	}

	// Check for array types (suffix contains 'A')
	// Arrays are like "intA[10]" - sizeof(array) = element_size * element_count
	size_t array_pos = type_suffix.find('A');
	if (array_pos != std::string_view::npos) {
		// Extract base type and array dimensions
		std::string_view base_type = type_suffix.substr(0, array_pos);
		std::string_view array_part = type_suffix.substr(array_pos + 1); // Skip 'A'

		// Strip CV qualifiers from base_type (already stripped from type_suffix earlier, but double-check)
		while (!base_type.empty() && (base_type.front() == 'C' || base_type.front() == 'V')) {
			base_type = base_type.substr(1);
		}

		// Parse array dimensions like "[10]" or "[]"
		if (array_part.starts_with('[') && array_part.ends_with(']')) {
			std::string_view dimensions = array_part.substr(1, array_part.size() - 2);
			if (!dimensions.empty()) {
				// Parse the dimension as a number
				size_t array_count = 0;
				auto result = std::from_chars(dimensions.data(), dimensions.data() + dimensions.size(), array_count);
				if (result.ec == std::errc{} && array_count > 0) {
					// Get base type size
					size_t base_size = 0;

					// Check if base_type is a pointer (ends with 'P')
					// e.g., "intP" for int*, "charPP" for char**, etc.
					if (!base_type.empty() && base_type.back() == 'P') {
						// All pointers are 8 bytes on x64
						base_size = 8;
					} else {
						// Look up non-pointer base type size
						if (base_type == "int") base_size = 4;
						else if (base_type == "char") base_size = 1;
						else if (base_type == "short") base_size = 2;
						else if (base_type == "long") base_size = get_long_size_bits() / 8;
						else if (base_type == "float") base_size = 4;
						else if (base_type == "double") base_size = 8;
						else if (base_type == "bool") base_size = 1;
						else if (base_type == "uint") base_size = 4;
						else if (base_type == "uchar") base_size = 1;
						else if (base_type == "ushort") base_size = 2;
						else if (base_type == "ulong") base_size = get_long_size_bits() / 8;
						else if (base_type == "ulonglong") base_size = 8;
						else if (base_type == "longlong") base_size = 8;
					}

					if (base_size > 0) {
						return base_size * array_count;
					}
				}
			}
		}
		return 0;  // Failed to parse array dimensions
	}

	// Map common type suffixes to their sizes
	// Note: Must match the output of TemplateTypeArg::toString() in TemplateRegistry.h
	if (type_suffix == "int") return 4;
	else if (type_suffix == "char") return 1;
	else if (type_suffix == "short") return 2;
	else if (type_suffix == "long") return get_long_size_bits() / 8;
	else if (type_suffix == "float") return 4;
	else if (type_suffix == "double") return 8;
	else if (type_suffix == "bool") return 1;
	else if (type_suffix == "uint") return 4;
	else if (type_suffix == "uchar") return 1;
	else if (type_suffix == "ushort") return 2;
	else if (type_suffix == "ulong") return get_long_size_bits() / 8;
	else if (type_suffix == "ulonglong") return 8;
	else if (type_suffix == "longlong") return 8;

	return 0;  // Unknown type
}



// Recursively zero-initialize all scalar leaf members of a struct.
// For sub-members that are themselves structs (> 64 bits), recurse instead of
// emitting a single MemberStore with 0ULL (which would only zero the first 8 bytes).
void AstToIr::emitRecursiveZeroFill(
	const StructTypeInfo& struct_info,
	StringHandle base_object,
	int base_offset,
	const Token& token)
{
	for (const StructMember& sub_member : struct_info.members) {
		bool is_nested_struct = isIrStructType(toIrType(sub_member.memberType()))
			&& sub_member.type_index.index() < getTypeInfoCount()
			&& getTypeInfo(sub_member.type_index).struct_info_
			&& (sub_member.size * 8) > 64;

		if (is_nested_struct) {
			emitRecursiveZeroFill(
				*getTypeInfo(sub_member.type_index).struct_info_,
				base_object,
				base_offset + static_cast<int>(sub_member.offset),
				token);
		} else {
			MemberStoreOp member_store;
			member_store.value.setType(sub_member.memberType());
			member_store.value.size_in_bits = SizeInBits{static_cast<int>(sub_member.size * 8)};
			member_store.value.value = 0ULL;
			member_store.object = base_object;
			member_store.member_name = sub_member.getName();
			member_store.offset = base_offset + static_cast<int>(sub_member.offset);
			member_store.ref_qualifier = ((sub_member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((sub_member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
			member_store.struct_type_info = nullptr;
			ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
		}
	}
}



// Implementation of recursive nested member store generation
bool AstToIr::tryEmitArrayMemberStores(
const StructMember& member,
const InitializerListNode& init_list,
StringHandle base_object,
int base_offset,
const Token& token)
{
	if (!member.is_array || member.array_dimensions.empty()) {
		return false;
	}

	size_t element_count = 1;
	for (size_t dim : member.array_dimensions) {
		element_count *= dim;
	}
	if (element_count == 0) {
		return false;
	}

	int element_size_bits = 0;
	if (member.type_index.index() < getTypeInfoCount()) {
		const TypeInfo& elem_type_info = getTypeInfo(member.type_index);
		if (elem_type_info.struct_info_) {
			// Struct types store type_size_ in bytes
			element_size_bits = static_cast<int>(elem_type_info.type_size_ * 8);
		} else if (elem_type_info.type_size_ > 0) {
			// Non-struct types (enums, typedefs, etc.) store type_size_ in bits
			element_size_bits = static_cast<int>(elem_type_info.type_size_);
		}
	}
	if (element_size_bits <= 0) {
		element_size_bits = static_cast<int>((member.size * 8) / element_count);
	}

	auto count_expressions = [&](const auto& self, const InitializerListNode& list) -> size_t {
		size_t count = 0;
		for (const ASTNode& node : list.initializers()) {
			if (node.is<ExpressionNode>()) {
				count++;
			} else if (node.is<InitializerListNode>()) {
				count += self(self, node.as<InitializerListNode>());
			}
		}
		return count;
	};

	const size_t first_dimension_limit = member.array_dimensions[0];
	size_t nested_subarray_count = 0;
	const size_t subarray_limit = member.array_dimensions.size() > 1
		? (element_count / first_dimension_limit)
		: element_count;
	for (const ASTNode& node : init_list.initializers()) {
		if (node.is<InitializerListNode>()) {
			nested_subarray_count++;
			if (nested_subarray_count > first_dimension_limit) {
				throw CompileError("Too many initializers for array");
			}
			if (member.array_dimensions.size() > 1 &&
				count_expressions(count_expressions, node.as<InitializerListNode>()) > subarray_limit) {
				throw CompileError("Too many initializers for array subobject");
			}
		}
	}

	std::vector<const ExpressionNode*> flat_initializers;
	auto collect_initializers = [&](const auto& self, const InitializerListNode& list) -> void {
		for (const ASTNode& node : list.initializers()) {
			if (node.is<ExpressionNode>()) {
				flat_initializers.push_back(&node.as<ExpressionNode>());
			} else if (node.is<InitializerListNode>()) {
				self(self, node.as<InitializerListNode>());
			}
		}
	};
	collect_initializers(collect_initializers, init_list);
	if (flat_initializers.size() > element_count) {
		throw CompileError("Too many initializers for array");
	}

	const size_t emit_count = std::min(element_count, flat_initializers.size());
	for (size_t i = 0; i < emit_count; ++i) {
		ExprResult init_operands = visitExpressionNode(*flat_initializers[i]);

		emitArrayStore(
			typeToCategory(member.memberType()),
			element_size_bits,
			base_object,
			makeTypedValue(TypeCategory::Int, SizeInBits{32}, static_cast<unsigned long long>(i)),
			toTypedValue(init_operands),
			base_offset + static_cast<int>(member.offset),
			false,
			token
		);
	}

	// Zero-fill trailing uninitialized elements.
	// For struct-typed elements larger than 64 bits, a single ArrayStore with 0ULL
	// would only zero the first 8 bytes. Instead, recursively zero each sub-member.
	const bool is_struct_element = isIrStructType(toIrType(member.memberType()))
		&& member.type_index.index() < getTypeInfoCount()
		&& getTypeInfo(member.type_index).struct_info_
		&& element_size_bits > 64;

	for (size_t i = emit_count; i < element_count; ++i) {
		if (is_struct_element) {
			// Recursively zero each sub-member of the struct element.
			int element_byte_offset = base_offset
				+ static_cast<int>(member.offset)
				+ static_cast<int>(i) * (element_size_bits / 8);

			emitRecursiveZeroFill(*getTypeInfo(member.type_index).struct_info_,
				base_object, element_byte_offset, token);
		} else {
			auto zero_value = makeTypedValue(member.memberType(), SizeInBits{element_size_bits}, 0ULL);
			emitArrayStore(
				typeToCategory(member.memberType()),
				element_size_bits,
				base_object,
				makeTypedValue(TypeCategory::Int, SizeInBits{32}, static_cast<unsigned long long>(i)),
				zero_value,
				base_offset + static_cast<int>(member.offset),
				false,
				token
			);
		}
	}

	return true;
}


void AstToIr::generateNestedMemberStores(
const StructTypeInfo& struct_info,
const InitializerListNode& init_list,
StringHandle base_object,
int base_offset,
const Token& token)
{
	// Build map of member names to initializer expressions
	std::unordered_map<StringHandle, const ASTNode*> member_values;
	size_t positional_index = 0;
	const auto& initializers = init_list.initializers();

	for (size_t i = 0; i < initializers.size(); ++i) {
		if (init_list.is_designated(i)) {
			member_values[init_list.member_name(i)] = &initializers[i];
		} else if (positional_index < struct_info.members.size()) {
			StringHandle member_name = struct_info.members[positional_index].getName();
			member_values[member_name] = &initializers[i];
			positional_index++;
		}
	}

	// Process each struct member
	for (const StructMember& member : struct_info.members) {
		StringHandle member_name = member.getName();

		if (!member_values.count(member_name)) {
			// Zero-initialize unspecified members
			MemberStoreOp member_store;
			member_store.value.setType(member.memberType());
			member_store.value.size_in_bits = SizeInBits{static_cast<int>(member.size * 8)};
			member_store.value.value = 0ULL;
			member_store.object = base_object;
			member_store.member_name = member_name;
			member_store.offset = base_offset + static_cast<int>(member.offset);
			member_store.ref_qualifier = ((member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
			member_store.struct_type_info = nullptr;
			ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
			continue;
		}

		const ASTNode& init_expr = *member_values[member_name];

		if (init_expr.is<InitializerListNode>()) {
			// Nested brace initializer - check if member is a struct
			const InitializerListNode& nested_init_list = init_expr.as<InitializerListNode>();

			if (tryEmitArrayMemberStores(member, nested_init_list, base_object, base_offset, token)) {
				continue;
			}

			if (member.type_index.index() < getTypeInfoCount()) {
				const TypeInfo& member_type_info = getTypeInfo(member.type_index);

				if (member_type_info.struct_info_ && !member_type_info.struct_info_->members.empty()) {
					// RECURSIVE CALL for nested struct
					generateNestedMemberStores(
					*member_type_info.struct_info_,
					nested_init_list,
					base_object,
					base_offset + static_cast<int>(member.offset),
					token
					);
					continue;
				}
			}

			// Not a struct type - try to extract single value from single-element list
			const auto& nested_initializers = nested_init_list.initializers();
			if (nested_initializers.size() == 1 && nested_initializers[0].is<ExpressionNode>()) {
				ExprResult init_operands = visitExpressionNode(nested_initializers[0].as<ExpressionNode>());
				IrValue member_value = 0ULL;
					if (const auto* temp_var = std::get_if<TempVar>(&init_operands.value)) {
						member_value = *temp_var;
					} else if (const auto* ull_val = std::get_if<unsigned long long>(&init_operands.value)) {
						member_value = *ull_val;
					} else if (const auto* d_val = std::get_if<double>(&init_operands.value)) {
						member_value = *d_val;
					} else if (const auto* string = std::get_if<StringHandle>(&init_operands.value)) {
						member_value = *string;
					}

				MemberStoreOp member_store;
				member_store.value.setType(member.memberType());
				member_store.value.size_in_bits = SizeInBits{static_cast<int>(member.size * 8)};
				member_store.value.value = member_value;
				member_store.object = base_object;
				member_store.member_name = member_name;
				member_store.offset = base_offset + static_cast<int>(member.offset);
				member_store.ref_qualifier = ((member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
				member_store.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
			} else {
				// Zero-initialize if we can't extract a value
				MemberStoreOp member_store;
				member_store.value.setType(member.memberType());
				member_store.value.size_in_bits = SizeInBits{static_cast<int>(member.size * 8)};
				member_store.value.value = 0ULL;
				member_store.object = base_object;
				member_store.member_name = member_name;
				member_store.offset = base_offset + static_cast<int>(member.offset);
				member_store.ref_qualifier = ((member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
				member_store.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
			}
		} else if (init_expr.is<ExpressionNode>()) {
			// Direct expression initializer
			ExprResult init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
			IrValue member_value = 0ULL;
				if (const auto* temp_var = std::get_if<TempVar>(&init_operands.value)) {
					member_value = *temp_var;
				} else if (const auto* ull_val = std::get_if<unsigned long long>(&init_operands.value)) {
					member_value = *ull_val;
				} else if (const auto* d_val = std::get_if<double>(&init_operands.value)) {
					member_value = *d_val;
				} else if (const auto* string = std::get_if<StringHandle>(&init_operands.value)) {
					member_value = *string;
				}

			MemberStoreOp member_store;
			member_store.value.setType(member.memberType());
			member_store.value.size_in_bits = SizeInBits{static_cast<int>(member.size * 8)};
			member_store.value.value = member_value;
			member_store.object = base_object;
			member_store.member_name = member_name;
			member_store.offset = base_offset + static_cast<int>(member.offset);
			member_store.ref_qualifier = ((member.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((member.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
			member_store.struct_type_info = nullptr;
			ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
		}
	}
}



// Generate just the function declaration for a template instantiation (without body)
// This is called immediately when a template call is detected, so the IR converter
// knows the full function signature before the call is converted to object code
void AstToIr::generateTemplateFunctionDecl(const TemplateInstantiationInfo& inst_info) {
	const FunctionDeclarationNode& template_func_decl = inst_info.template_node_ptr->function_decl_node();
	const DeclarationNode& template_decl = template_func_decl.decl_node();

	// Create mangled name token
	Token mangled_token(
		Token::Type::Identifier,
		StringTable::getStringView(inst_info.mangled_name),
		template_decl.identifier_token().line(),
		template_decl.identifier_token().column(),
		template_decl.identifier_token().file_index()
	);

	StringHandle full_func_name = inst_info.mangled_name;
	StringHandle struct_name = inst_info.struct_name;

	// Generate function declaration IR using typed payload
	FunctionDeclOp func_decl_op;

	// Add return type
	const TypeSpecifierNode& return_type = template_decl.type_node().as<TypeSpecifierNode>();
	func_decl_op.return_type_index = return_type.type_index();
	func_decl_op.return_size_in_bits = SizeInBits{static_cast<int>(return_type.size_in_bits())};
	func_decl_op.return_pointer_depth = PointerDepth{static_cast<int>(return_type.pointer_depth())};

	// Add function name and struct name
	func_decl_op.function_name = full_func_name;
	func_decl_op.struct_name = struct_name;

	// Add linkage (C++)
	func_decl_op.linkage = Linkage::None;

	// Add variadic flag (template functions are typically not variadic, but check anyway)
	func_decl_op.is_variadic = template_func_decl.is_variadic();

	// Mangled name is the full function name (already stored in StringBuilder's stable storage)
	func_decl_op.mangled_name = full_func_name;

	// Add function parameters with concrete types
	size_t template_unnamed_param_counter = 0;
	for (size_t i = 0; i < template_func_decl.parameter_nodes().size(); ++i) {
		const auto& param_node = template_func_decl.parameter_nodes()[i];
		if (param_node.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();

			FunctionParam func_param;
			// Use concrete type if this parameter uses a template parameter
			if (i < inst_info.template_args.size()) {
				Type concrete_type = inst_info.template_args[i];
				func_param.type_index = TypeIndex::fromTypeAndIndex(concrete_type, {});
				func_param.size_in_bits = SizeInBits{get_type_size_bits(concrete_type)};
				func_param.pointer_depth = PointerDepth{};  // pointer depth
			} else {
				// Use original parameter type
				const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				func_param.type_index = param_type.type_index();
				func_param.size_in_bits = SizeInBits{param_type.size_in_bits()};
				func_param.pointer_depth = PointerDepth{static_cast<int>(param_type.pointer_depth())};
			}

			// Handle empty parameter names
			std::string_view param_name = param_decl.identifier_token().value();
			if (param_name.empty()) {
				func_param.name = StringTable::getOrInternStringHandle(
					StringBuilder().append("__param_").append(template_unnamed_param_counter++).commit());
			} else {
				func_param.name = StringTable::getOrInternStringHandle(param_name);
			}

			func_param.ref_qualifier = CVReferenceQualifier::None;
			func_param.cv_qualifier = CVQualifier::None;
			func_decl_op.parameters.push_back(func_param);
		}
	}

	// Emit function declaration IR (declaration only, no body)
	ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), mangled_token));
}


// Generate an instantiated member function template
void AstToIr::generateTemplateInstantiation(const TemplateInstantiationInfo& inst_info) {
	auto saved_namespace_stack = current_namespace_stack_;
	auto parse_namespace_components = [](std::string_view qualified_prefix) {
		std::vector<std::string> components;
		size_t start = 0;
		while (start < qualified_prefix.size()) {
			size_t sep = qualified_prefix.find("::", start);
			if (sep == std::string_view::npos) {
				components.emplace_back(qualified_prefix.substr(start));
				break;
			}
			components.emplace_back(qualified_prefix.substr(start, sep - start));
			start = sep + 2;
		}
		return components;
	};
	auto extract_namespace_prefix = [](std::string_view qualified_name) -> std::string_view {
		size_t scope_pos = qualified_name.rfind("::");
		if (scope_pos == std::string_view::npos) {
			return {};
		}
		return qualified_name.substr(0, scope_pos);
	};

	std::string_view namespace_source;
	if (inst_info.struct_name.isValid()) {
		namespace_source = extract_namespace_prefix(StringTable::getStringView(inst_info.struct_name));
	} else {
		namespace_source = extract_namespace_prefix(StringTable::getStringView(inst_info.qualified_template_name));
	}
	if (!namespace_source.empty()) {
		current_namespace_stack_ = parse_namespace_components(namespace_source);
	} else {
		current_namespace_stack_.clear();
	}

	// First, generate the FunctionDecl IR for the template instantiation
	// This must be done at the top level, BEFORE any function bodies that might call it
	generateTemplateFunctionDecl(inst_info);

	// Get the template function declaration
	const FunctionDeclarationNode& template_func_decl = inst_info.template_node_ptr->function_decl_node();
	const DeclarationNode& template_decl = template_func_decl.decl_node();

	// Create mangled name token
	Token mangled_token(
		Token::Type::Identifier,
		StringTable::getStringView(inst_info.mangled_name),
		template_decl.identifier_token().line(),
		template_decl.identifier_token().column(),
		template_decl.identifier_token().file_index()
	);

	// Enter function scope
	symbol_table.enter_scope(ScopeType::Function);

	// Get struct type info for member functions
	const TypeInfo* struct_type_info = nullptr;
	if (inst_info.struct_name.isValid()) {
		auto struct_type_it = getTypesByNameMap().find(inst_info.struct_name);
		if (struct_type_it != getTypesByNameMap().end()) {
			struct_type_info = struct_type_it->second;
		}
	}

	// For member functions, add implicit 'this' pointer to symbol table
	// This is needed so member variable access works during template body parsing
	if (struct_type_info) {
		// Create a 'this' pointer type (pointer to the struct)
		auto this_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			TypeCategory::Struct,
			struct_type_info->type_index_,
			64,  // Pointer size in bits
			template_decl.identifier_token(),
			CVQualifier::None,
			ReferenceQualifier::None
		);

		// Set pointer depth to 1 (this is a pointer)
		this_type_node.as<TypeSpecifierNode>().add_pointer_level(CVQualifier::None);

		// Create 'this' declaration
		Token this_token(Token::Type::Identifier, "this"sv,
			template_decl.identifier_token().line(),
			template_decl.identifier_token().column(),
			template_decl.identifier_token().file_index());
		auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type_node, this_token);

		// Add 'this' to symbol table
		symbol_table.insert("this"sv, this_decl);
	}

	// Add function parameters to symbol table for name resolution during body parsing
	for (size_t i = 0; i < template_func_decl.parameter_nodes().size(); ++i) {
		const auto& param_node = template_func_decl.parameter_nodes()[i];
		if (param_node.is<DeclarationNode>()) {
			const DeclarationNode& param_decl = param_node.as<DeclarationNode>();

			// Create declaration with concrete type
			if (i < inst_info.template_args.size()) {
				Type concrete_type = inst_info.template_args[i];
				auto concrete_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
					concrete_type,
					TypeQualifier::None,
					get_type_size_bits(concrete_type),
					param_decl.identifier_token()
				);
				auto concrete_param_decl = ASTNode::emplace_node<DeclarationNode>(concrete_type_node, param_decl.identifier_token());
				symbol_table.insert(param_decl.identifier_token().value(), concrete_param_decl);
			} else {
				symbol_table.insert(param_decl.identifier_token().value(), param_node);
			}
		}
	}

	// Parse the template body with concrete types
	// Pass the struct name and type index so the parser can set up member function context
	auto body_node_opt = parser_->parseTemplateBody(
		inst_info.body_position,
		inst_info.template_param_names,
		inst_info.template_args,
		inst_info.struct_name.isValid() ? inst_info.struct_name : StringHandle(),  // Pass struct name
		struct_type_info ? struct_type_info->type_index_ : TypeIndex{}  // Pass type index
	);

	if (body_node_opt.has_value()) {
		if (body_node_opt->is<BlockNode>()) {
			const BlockNode& block = body_node_opt->as<BlockNode>();
			const auto& stmts = block.get_statements();

			// Visit each statement in the block to generate IR
			for (size_t i = 0; i < stmts.size(); ++i) {
				visit(stmts[i]);
			}
		}
	} else {
		std::cerr << "Warning: Template body does NOT have value!\n";
	}

	// Add implicit return for void functions
	const TypeSpecifierNode& return_type = template_decl.type_node().as<TypeSpecifierNode>();
	if (return_type.category() == TypeCategory::Void) {
		ReturnOp ret_op;  // No return value for void
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), mangled_token));
	}

	// Exit function scope
	symbol_table.exit_scope();
	current_namespace_stack_ = saved_namespace_stack;
}



ExprResult AstToIr::generateTemplateParameterReferenceIr(const TemplateParameterReferenceNode& templateParamRefNode) {
	// This should not happen during normal code generation - template parameters should be substituted
	// during template instantiation. If we get here, it means template instantiation failed.
	std::string param_name = std::string(templateParamRefNode.param_name().view());
	std::cerr << "Error: Template parameter '" << param_name << "' was not substituted during template instantiation\n";
	std::cerr << "This indicates a bug in template instantiation - template parameters should be replaced with concrete types/values\n";
	assert(false && "Template parameter reference found during code generation - should have been substituted");
	return ExprResult{};
}
