#include "CodeGen.h"

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
			// Template declarations don't generate code yet - they're stored for later instantiation
			// TODO: Implement template instantiation in Phase 2
			return;
		}
		else if (node.is<TemplateClassDeclarationNode>()) {
			// Template class declarations don't generate code yet - they're stored for later instantiation
			// TODO: Implement class template instantiation in Phase 6
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
		else if (node.is<ExpressionNode>()) {
			// Expression statement (e.g., function call, lambda expression, etc.)
			// Evaluate the expression but discard the result
			visitExpressionNode(node.as<ExpressionNode>());
		}
		else if (node.is<LambdaExpressionNode>()) {
			// Lambda expression as a statement
			// Evaluate the lambda (creates closure instance) but discard the result
			generateLambdaExpressionIr(node.as<LambdaExpressionNode>());
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
		while (processed_count < collected_lambdas_.size()) {
			// Process from the end (newly added lambdas) backwards
			size_t current_size = collected_lambdas_.size();
			for (size_t i = current_size; i > processed_count; --i) {
				// CRITICAL: Copy the LambdaInfo before calling generateLambdaFunctions
				// because that function may push new lambdas which can reallocate the vector
				// and invalidate any references
				LambdaInfo lambda_info = collected_lambdas_[i - 1];
				// Skip if this lambda has already been generated (prevents duplicate definitions)
				if (generated_lambda_ids_.find(lambda_info.lambda_id) != generated_lambda_ids_.end()) {
					continue;
				}
				generated_lambda_ids_.insert(lambda_info.lambda_id);
				generateLambdaFunctions(lambda_info);
			}
			processed_count = current_size;
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
					if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(info.struct_name, member_handle)) {
						auto lazy_info_opt = LazyMemberInstantiationRegistry::getInstance().getLazyMemberInfo(info.struct_name, member_handle);
						if (lazy_info_opt.has_value()) {
							auto new_func_node = parser_->instantiateLazyMemberFunction(*lazy_info_opt);
							if (new_func_node.has_value() && new_func_node->is<FunctionDeclarationNode>()) {
								LazyMemberInstantiationRegistry::getInstance().markInstantiated(info.struct_name, member_handle);
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
			
			auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);
			if (!eval_result.success()) {
				return false;
			}
			
			if (std::holds_alternative<unsigned long long>(eval_result.value)) {
				out_value = std::get<unsigned long long>(eval_result.value);
				return true;
			}
			if (std::holds_alternative<long long>(eval_result.value)) {
				out_value = static_cast<unsigned long long>(std::get<long long>(eval_result.value));
				return true;
			}
			if (std::holds_alternative<bool>(eval_result.value)) {
				out_value = std::get<bool>(eval_result.value) ? 1ULL : 0ULL;
				return true;
			}
			if (std::holds_alternative<double>(eval_result.value)) {
				double d = std::get<double>(eval_result.value);
				out_value = static_cast<unsigned long long>(d);
				return true;
			}
			
			return false;
		};
		
		for (const auto& [type_name, type_info] : gTypesByName) {
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
			// (same struct can be registered under multiple keys in gTypesByName)
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
					op.type = static_member.type;
					op.size_in_bits = static_cast<int>(static_member.size * 8);
					// If size is 0 for struct types, look up from type info
					if (op.size_in_bits == 0 && static_member.type_index > 0 && static_member.type_index < gTypeInfo.size()) {
						const StructTypeInfo* member_si = gTypeInfo[static_member.type_index].getStructInfo();
						if (member_si) {
							op.size_in_bits = static_cast<int>(member_si->total_size * 8);
						}
					}
					op.var_name = name_handle;  // Phase 3: Now using StringHandle instead of string_view

					// Check if static member has an initializer
					op.is_initialized = static_member.initializer.has_value() || unresolved_identifier_initializer;
					auto zero_initialize = [&]() {
						size_t byte_count = op.size_in_bits / 8;
						for (size_t i = 0; i < byte_count; ++i) {
							op.init_data.push_back(0);
						}
					};
						if (unresolved_identifier_initializer) {
							FLASH_LOG(Codegen, Debug, "Initializer unresolved; zero-initializing static member '", qualified_name, "'");
							zero_initialize();
						} else if (op.is_initialized) {
							if (!static_member.initializer->is<ExpressionNode>()) {
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
								if (ctor_type_index < gTypeInfo.size()) {
									const StructTypeInfo* ctor_struct_info = gTypeInfo[ctor_type_index].getStructInfo();
									if (ctor_struct_info) {
										// Find matching constructor
										const ConstructorDeclarationNode* matching_ctor = nullptr;
										for (const auto& mf : ctor_struct_info->member_functions) {
											if (!mf.is_constructor || !mf.function_decl.is<ConstructorDeclarationNode>()) continue;
											const auto& ctor = mf.function_decl.as<ConstructorDeclarationNode>();
											if (ctor.parameter_nodes().size() == ctor_call.arguments().size()) {
												matching_ctor = &ctor;
												break;
											}
										}
										if (matching_ctor) {
											// Evaluate arguments
											ConstExpr::EvaluationContext eval_ctx(*global_symbol_table_);
											std::unordered_map<std::string_view, long long> param_values;
											bool args_ok = true;
											const auto& params = matching_ctor->parameter_nodes();
											for (size_t ai = 0; ai < params.size() && ai < ctor_call.arguments().size(); ++ai) {
												if (params[ai].is<DeclarationNode>()) {
													auto arg_result = ConstExpr::Evaluator::evaluate(ctor_call.arguments()[ai], eval_ctx);
													if (arg_result.success()) {
														param_values[params[ai].as<DeclarationNode>().identifier_token().value()] = arg_result.as_int();
													} else {
														args_ok = false;
														break;
													}
												}
											}
											if (args_ok) {
												// Evaluate each member's value from constructor initializer list
												size_t total_bytes = op.size_in_bits / 8;
												op.init_data.resize(total_bytes, 0);
												for (const auto& member : ctor_struct_info->members) {
													long long member_val = 0;
													for (const auto& mem_init : matching_ctor->member_initializers()) {
														if (mem_init.member_name == StringTable::getStringView(member.getName())) {
															// Try identifier lookup in param_values first
															if (mem_init.initializer_expr.is<ExpressionNode>()) {
																const auto& init_e = mem_init.initializer_expr.as<ExpressionNode>();
																if (std::holds_alternative<IdentifierNode>(init_e)) {
																	auto it = param_values.find(std::get<IdentifierNode>(init_e).name());
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
							size_t byte_count = op.size_in_bits / 8;
							for (size_t i = 0; i < byte_count; ++i) {
								op.init_data.push_back(0);
							}
						}
					} else if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
						const auto& bool_lit = std::get<BoolLiteralNode>(init_expr);
						FLASH_LOG(Codegen, Debug, "Processing BoolLiteralNode initializer for static member '", 
						qualified_name, "' value=", bool_lit.value() ? "true" : "false");
						unsigned long long value = bool_lit.value() ? 1ULL : 0ULL;
						size_t byte_count = op.size_in_bits / 8;
						for (size_t i = 0; i < byte_count; ++i) {
							op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
						}
						FLASH_LOG(Codegen, Debug, "  Wrote ", byte_count, " bytes to init_data");
					} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
						FLASH_LOG(Codegen, Debug, "Processing NumericLiteralNode initializer for static member '", 
						qualified_name, "'");
						// Evaluate the initializer expression
						auto init_operands = visitExpressionNode(init_expr);
						// Convert to raw bytes
						if (init_operands.size() >= 3) {
							unsigned long long value = 0;
							if (std::holds_alternative<unsigned long long>(init_operands[2])) {
								value = std::get<unsigned long long>(init_operands[2]);
								FLASH_LOG(Codegen, Debug, "  Extracted uint64 value: ", value);
							} else if (std::holds_alternative<double>(init_operands[2])) {
								double d = std::get<double>(init_operands[2]);
								std::memcpy(&value, &d, sizeof(double));
								FLASH_LOG(Codegen, Debug, "  Extracted double value: ", d);
							}
							size_t byte_count = op.size_in_bits / 8;
							for (size_t i = 0; i < byte_count; ++i) {
								op.init_data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
							}
							FLASH_LOG(Codegen, Debug, "  Wrote ", byte_count, " bytes to init_data");
						} else {
							FLASH_LOG(Codegen, Debug, "  WARNING: init_operands.size() = ", init_operands.size(), " (expected >= 3)");
						}
					} else if (std::holds_alternative<TemplateParameterReferenceNode>(init_expr)) {
						FLASH_LOG(Codegen, Debug, "WARNING: Processing TemplateParameterReferenceNode initializer for static member '", 
						qualified_name, "' - should have been substituted!");
						// Try to evaluate anyway
						auto init_operands = visitExpressionNode(init_expr);
						if (init_operands.size() >= 3) {
							unsigned long long value = 0;
							if (std::holds_alternative<unsigned long long>(init_operands[2])) {
								value = std::get<unsigned long long>(init_operands[2]);
							} else if (std::holds_alternative<double>(init_operands[2])) {
								double d = std::get<double>(init_operands[2]);
								std::memcpy(&value, &d, sizeof(double));
							}
							size_t byte_count = op.size_in_bits / 8;
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
								size_t byte_count = op.size_in_bits / 8;
								for (size_t i = 0; i < byte_count; ++i) {
									op.init_data.push_back(0);
								}
								FLASH_LOG(Codegen, Debug, "  Set reloc_target='", id.name(), "' for reference static member");
							} else {
							// Evaluate the initializer expression
							auto init_operands = visitExpressionNode(init_expr);
							if (init_operands.size() >= 3) {
								unsigned long long value = 0;
								if (std::holds_alternative<unsigned long long>(init_operands[2])) {
									value = std::get<unsigned long long>(init_operands[2]);
								} else if (std::holds_alternative<double>(init_operands[2])) {
									double d = std::get<double>(init_operands[2]);
									std::memcpy(&value, &d, sizeof(double));
								}
								size_t byte_count = op.size_in_bits / 8;
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
									size_t byte_count = op.size_in_bits / 8;
									for (size_t i = 0; i < byte_count; ++i) {
										op.init_data.push_back(0);
									}
								} else {
									FLASH_LOG(Codegen, Debug, "Address-of non-identifier for static member '",
									qualified_name, "' - zero-initializing");
									append_bytes(0, op.size_in_bits, op.init_data);
								}
							} else {
								// Other unary operators - try constexpr evaluation
								unsigned long long evaluated_value = 0;
								if (evaluate_static_initializer(*static_member.initializer, evaluated_value, struct_info)) {
									append_bytes(evaluated_value, op.size_in_bits, op.init_data);
								} else {
									append_bytes(0, op.size_in_bits, op.init_data);
								}
							}
						} else {
							unsigned long long evaluated_value = 0;
							if (evaluate_static_initializer(*static_member.initializer, evaluated_value, struct_info)) {
								FLASH_LOG(Codegen, Debug, "Evaluated constexpr initializer for static member '", 
								qualified_name, "' = ", evaluated_value);
								append_bytes(evaluated_value, op.size_in_bits, op.init_data);
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
											append_bytes(evaluated_value, op.size_in_bits, op.init_data);
											resolved_via_lazy = true;
										}
									}
								}
								if (!resolved_via_lazy) {
									FLASH_LOG(Codegen, Debug, "Processing unknown expression type initializer for static member '", 
									qualified_name, "' - skipping evaluation");
									// For unknown expression types, skip evaluation to avoid crashes
									// Initialize to zero as a safe default
									append_bytes(0, op.size_in_bits, op.init_data);
								}
							}
						}
					}
				}
				ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), Token()));
			}
		}
			
			// Also check if this struct inherits static members from base classes
			// and generate alias definitions if needed (Phase 3: Generate ALL inherited static members)
			if (!struct_info->base_classes.empty()) {
				for (const auto& base : struct_info->base_classes) {
					if (base.type_index >= gTypeInfo.size()) {
						continue;
					}
					
					const TypeInfo& base_type = gTypeInfo[base.type_index];
					const StructTypeInfo* base_info = base_type.getStructInfo();
					
					// If base_type is a type alias (no struct_info), follow type_index_ to get the actual struct
					// This handles cases like `struct Test : wrapper<true_type>::type` where `::type` is a type alias
					if (!base_info && base_type.type_index_ != base.type_index && base_type.type_index_ < gTypeInfo.size()) {
						const TypeInfo& resolved_type = gTypeInfo[base_type.type_index_];
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
							auto actual_struct_it = gTypesByName.find(StringTable::getOrInternStringHandle(actual_struct_name));
							if (actual_struct_it != gTypesByName.end()) {
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
								if (base_spec.type_index < gTypeInfo.size()) {
									const TypeInfo& base_type_info = gTypeInfo[base_spec.type_index];
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
							alias_op.type = static_member_ptr->type;
							alias_op.size_in_bits = static_cast<int>(static_member_ptr->size * 8);
							alias_op.var_name = derived_name_handle;
							alias_op.is_initialized = true;
							
							// Evaluate the initializer to get the value
							bool found_base_value = false;
							unsigned long long inferred_value = 0;
							
							if (static_member_ptr->initializer.has_value() && 
							static_member_ptr->initializer->is<ExpressionNode>()) {
								const ExpressionNode& init_expr = static_member_ptr->initializer->as<ExpressionNode>();
								
								if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
									const auto& bool_lit = std::get<BoolLiteralNode>(init_expr);
									inferred_value = bool_lit.value() ? 1ULL : 0ULL;
									found_base_value = true;
									FLASH_LOG(Codegen, Debug, "Found bool literal value: ", bool_lit.value());
								} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
									auto init_operands = visitExpressionNode(init_expr);
									if (init_operands.size() >= 3 && std::holds_alternative<unsigned long long>(init_operands[2])) {
										inferred_value = std::get<unsigned long long>(init_operands[2]);
										found_base_value = true;
										FLASH_LOG(Codegen, Debug, "Found numeric literal value: ", inferred_value);
									} else if (init_operands.size() >= 3 && std::holds_alternative<double>(init_operands[2])) {
										double d = std::get<double>(init_operands[2]);
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
							append_bytes(inferred_value, alias_op.size_in_bits, alias_op.init_data);
							
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
		
		for (const auto& [type_name, type_info] : gTypesByName) {
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
				ctor_decl_op.return_type = Type::Void;
				ctor_decl_op.return_size_in_bits = 0;
				ctor_decl_op.return_pointer_depth = 0;
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
						Linkage::CPlusPlus
					));
				} else {
					assert(false && "Unhandled name mangling type");
				}

				ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(ctor_decl_op), Token()));
				
				// Call base class constructors if any
				for (const auto& base : struct_info->base_classes) {
					auto base_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(base.name));
					if (base_type_it != gTypesByName.end()) {
						// Only call base constructor if the base class actually has constructors
						// This avoids link errors when inheriting from classes without constructors
						const StructTypeInfo* base_struct_info = base_type_it->second->getStructInfo();
						if (base_struct_info && base_struct_info->hasAnyConstructor()) {
							ConstructorCallOp call_op;
							call_op.struct_name = base_type_it->second->name();
							call_op.object = StringTable::getOrInternStringHandle("this");
							// No arguments for default constructor
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
								if (std::holds_alternative<unsigned long long>(eval_result.value)) {
									val = std::get<unsigned long long>(eval_result.value);
								} else if (std::holds_alternative<long long>(eval_result.value)) {
									val = static_cast<unsigned long long>(std::get<long long>(eval_result.value));
								} else if (std::holds_alternative<bool>(eval_result.value)) {
									val = std::get<bool>(eval_result.value) ? 1ULL : 0ULL;
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
								combined_store.value.type = member.type;
								combined_store.value.size_in_bits = static_cast<int>(member.size * 8);
								combined_store.value.value = combined_bitfield_values[offset];
								combined_store.object = StringTable::getOrInternStringHandle("this");
								combined_store.member_name = member.getName();
								combined_store.offset = static_cast<int>(offset);
								combined_store.is_reference = false;
								combined_store.is_rvalue_reference = false;
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
							auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
							// Extract just the value (third element of init_operands)
							// Verify we have at least 3 elements before accessing
							if (init_operands.size() < 3) {
								FLASH_LOG(Codegen, Warning, "Default initializer expression returned fewer than 3 operands");
								continue;
							}
							
							IrValue member_value;
							if (std::holds_alternative<TempVar>(init_operands[2])) {
								member_value = std::get<TempVar>(init_operands[2]);
							} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
								member_value = std::get<unsigned long long>(init_operands[2]);
							} else if (std::holds_alternative<double>(init_operands[2])) {
								member_value = std::get<double>(init_operands[2]);
							} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
								member_value = std::get<StringHandle>(init_operands[2]);
							} else {
								member_value = 0ULL;  // fallback
							}
							
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = StringTable::getOrInternStringHandle("this");
							member_store.member_name = member.getName();
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference();
							member_store.is_rvalue_reference = member.is_rvalue_reference();
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

