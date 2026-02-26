class AstToIr {
public:
	AstToIr() = delete;  // Require valid references
	AstToIr(SymbolTable& global_symbol_table, CompileContext& context, Parser& parser)
		: global_symbol_table_(&global_symbol_table), context_(&context), parser_(&parser) {
		// Generate static member declarations for template classes before processing AST
		generateStaticMemberDeclarations();
		// Generate trivial default constructors for structs that need them
		generateTrivialDefaultConstructors();
	}

	void visit(const ASTNode& node) {
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

	const Ir& getIr() const { return ir_; }

	// Generate all collected lambdas (must be called after visiting all nodes)
	void generateCollectedLambdas() {
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
	
	// Generate all collected local struct member functions
	void generateCollectedLocalStructMembers() {
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
	
	// Generate deferred member functions discovered during function call resolution.
	// Uses a worklist approach since generated functions may call other ungenerated functions.
	// Deduplication is handled by visitFunctionDeclarationNode via generated_function_names_,
	// which skips any function whose mangled name has already been emitted.
	void generateDeferredMemberFunctions() {
		size_t processed = 0;
		while (processed < deferred_member_functions_.size()) {
		DeferredMemberFunctionInfo info = deferred_member_functions_[processed++];
			StringHandle saved_function = current_function_name_;
			auto saved_namespace = current_namespace_stack_;
			current_struct_name_ = info.struct_name;
			current_function_name_ = StringHandle();
			current_namespace_stack_ = info.namespace_stack;
			
			
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
			
			current_function_name_ = saved_function;
			current_namespace_stack_ = saved_namespace;
		}
	}
	
	// Generate all collected template instantiations (must be called after visiting all nodes)
	void generateCollectedTemplateInstantiations() {
		for (const auto& inst_info : collected_template_instantiations_) {
			generateTemplateInstantiation(inst_info);
		}
	}

	// Reserve space for IR instructions (optimization)
	void reserveInstructions(size_t capacity) {
		ir_.reserve(capacity);
	}

	// Generate GlobalVariableDecl for all static members in all registered types
	// This is called at the beginning of IR generation to ensure all template
	// instantiation static members are emitted
	void generateStaticMemberDeclarations() {
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

	// Generate trivial default constructors for structs that need them
	// This handles template instantiations like Tuple<> that have no user-defined constructors
	void generateTrivialDefaultConstructors() {
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
							member_store.is_reference = member.is_reference;
							member_store.is_rvalue_reference = member.is_rvalue_reference;
							member_store.struct_type_info = nullptr;
							member_store.bitfield_width = member.bitfield_width;
							member_store.bitfield_bit_offset = member.bitfield_bit_offset;
							
							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), Token()));
						}
					}
				}
				
				// Emit return
				ReturnOp ret_op;
				// ReturnOp fields: return_value (optional), return_type (optional), return_size
				// For void constructor, leave return_value as nullopt
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), Token()));
			}
		}
	}

private:
	// Helper function to resolve template parameter size from struct name
	// This is used by both ConstExpr evaluator and IR generation for sizeof(T)
	// where T is a template parameter in a template class member function
	static size_t resolveTemplateSizeFromStructName(std::string_view struct_name) {
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

	// Helper function to try evaluating sizeof/alignof using ConstExprEvaluator
	// Returns the evaluated operands if successful, empty vector otherwise
	template<typename NodeType>
	std::vector<IrOperand> tryEvaluateAsConstExpr(const NodeType& node) {
		// Try to evaluate as a constant expression first
		ConstExpr::EvaluationContext ctx(symbol_table);
		
		// Pass global symbol table for resolving global variables in sizeof etc.
		if (global_symbol_table_) {
			ctx.global_symbols = global_symbol_table_;
		}
		
		// If we're in a member function, set the struct_info in the context
		// This allows sizeof(T) to resolve template parameters from the struct
		if (current_struct_name_.isValid()) {
			auto struct_type_it = gTypesByName.find(current_struct_name_);
			if (struct_type_it != gTypesByName.end()) {
				const TypeInfo* struct_type_info = struct_type_it->second;
				ctx.struct_info = struct_type_info->getStructInfo();
			}
		}
		
		auto expr_node = ASTNode::emplace_node<ExpressionNode>(node);
		auto eval_result = ConstExpr::Evaluator::evaluate(expr_node, ctx);
		
		if (eval_result.success()) {
			// Return the constant value
			unsigned long long value = 0;
			if (std::holds_alternative<long long>(eval_result.value)) {
				value = static_cast<unsigned long long>(std::get<long long>(eval_result.value));
			} else if (std::holds_alternative<unsigned long long>(eval_result.value)) {
				value = std::get<unsigned long long>(eval_result.value);
			}
			return { Type::UnsignedLongLong, 64, value };
		}
		
		// Return empty vector if evaluation failed
		return {};
	}

	// Helper function to evaluate whether an expression is noexcept
	// Returns true if the expression is guaranteed not to throw, false otherwise
	bool isExpressionNoexcept(const ExpressionNode& expr) const {
		// Literals are always noexcept
		if (std::holds_alternative<BoolLiteralNode>(expr) ||
		    std::holds_alternative<NumericLiteralNode>(expr) ||
		    std::holds_alternative<StringLiteralNode>(expr)) {
			return true;
		}
		
		// Identifiers (variable references) are noexcept
		if (std::holds_alternative<IdentifierNode>(expr) ||
		    std::holds_alternative<QualifiedIdentifierNode>(expr)) {
			return true;
		}
		
		// Template parameter references are noexcept
		if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
			return true;
		}
		
		// Built-in operators on primitives are noexcept
		if (std::holds_alternative<BinaryOperatorNode>(expr)) {
			const auto& binop = std::get<BinaryOperatorNode>(expr);
			// Recursively check operands
			if (binop.get_lhs().is<ExpressionNode>() && binop.get_rhs().is<ExpressionNode>()) {
				return isExpressionNoexcept(binop.get_lhs().as<ExpressionNode>()) &&
				       isExpressionNoexcept(binop.get_rhs().as<ExpressionNode>());
			}
			// If operands are not expressions, assume noexcept for built-ins
			return true;
		}
		
		if (std::holds_alternative<UnaryOperatorNode>(expr)) {
			const auto& unop = std::get<UnaryOperatorNode>(expr);
			if (unop.get_operand().is<ExpressionNode>()) {
				return isExpressionNoexcept(unop.get_operand().as<ExpressionNode>());
			}
			return true;
		}
		
		// Ternary operator: check all three sub-expressions
		if (std::holds_alternative<TernaryOperatorNode>(expr)) {
			const auto& ternary = std::get<TernaryOperatorNode>(expr);
			bool cond_noexcept = true, then_noexcept = true, else_noexcept = true;
			if (ternary.condition().is<ExpressionNode>()) {
				cond_noexcept = isExpressionNoexcept(ternary.condition().as<ExpressionNode>());
			}
			if (ternary.true_expr().is<ExpressionNode>()) {
				then_noexcept = isExpressionNoexcept(ternary.true_expr().as<ExpressionNode>());
			}
			if (ternary.false_expr().is<ExpressionNode>()) {
				else_noexcept = isExpressionNoexcept(ternary.false_expr().as<ExpressionNode>());
			}
			return cond_noexcept && then_noexcept && else_noexcept;
		}
		
		// Function calls: check if function is declared noexcept
		if (std::holds_alternative<FunctionCallNode>(expr)) {
			const auto& func_call = std::get<FunctionCallNode>(expr);
			// Check if function_declaration is available and noexcept
			// The FunctionCallNode contains a reference to the function's DeclarationNode
			// We need to look up the FunctionDeclarationNode to check noexcept
			const DeclarationNode& decl = func_call.function_declaration();
			std::string_view func_name = decl.identifier_token().value();
			
			// Look up the function in the symbol table
			extern SymbolTable gSymbolTable;
			auto symbol = gSymbolTable.lookup(StringTable::getOrInternStringHandle(func_name));
			if (symbol.has_value() && symbol->is<FunctionDeclarationNode>()) {
				const FunctionDeclarationNode& func_decl = symbol->as<FunctionDeclarationNode>();
				return func_decl.is_noexcept();
			}
			// If we can't determine, conservatively assume it may throw
			return false;
		}
		
		// Member function calls: check if method is declared noexcept
		if (std::holds_alternative<MemberFunctionCallNode>(expr)) {
			const auto& member_call = std::get<MemberFunctionCallNode>(expr);
			const FunctionDeclarationNode& func_decl = member_call.function_declaration();
			return func_decl.is_noexcept();
		}
		
		// Constructor calls: check if constructor is noexcept
		if (std::holds_alternative<ConstructorCallNode>(expr)) {
			// For now, conservatively assume constructors may throw
			// A complete implementation would check the constructor declaration
			return false;
		}
		
		// Array subscript: noexcept if index expression is noexcept
		if (std::holds_alternative<ArraySubscriptNode>(expr)) {
			const auto& subscript = std::get<ArraySubscriptNode>(expr);
			if (subscript.index_expr().is<ExpressionNode>()) {
				return isExpressionNoexcept(subscript.index_expr().as<ExpressionNode>());
			}
			return true;
		}
		
		// Member access is noexcept
		if (std::holds_alternative<MemberAccessNode>(expr)) {
			return true;
		}
		
		// sizeof, alignof, offsetof are always noexcept
		if (std::holds_alternative<SizeofExprNode>(expr) ||
		    std::holds_alternative<SizeofPackNode>(expr) ||
		    std::holds_alternative<AlignofExprNode>(expr) ||
		    std::holds_alternative<OffsetofExprNode>(expr)) {
			return true;
		}
		
		// Type traits are noexcept
		if (std::holds_alternative<TypeTraitExprNode>(expr)) {
			return true;
		}
		
		// new/delete can throw (unless using nothrow variant)
		if (std::holds_alternative<NewExpressionNode>(expr) ||
		    std::holds_alternative<DeleteExpressionNode>(expr)) {
			return false;
		}
		
		// Cast expressions: check the operand
		if (std::holds_alternative<StaticCastNode>(expr)) {
			const auto& cast = std::get<StaticCastNode>(expr);
			if (cast.expr().is<ExpressionNode>()) {
				return isExpressionNoexcept(cast.expr().as<ExpressionNode>());
			}
			return true;
		}
		if (std::holds_alternative<DynamicCastNode>(expr)) {
			// dynamic_cast can throw std::bad_cast
			return false;
		}
		if (std::holds_alternative<ConstCastNode>(expr)) {
			const auto& cast = std::get<ConstCastNode>(expr);
			if (cast.expr().is<ExpressionNode>()) {
				return isExpressionNoexcept(cast.expr().as<ExpressionNode>());
			}
			return true;
		}
		if (std::holds_alternative<ReinterpretCastNode>(expr)) {
			const auto& cast = std::get<ReinterpretCastNode>(expr);
			if (cast.expr().is<ExpressionNode>()) {
				return isExpressionNoexcept(cast.expr().as<ExpressionNode>());
			}
			return true;
		}
		
		// typeid can throw for dereferencing null polymorphic pointers
		if (std::holds_alternative<TypeidNode>(expr)) {
			return false;
		}
		
		// Lambda expressions themselves are noexcept (creating the closure)
		if (std::holds_alternative<LambdaExpressionNode>(expr)) {
			return true;
		}
		
		// Fold expressions: would need to check all sub-expressions
		if (std::holds_alternative<FoldExpressionNode>(expr)) {
			// Conservatively assume may throw
			return false;
		}
		
		// Pseudo-destructor calls are noexcept
		if (std::holds_alternative<PseudoDestructorCallNode>(expr)) {
			return true;
		}
		
		// Nested noexcept expression
		if (std::holds_alternative<NoexceptExprNode>(expr)) {
			// noexcept(noexcept(x)) - the outer noexcept doesn't evaluate its operand
			return true;
		}
		
		// Default: conservatively assume may throw
		return false;
	}

	// Implementation of recursive nested member store generation
	void generateNestedMemberStores(
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
				member_store.value.type = member.type;
				member_store.value.size_in_bits = static_cast<int>(member.size * 8);
				member_store.value.value = 0ULL;
				member_store.object = base_object;
				member_store.member_name = member_name;
				member_store.offset = base_offset + static_cast<int>(member.offset);
				member_store.is_reference = member.is_reference;
				member_store.is_rvalue_reference = member.is_rvalue_reference;
				member_store.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
				continue;
			}

			const ASTNode& init_expr = *member_values[member_name];

			if (init_expr.is<InitializerListNode>()) {
				// Nested brace initializer - check if member is a struct
				const InitializerListNode& nested_init_list = init_expr.as<InitializerListNode>();

				if (member.type_index < gTypeInfo.size()) {
					const TypeInfo& member_type_info = gTypeInfo[member.type_index];

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
					auto init_operands = visitExpressionNode(nested_initializers[0].as<ExpressionNode>());
					IrValue member_value = 0ULL;
					if (init_operands.size() >= 3) {
						if (std::holds_alternative<TempVar>(init_operands[2])) {
							member_value = std::get<TempVar>(init_operands[2]);
						} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
							member_value = std::get<unsigned long long>(init_operands[2]);
						} else if (std::holds_alternative<double>(init_operands[2])) {
							member_value = std::get<double>(init_operands[2]);
						} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
							member_value = std::get<StringHandle>(init_operands[2]);
						}
					}

					MemberStoreOp member_store;
					member_store.value.type = member.type;
					member_store.value.size_in_bits = static_cast<int>(member.size * 8);
					member_store.value.value = member_value;
					member_store.object = base_object;
					member_store.member_name = member_name;
					member_store.offset = base_offset + static_cast<int>(member.offset);
					member_store.is_reference = member.is_reference;
					member_store.is_rvalue_reference = member.is_rvalue_reference;
					member_store.struct_type_info = nullptr;
					ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
				} else {
					// Zero-initialize if we can't extract a value
					MemberStoreOp member_store;
					member_store.value.type = member.type;
					member_store.value.size_in_bits = static_cast<int>(member.size * 8);
					member_store.value.value = 0ULL;
					member_store.object = base_object;
					member_store.member_name = member_name;
					member_store.offset = base_offset + static_cast<int>(member.offset);
					member_store.is_reference = member.is_reference;
					member_store.is_rvalue_reference = member.is_rvalue_reference;
					member_store.struct_type_info = nullptr;
					ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
				}
			} else if (init_expr.is<ExpressionNode>()) {
				// Direct expression initializer
				auto init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
				IrValue member_value = 0ULL;
				if (init_operands.size() >= 3) {
					if (std::holds_alternative<TempVar>(init_operands[2])) {
						member_value = std::get<TempVar>(init_operands[2]);
					} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
						member_value = std::get<unsigned long long>(init_operands[2]);
					} else if (std::holds_alternative<double>(init_operands[2])) {
						member_value = std::get<double>(init_operands[2]);
					} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
						member_value = std::get<StringHandle>(init_operands[2]);
					}
				}

				MemberStoreOp member_store;
				member_store.value.type = member.type;
				member_store.value.size_in_bits = static_cast<int>(member.size * 8);
				member_store.value.value = member_value;
				member_store.object = base_object;
				member_store.member_name = member_name;
				member_store.offset = base_offset + static_cast<int>(member.offset);
				member_store.is_reference = member.is_reference;
				member_store.is_rvalue_reference = member.is_rvalue_reference;
				member_store.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
			}
		}
	}

	// Helper function to convert a MemberFunctionCallNode to a regular FunctionCallNode
	// Used when a member function call syntax is used but the object is not a struct
	std::vector<IrOperand> convertMemberCallToFunctionCall(const MemberFunctionCallNode& memberFunctionCallNode) {
		const FunctionDeclarationNode& func_decl = memberFunctionCallNode.function_declaration();
		DeclarationNode& decl_node = const_cast<DeclarationNode&>(func_decl.decl_node());
		
		// Copy the arguments using the visit method
		ChunkedVector<ASTNode> args_copy;
		memberFunctionCallNode.arguments().visit([&](ASTNode arg) {
			args_copy.push_back(arg);
		});
		
		FunctionCallNode function_call(decl_node, std::move(args_copy), memberFunctionCallNode.called_from());
		return generateFunctionCallIr(function_call);
	}

	// Helper function to check if access to a member is allowed
	// Returns true if access is allowed, false otherwise
	bool checkMemberAccess(const StructMember* member,
	                       const StructTypeInfo* member_owner_struct,
	                       const StructTypeInfo* accessing_struct,
	                       [[maybe_unused]] const BaseClassSpecifier* inheritance_path = nullptr,
	                       const std::string_view& accessing_function = "") const {
		if (!member || !member_owner_struct) {
			return false;
		}

		// If access control is disabled, allow all access
		if (context_->isAccessControlDisabled()) {
			return true;
		}

		// Public members are always accessible
		if (member->access == AccessSpecifier::Public) {
			return true;
		}

		// Check if accessing function is a friend function of the member owner
		if (!accessing_function.empty() && member_owner_struct->isFriendFunction(accessing_function)) {
			return true;
		}

		// Check if accessing class is a friend class of the member owner
		if (accessing_struct && member_owner_struct->isFriendClass(accessing_struct->getName())) {
			return true;
		}

		// If we're not in a member function context, only public members are accessible
		if (!accessing_struct) {
			return false;
		}

		// Helper: check if two structs are the same class, including template instantiations.
		// Template instantiations use a '$hash' suffix (e.g., basic_string_view$291eceb35e7234a9)
		// that must be stripped for comparison with the base template.
		// Template instantiation names may lack namespace prefix (e.g., "basic_string_view$hash"
		// vs "std::basic_string_view"), so we compare the unqualified class name only when
		// one name is a namespace-qualified version of the other.
		auto isSameClassOrInstantiation = [](const StructTypeInfo* a, const StructTypeInfo* b) -> bool {
			if (a == b) return true;
			if (!a || !b) return false;
			std::string_view name_a = StringTable::getStringView(a->getName());
			std::string_view name_b = StringTable::getStringView(b->getName());
			if (name_a == name_b) return true;
			// Strip '$hash' suffix only
			auto stripHash = [](std::string_view name) -> std::string_view {
				std::string_view base = extractBaseTemplateName(name);
				if (!base.empty()) {
					// Preserve namespace qualification: find the base template name
					// in the original and return everything up to where it starts
					auto pos = name.find(base);
					if (pos != std::string_view::npos) {
						return name.substr(0, pos + base.size());
					}
					return base;
				}
				return name;
			};
			std::string_view base_a = stripHash(name_a);
			std::string_view base_b = stripHash(name_b);
			if (base_a.empty() || base_b.empty()) return false;
			if (base_a == base_b) return true;
			// Handle asymmetric namespace qualification:
			// "basic_string_view" should match "std::basic_string_view" but
			// "ns1::Foo" should NOT match "ns2::Foo"
			// Check if the shorter name matches the unqualified part of the longer name
			auto getUnqualified = [](std::string_view name) -> std::string_view {
				auto ns_pos = name.rfind("::");
				if (ns_pos != std::string_view::npos) {
					return name.substr(ns_pos + 2);
				}
				return name;
			};
			// Only allow match when one has no namespace and the other does
			bool a_has_ns = base_a.find("::") != std::string_view::npos;
			bool b_has_ns = base_b.find("::") != std::string_view::npos;
			if (a_has_ns == b_has_ns) return false; // both qualified or both unqualified - already compared
			std::string_view unqual_a = getUnqualified(base_a);
			std::string_view unqual_b = getUnqualified(base_b);
			return unqual_a == unqual_b;
		};

		// Private members are only accessible from:
		// 1. The same class (or a template instantiation of the same class)
		// 2. Nested classes within the same class
		if (member->access == AccessSpecifier::Private) {
			if (isSameClassOrInstantiation(accessing_struct, member_owner_struct)) {
				return true;
			}
			// Check if accessing_struct is nested within member_owner_struct
			return isNestedWithin(accessing_struct, member_owner_struct);
		}

		// Protected members are accessible from:
		// 1. The same class (or a template instantiation of the same class)
		// 2. Derived classes (if inherited as public or protected)
		// 3. Nested classes within the same class (C++ allows nested classes to access protected)
		if (member->access == AccessSpecifier::Protected) {
			// Same class
			if (isSameClassOrInstantiation(accessing_struct, member_owner_struct)) {
				return true;
			}

			// Check if accessing_struct is nested within member_owner_struct
			if (isNestedWithin(accessing_struct, member_owner_struct)) {
				return true;
			}

			// Check if accessing_struct is derived from member_owner_struct
			return isAccessibleThroughInheritance(accessing_struct, member_owner_struct);
		}

		return false;
	}

	// Helper to check if accessing_struct is nested within member_owner_struct
	bool isNestedWithin(const StructTypeInfo* accessing_struct,
	                     const StructTypeInfo* member_owner_struct) const {
		if (!accessing_struct || !member_owner_struct) {
			return false;
		}

		// Check if accessing_struct is nested within member_owner_struct
		StructTypeInfo* current = accessing_struct->getEnclosingClass();
		while (current) {
			if (current == member_owner_struct) {
				return true;
			}
			current = current->getEnclosingClass();
		}

		return false;
	}

	// Helper to check if derived_struct can access protected members of base_struct
	bool isAccessibleThroughInheritance(const StructTypeInfo* derived_struct,
	                                     const StructTypeInfo* base_struct) const {
		if (!derived_struct || !base_struct) {
			return false;
		}

		// Check direct base classes
		for (const auto& base : derived_struct->base_classes) {
			if (base.type_index >= gTypeInfo.size()) {
				continue;
			}

			const TypeInfo& base_type = gTypeInfo[base.type_index];
			const StructTypeInfo* base_info = base_type.getStructInfo();

			if (!base_info) {
				continue;
			}

			// Found the base class
			if (base_info == base_struct) {
				// Protected members are accessible if inherited as public or protected
				return base.access == AccessSpecifier::Public ||
				       base.access == AccessSpecifier::Protected;
			}

			// Recursively check base classes
			if (isAccessibleThroughInheritance(base_info, base_struct)) {
				return true;
			}
		}

		return false;
	}

	// Get the current struct context (which class we're currently in)
	const StructTypeInfo* getCurrentStructContext() const {
		// Check if we're in a member function by looking at the symbol table
		// The 'this' pointer is only present in member function contexts
		auto this_symbol = symbol_table.lookup("this");
		if (this_symbol.has_value() && this_symbol->is<DeclarationNode>()) {
			const DeclarationNode& this_decl = this_symbol->as<DeclarationNode>();
			const TypeSpecifierNode& this_type = this_decl.type_node().as<TypeSpecifierNode>();

			if (this_type.type() == Type::Struct && this_type.type_index() < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[this_type.type_index()];
				return type_info.getStructInfo();
			}
		}

		return nullptr;
	}

	// Get the current function name
	std::string_view getCurrentFunctionName() const {
		return current_function_name_.isValid() ? StringTable::getStringView(current_function_name_) : std::string_view();
	}

	// Helper function to check if access to a member function is allowed
	bool checkMemberFunctionAccess(const StructMemberFunction* member_func,
	                                const StructTypeInfo* member_owner_struct,
	                                const StructTypeInfo* accessing_struct,
	                                std::string_view accessing_function = "") const {
		if (!member_func || !member_owner_struct) {
			return false;
		}

		// If access control is disabled, allow all access
		if (context_->isAccessControlDisabled()) {
			return true;
		}

		// Public member functions are always accessible
		if (member_func->access == AccessSpecifier::Public) {
			return true;
		}

		// Check if accessing function is a friend function of the member owner
		if (!accessing_function.empty() && member_owner_struct->isFriendFunction(accessing_function)) {
			return true;
		}

		// Check if accessing class is a friend class of the member owner
		if (accessing_struct && member_owner_struct->isFriendClass(accessing_struct->getName())) {
			return true;
		}

		// If we're not in a member function context, only public functions are accessible
		if (!accessing_struct) {
			return false;
		}

		// Private member functions are only accessible from:
		// 1. The same class
		// 2. Nested classes within the same class
		if (member_func->access == AccessSpecifier::Private) {
			if (accessing_struct == member_owner_struct) {
				return true;
			}
			// Check if accessing_struct is nested within member_owner_struct
			return isNestedWithin(accessing_struct, member_owner_struct);
		}

		// Protected member functions are accessible from:
		// 1. The same class
		// 2. Derived classes
		// 3. Nested classes within the same class (C++ allows nested classes to access protected)
		if (member_func->access == AccessSpecifier::Protected) {
			// Same class
			if (accessing_struct == member_owner_struct) {
				return true;
			}

			// Check if accessing_struct is nested within member_owner_struct
			if (isNestedWithin(accessing_struct, member_owner_struct)) {
				return true;
			}

			// Check if accessing_struct is derived from member_owner_struct
			return isAccessibleThroughInheritance(accessing_struct, member_owner_struct);
		}

		return false;
	}

	// Helper function to check if a variable is a reference by looking it up in the symbol table
	// Returns true if the variable is declared as a reference (&  or &&)
	bool isVariableReference(std::string_view var_name) const {
		const std::optional<ASTNode> symbol = symbol_table.lookup(var_name);
		
		if (symbol.has_value() && symbol->is<DeclarationNode>()) {
			const auto& decl = symbol->as<DeclarationNode>();
			const auto& type_spec = decl.type_node().as<TypeSpecifierNode>();
			return type_spec.is_lvalue_reference() || type_spec.is_rvalue_reference();
		}
		
		return false;
	}

	// Helper function to resolve the struct type and member info for a member access chain
	// Handles nested member access like o.inner.callback by recursively resolving types
	// Returns true if successfully resolved, with the struct_info and member populated
	bool resolveMemberAccessType(const MemberAccessNode& member_access,
	                            const StructTypeInfo*& out_struct_info,
	                            const StructMember*& out_member) const {
		// Get the base object expression
		const ASTNode& base_node = member_access.object();
		if (!base_node.is<ExpressionNode>()) {
			return false;
		}
		
		const ExpressionNode& base_expr = base_node.as<ExpressionNode>();
		TypeSpecifierNode base_type;
		
		if (std::holds_alternative<IdentifierNode>(base_expr)) {
			// Simple identifier - look it up in the symbol table
			const IdentifierNode& base_ident = std::get<IdentifierNode>(base_expr);
			std::optional<ASTNode> symbol = lookupSymbol(base_ident.name());
			if (!symbol.has_value()) {
				return false;
			}
			const DeclarationNode* base_decl = get_decl_from_symbol(*symbol);
			if (!base_decl) {
				return false;
			}
			base_type = base_decl->type_node().as<TypeSpecifierNode>();
		} else if (std::holds_alternative<MemberAccessNode>(base_expr)) {
			// Nested member access - recursively resolve
			const MemberAccessNode& nested_access = std::get<MemberAccessNode>(base_expr);
			const StructTypeInfo* nested_struct_info = nullptr;
			const StructMember* nested_member = nullptr;
			if (!resolveMemberAccessType(nested_access, nested_struct_info, nested_member)) {
				return false;
			}
			if (!nested_member || nested_member->type != Type::Struct) {
				return false;
			}
			// Get the type info for the nested member's struct type
			if (nested_member->type_index >= gTypeInfo.size()) {
				return false;
			}
			const TypeInfo& nested_type_info = gTypeInfo[nested_member->type_index];
			if (!nested_type_info.isStruct()) {
				return false;
			}
			// Convert size from bytes to bits for TypeSpecifierNode
			base_type = TypeSpecifierNode(Type::Struct, nested_member->type_index, 
			                               nested_member->size * 8, Token());  // size in bits
		} else {
			// Unsupported base expression type
			return false;
		}
		
		// If the base type is a pointer, dereference it
		if (base_type.pointer_levels().size() > 0) {
			base_type.remove_pointer_level();
		}
		
		// The base type should now be a struct type
		if (base_type.type() != Type::Struct) {
			return false;
		}
		
		// Look up the struct info
		size_t struct_type_index = base_type.type_index();
		if (struct_type_index >= gTypeInfo.size()) {
			return false;
		}
		const TypeInfo& struct_type_info = gTypeInfo[struct_type_index];
		const StructTypeInfo* struct_info = struct_type_info.getStructInfo();
		if (!struct_info) {
			return false;
		}
		
		// Find the member in the struct
		std::string_view member_name = member_access.member_name();
		StringHandle member_name_handle = StringTable::getOrInternStringHandle(member_name);
		for (const auto& member : struct_info->members) {
			if (member.getName() == member_name_handle) {
				out_struct_info = struct_info;
				out_member = &member;
				return true;
			}
		}
		
		return false;
	}

	// Helper function to handle assignment using lvalue metadata
	// Queries LValueInfo::Kind and routes to appropriate store instruction
	// Returns true if assignment was handled via lvalue metadata, false otherwise
	//
	// USAGE: Call this after evaluating both LHS and RHS expressions.
	//        If it returns true, the assignment was handled and caller should skip normal assignment logic.
	//        If it returns false, fall back to normal assignment or special-case handling.
	//
	// CURRENT LIMITATIONS:
	// - ArrayElement and Member cases need additional metadata (index, member_name) not currently in LValueInfo
	// - Only Indirect (dereference) case is fully implemented
	// - Future work: Extend LValueInfo or pass additional context to handle all cases
	bool handleLValueAssignment(const std::vector<IrOperand>& lhs_operands,
	                            const std::vector<IrOperand>& rhs_operands,
	                            const Token& token) {
		// Check if LHS has a TempVar with lvalue metadata
		if (lhs_operands.size() < 3 || !std::holds_alternative<TempVar>(lhs_operands[2])) {
			FLASH_LOG(Codegen, Info, "handleLValueAssignment: FAIL - size=", lhs_operands.size(), " has_tempvar=", (lhs_operands.size() >= 3 ? std::holds_alternative<TempVar>(lhs_operands[2]) : false));
			return false;
		}

		TempVar lhs_temp = std::get<TempVar>(lhs_operands[2]);
		auto lvalue_info_opt = getTempVarLValueInfo(lhs_temp);
		
		if (!lvalue_info_opt.has_value()) {
			FLASH_LOG(Codegen, Info, "handleLValueAssignment: FAIL - no lvalue metadata for temp=", lhs_temp.var_number);
			return false;
		}

		const LValueInfo& lv_info = lvalue_info_opt.value();
		
		FLASH_LOG(Codegen, Debug, "handleLValueAssignment: kind=", static_cast<int>(lv_info.kind));

		// Route to appropriate store instruction based on LValueInfo::Kind
		switch (lv_info.kind) {
			case LValueInfo::Kind::ArrayElement: {
				// Array element assignment: arr[i] = value
				FLASH_LOG(Codegen, Debug, "  -> ArrayStore (handled via metadata)");
				
				// Check if we have the index stored in metadata
				if (!lv_info.array_index.has_value()) {
					FLASH_LOG(Codegen, Info, "     ArrayElement: No index in metadata, falling back");
					return false;
				}
				
				FLASH_LOG(Codegen, Info, "     ArrayElement: Has index in metadata, proceeding with unified handler");
				
				// Build TypedValue for index from metadata
				IrValue index_value = lv_info.array_index.value();
				TypedValue index_tv;
				index_tv.value = index_value;
				index_tv.type = Type::Int;  // Index type (typically int)
				index_tv.size_in_bits = 32;  // Standard index size
				
				// Build TypedValue for value with LHS type/size but RHS value
				// This is important: the size must match the array element type
				TypedValue value_tv;
				value_tv.type = std::get<Type>(lhs_operands[0]);
				value_tv.size_in_bits = std::get<int>(lhs_operands[1]);
				value_tv.value = toIrValue(rhs_operands[2]);
				
				// Emit the store using helper
				emitArrayStore(
					std::get<Type>(lhs_operands[0]),  // element_type
					std::get<int>(lhs_operands[1]),   // element_size_bits
					lv_info.base,                      // array
					index_tv,                          // index
					value_tv,                          // value (with LHS type/size, RHS value)
					lv_info.offset,                    // member_offset
					lv_info.is_pointer_to_array,       // is_pointer_to_array
					token
				);
				return true;
			}

			case LValueInfo::Kind::Member: {
				// Member assignment: obj.member = value
				FLASH_LOG(Codegen, Debug, "  -> MemberStore (handled via metadata)");
				
				// Check if we have member_name stored in metadata
				if (!lv_info.member_name.has_value()) {
					FLASH_LOG(Codegen, Debug, "     No member_name in metadata, falling back");
					return false;
				}
				
				// Safety check: validate size is reasonable (not 0 or negative)
				int lhs_size = std::get<int>(lhs_operands[1]);
				if (lhs_size <= 0 || lhs_size > 1024) {
					FLASH_LOG(Codegen, Debug, "     Invalid size in metadata (", lhs_size, "), falling back");
					return false;
				}
				
				// Build TypedValue with LHS type/size but RHS value
				// This is important: the size must match the member being stored to, not the RHS
				TypedValue value_tv;
				value_tv.type = std::get<Type>(lhs_operands[0]);
				value_tv.size_in_bits = lhs_size;
				value_tv.value = toIrValue(rhs_operands[2]);
				
				// Emit the store using helper
				emitMemberStore(
					value_tv,                           // value (with LHS type/size, RHS value)
					lv_info.base,                       // object
					lv_info.member_name.value(),        // member_name
					lv_info.offset,                     // offset
					false,                              // is_reference
					false,                              // is_rvalue_reference
					lv_info.is_pointer_to_member,       // is_pointer_to_member
					token,
					lv_info.bitfield_width,             // bitfield_width
					lv_info.bitfield_bit_offset         // bitfield_bit_offset
				);
				return true;
			}

			case LValueInfo::Kind::Indirect: {
				// Dereference assignment: *ptr = value
				// This case works because we have all needed info in LValueInfo
				FLASH_LOG(Codegen, Debug, "  -> DereferenceStore (handled via metadata)");
				
				// Emit the store using helper
				emitDereferenceStore(
					toTypedValue(rhs_operands),     // value
					std::get<Type>(lhs_operands[0]), // pointee_type
					std::get<int>(lhs_operands[1]),  // pointee_size_in_bits
					lv_info.base,                    // pointer
					token
				);
				return true;
			}

			case LValueInfo::Kind::Direct:
			case LValueInfo::Kind::Temporary:
				// Direct variable assignment - handled by regular assignment logic
				FLASH_LOG(Codegen, Debug, "  -> Regular assignment (Direct/Temporary)");
				return false;

			default:
				return false;
		}

		return false;
	}

	// Handle compound assignment to lvalues (e.g., v.x += 5, arr[i] += 5)
	// Supports Member kind (struct member access), Indirect kind (dereferenced pointers - already supported), and ArrayElement kind (array subscripts - added in this function)
	// This is similar to handleLValueAssignment but also performs the arithmetic operation
	bool handleLValueCompoundAssignment(const std::vector<IrOperand>& lhs_operands,
	                                     const std::vector<IrOperand>& rhs_operands,
	                                     const Token& token,
	                                     std::string_view op) {
		// Check if LHS has a TempVar with lvalue metadata
		if (lhs_operands.size() < 3 || !std::holds_alternative<TempVar>(lhs_operands[2])) {
			FLASH_LOG(Codegen, Info, "handleLValueCompoundAssignment: FAIL - size=", lhs_operands.size(), 
				", has_tempvar=", (lhs_operands.size() >= 3 && std::holds_alternative<TempVar>(lhs_operands[2])));
			return false;
		}

		TempVar lhs_temp = std::get<TempVar>(lhs_operands[2]);
		FLASH_LOG_FORMAT(Codegen, Debug, "handleLValueCompoundAssignment: Checking TempVar {} for metadata", lhs_temp.var_number);
		auto lvalue_info_opt = getTempVarLValueInfo(lhs_temp);
		
		if (!lvalue_info_opt.has_value()) {
			FLASH_LOG_FORMAT(Codegen, Debug, "handleLValueCompoundAssignment: FAIL - no lvalue metadata for TempVar {}", lhs_temp.var_number);
			return false;
		}

		const LValueInfo& lv_info = lvalue_info_opt.value();
		
		FLASH_LOG(Codegen, Debug, "handleLValueCompoundAssignment: kind=", static_cast<int>(lv_info.kind), " op=", op);

		// For compound assignments, we need to:
		// 1. The lhs_temp already contains the ADDRESS (from LValueAddress context)
		// 2. We need to LOAD the current value from that address
		// 3. Perform the operation with RHS
		// 4. Store the result back to the address
		
		// First, load the current value from the lvalue
		// The lhs_temp should contain the address, but we need to generate a Load instruction
		// to get the current value into a temp var
		TempVar current_value_temp = var_counter.next();
		
		// Generate a Load instruction based on the lvalue kind
		// Support both Member kind and Indirect kind (for dereferenced pointers like &y in lambda captures)
		if (lv_info.kind == LValueInfo::Kind::Indirect) {
			// For Indirect kind (dereferenced pointer), the base can be a TempVar or StringHandle
			// Generate a Dereference instruction to load the current value
			DereferenceOp deref_op;
			deref_op.result = current_value_temp;
			deref_op.pointer.type = std::get<Type>(lhs_operands[0]);
			deref_op.pointer.size_in_bits = 64;  // pointer size
			deref_op.pointer.pointer_depth = 1;
			
			// Extract the base (TempVar or StringHandle)
			std::variant<TempVar, StringHandle> base_value;
			if (std::holds_alternative<TempVar>(lv_info.base)) {
				deref_op.pointer.value = std::get<TempVar>(lv_info.base);
				base_value = std::get<TempVar>(lv_info.base);
			} else if (std::holds_alternative<StringHandle>(lv_info.base)) {
				deref_op.pointer.value = std::get<StringHandle>(lv_info.base);
				base_value = std::get<StringHandle>(lv_info.base);
			} else {
				FLASH_LOG(Codegen, Debug, "     Indirect kind requires TempVar or StringHandle base");
				return false;
			}
			
			ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), token));
			
			// Now perform the operation (e.g., Add for +=, Subtract for -=, etc.)
			TempVar result_temp = var_counter.next();
			
			static const std::unordered_map<std::string_view, IrOpcode> compound_op_map = {
				{"+=", IrOpcode::Add},
				{"-=", IrOpcode::Subtract},
				{"*=", IrOpcode::Multiply},
				{"/=", IrOpcode::Divide},
				{"%=", IrOpcode::Modulo},
				{"&=", IrOpcode::BitwiseAnd},
				{"|=", IrOpcode::BitwiseOr},
				{"^=", IrOpcode::BitwiseXor},
				{"<<=", IrOpcode::ShiftLeft},
				{">>=", IrOpcode::ShiftRight}
			};
			
			auto op_it = compound_op_map.find(op);
			if (op_it == compound_op_map.end()) {
				FLASH_LOG(Codegen, Debug, "     Unsupported compound assignment operator: ", op);
				return false;
			}
			IrOpcode operation_opcode = op_it->second;
			
			// Create the binary operation
			BinaryOp bin_op;
			bin_op.lhs.type = std::get<Type>(lhs_operands[0]);
			bin_op.lhs.size_in_bits = std::get<int>(lhs_operands[1]);
			bin_op.lhs.value = current_value_temp;
			bin_op.rhs = toTypedValue(rhs_operands);
			bin_op.result = result_temp;
			
			ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));
			
			// Store result back through the pointer using DereferenceStore
			TypedValue result_tv;
			result_tv.type = std::get<Type>(lhs_operands[0]);
			result_tv.size_in_bits = std::get<int>(lhs_operands[1]);
			result_tv.value = result_temp;
			
			// Handle both TempVar and StringHandle bases for DereferenceStore
			if (std::holds_alternative<TempVar>(base_value)) {
				emitDereferenceStore(result_tv, std::get<Type>(lhs_operands[0]), std::get<int>(lhs_operands[1]),
				                     std::get<TempVar>(base_value), token);
			} else {
				// StringHandle base: emitDereferenceStore expects a TempVar, so we pass the StringHandle as the pointer
				// Generate DereferenceStore with StringHandle directly
				DereferenceStoreOp store_op;
				store_op.pointer.type = std::get<Type>(lhs_operands[0]);
				store_op.pointer.size_in_bits = 64;
				store_op.pointer.pointer_depth = 1;
				store_op.pointer.value = std::get<StringHandle>(base_value);
				store_op.value = result_tv;
				ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_op), token));
			}
			
			return true;
		}
		
		// Handle ArrayElement kind for compound assignments (e.g., arr[i] += 5)
		if (lv_info.kind == LValueInfo::Kind::ArrayElement) {
			// Check if we have the index stored in metadata
			if (!lv_info.array_index.has_value()) {
				FLASH_LOG(Codegen, Debug, "     ArrayElement: No index in metadata for compound assignment");
				return false;
			}
			
			FLASH_LOG(Codegen, Debug, "     ArrayElement compound assignment: proceeding with unified handler");
			
			// Build TypedValue for index from metadata
			IrValue index_value = lv_info.array_index.value();
			TypedValue index_tv;
			index_tv.value = index_value;
			index_tv.type = Type::Int;  // Index type (typically int)
			index_tv.size_in_bits = 32;  // Standard index size
			
			// Create ArrayAccessOp to load current value
			ArrayAccessOp load_op;
			load_op.result = current_value_temp;
			load_op.element_type = std::get<Type>(lhs_operands[0]);
			load_op.element_size_in_bits = std::get<int>(lhs_operands[1]);
			load_op.array = lv_info.base;
			load_op.index = index_tv;
			load_op.member_offset = lv_info.offset;
			load_op.is_pointer_to_array = lv_info.is_pointer_to_array;
			
			ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(load_op), token));
			
			// Now perform the operation (e.g., Add for +=, Subtract for -=, etc.)
			TempVar result_temp = var_counter.next();
			
			// Map compound assignment operator to the corresponding operation
			static const std::unordered_map<std::string_view, IrOpcode> compound_op_map = {
				{"+=", IrOpcode::Add},
				{"-=", IrOpcode::Subtract},
				{"*=", IrOpcode::Multiply},
				{"/=", IrOpcode::Divide},
				{"%=", IrOpcode::Modulo},
				{"&=", IrOpcode::BitwiseAnd},
				{"|=", IrOpcode::BitwiseOr},
				{"^=", IrOpcode::BitwiseXor},
				{"<<=", IrOpcode::ShiftLeft},
				{">>=", IrOpcode::ShiftRight}
			};
			
			auto op_it = compound_op_map.find(op);
			if (op_it == compound_op_map.end()) {
				FLASH_LOG(Codegen, Debug, "     Unsupported compound assignment operator: ", op);
				return false;
			}
			IrOpcode operation_opcode = op_it->second;
			
			// Create the binary operation
			BinaryOp bin_op;
			bin_op.lhs.type = std::get<Type>(lhs_operands[0]);
			bin_op.lhs.size_in_bits = std::get<int>(lhs_operands[1]);
			bin_op.lhs.value = current_value_temp;
			bin_op.rhs = toTypedValue(rhs_operands);
			bin_op.result = result_temp;
			
			ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));
			
			// Finally, store the result back to the array element
			TypedValue result_tv;
			result_tv.type = std::get<Type>(lhs_operands[0]);
			result_tv.size_in_bits = std::get<int>(lhs_operands[1]);
			result_tv.value = result_temp;
			
			// Emit the store using helper
			emitArrayStore(
				std::get<Type>(lhs_operands[0]),  // element_type
				std::get<int>(lhs_operands[1]),   // element_size_bits
				lv_info.base,                      // array
				index_tv,                          // index
				result_tv,                         // value (result of operation)
				lv_info.offset,                    // member_offset
				lv_info.is_pointer_to_array,       // is_pointer_to_array
				token
			);
			
			return true;
		}
		
	// Handle Global kind for compound assignments (e.g., g_score += 20)
		if (lv_info.kind == LValueInfo::Kind::Global) {
			if (!std::holds_alternative<StringHandle>(lv_info.base)) {
				FLASH_LOG(Codegen, Debug, "     Global compound assignment: base is not a StringHandle");
				return false;
			}
			StringHandle global_name = std::get<StringHandle>(lv_info.base);
			FLASH_LOG(Codegen, Debug, "     Global compound assignment op=", op);

			// Map compound assignment operator to the corresponding operation
			static const std::unordered_map<std::string_view, IrOpcode> compound_op_map = {
				{"+=", IrOpcode::Add},
				{"-=", IrOpcode::Subtract},
				{"*=", IrOpcode::Multiply},
				{"/=", IrOpcode::Divide},
				{"%=", IrOpcode::Modulo},
				{"&=", IrOpcode::BitwiseAnd},
				{"|=", IrOpcode::BitwiseOr},
				{"^=", IrOpcode::BitwiseXor},
				{"<<=", IrOpcode::ShiftLeft},
				{">>=", IrOpcode::ShiftRight}
			};
			auto op_it = compound_op_map.find(op);
			if (op_it == compound_op_map.end()) {
				FLASH_LOG(Codegen, Debug, "     Unsupported compound assignment operator: ", op);
				return false;
			}

			// lhs_temp already holds the loaded value (from GlobalLoad in LHS evaluation)
			TempVar result_temp = var_counter.next();
			BinaryOp bin_op;
			bin_op.lhs.type = std::get<Type>(lhs_operands[0]);
			bin_op.lhs.size_in_bits = std::get<int>(lhs_operands[1]);
			bin_op.lhs.value = lhs_temp;
			bin_op.rhs = toTypedValue(rhs_operands);
			bin_op.result = result_temp;
			ir_.addInstruction(IrInstruction(op_it->second, std::move(bin_op), token));

			// Store result back to global
			std::vector<IrOperand> store_operands;
			store_operands.emplace_back(global_name);
			store_operands.emplace_back(result_temp);
			ir_.addInstruction(IrOpcode::GlobalStore, std::move(store_operands), token);

			return true;
		}

		if (lv_info.kind != LValueInfo::Kind::Member) {
			FLASH_LOG(Codegen, Debug, "     Compound assignment only supports Member, Indirect, ArrayElement, or Global kind, got: ", static_cast<int>(lv_info.kind));
			return false;
		}
		
		// For member access, generate MemberAccess (Load) instruction
		if (!lv_info.member_name.has_value()) {
			FLASH_LOG(Codegen, Debug, "     No member_name in metadata for compound assignment");
			return false;
		}
		
		// Lookup member info to get is_reference flags
		bool member_is_reference = false;
		bool member_is_rvalue_reference = false;
		
		// Try to get struct type info from the base object
		if (std::holds_alternative<StringHandle>(lv_info.base)) {
			StringHandle base_name_handle = std::get<StringHandle>(lv_info.base);
			std::string_view base_name = StringTable::getStringView(base_name_handle);
			
			// Look up the base object in symbol table
			std::optional<ASTNode> symbol = lookupSymbol(base_name);
			
			if (symbol.has_value()) {
				const DeclarationNode* decl = get_decl_from_symbol(*symbol);
				if (decl) {
					const TypeSpecifierNode& type_node = decl->type_node().as<TypeSpecifierNode>();
					if (is_struct_type(type_node.type())) {
						TypeIndex type_index = type_node.type_index();
						if (type_index < gTypeInfo.size()) {
							auto result = FlashCpp::gLazyMemberResolver.resolve(type_index, lv_info.member_name.value());
							if (result) {
								member_is_reference = result.member->is_reference;
								member_is_rvalue_reference = result.member->is_rvalue_reference;
							}
						}
					}
				}
			}
		}
		// Note: For TempVar base, we don't have easy access to type info, so we default to false
		// This is acceptable since most compound assignments don't involve reference members
		
		MemberLoadOp load_op;
		load_op.result.value = current_value_temp;
		load_op.result.type = std::get<Type>(lhs_operands[0]);
		load_op.result.size_in_bits = std::get<int>(lhs_operands[1]);
		load_op.object = lv_info.base;
		load_op.member_name = lv_info.member_name.value();
		load_op.offset = lv_info.offset;
		load_op.is_reference = member_is_reference;
		load_op.is_rvalue_reference = member_is_rvalue_reference;
		load_op.struct_type_info = nullptr;
		load_op.bitfield_width = lv_info.bitfield_width;
		load_op.bitfield_bit_offset = lv_info.bitfield_bit_offset;
		
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), token));
		
		// Now perform the operation (e.g., Add for +=, Subtract for -=, etc.)
		TempVar result_temp = var_counter.next();
		
		// Map compound assignment operator to the corresponding operation
		static const std::unordered_map<std::string_view, IrOpcode> compound_op_map = {
			{"+=", IrOpcode::Add},
			{"-=", IrOpcode::Subtract},
			{"*=", IrOpcode::Multiply},
			{"/=", IrOpcode::Divide},
			{"%=", IrOpcode::Modulo},
			{"&=", IrOpcode::BitwiseAnd},
			{"|=", IrOpcode::BitwiseOr},
			{"^=", IrOpcode::BitwiseXor},
			{"<<=", IrOpcode::ShiftLeft},
			{">>=", IrOpcode::ShiftRight}
		};
		
		auto op_it = compound_op_map.find(op);
		if (op_it == compound_op_map.end()) {
			FLASH_LOG(Codegen, Debug, "     Unsupported compound assignment operator: ", op);
			return false;
		}
		IrOpcode operation_opcode = op_it->second;
		
		// Create the binary operation
		BinaryOp bin_op;
		bin_op.lhs.type = std::get<Type>(lhs_operands[0]);
		bin_op.lhs.size_in_bits = std::get<int>(lhs_operands[1]);
		bin_op.lhs.value = current_value_temp;
		bin_op.rhs = toTypedValue(rhs_operands);
		bin_op.result = result_temp;
		
		ir_.addInstruction(IrInstruction(operation_opcode, std::move(bin_op), token));
		
		// Finally, store the result back to the lvalue
		TypedValue result_tv;
		result_tv.type = std::get<Type>(lhs_operands[0]);
		result_tv.size_in_bits = std::get<int>(lhs_operands[1]);
		result_tv.value = result_temp;
		
		emitMemberStore(
			result_tv,
			lv_info.base,
			lv_info.member_name.value(),
			lv_info.offset,
			member_is_reference,
			member_is_rvalue_reference,
			lv_info.is_pointer_to_member,  // is_pointer_to_member
			token,
			lv_info.bitfield_width,
			lv_info.bitfield_bit_offset
		);
		
		return true;
	}

	// Helper functions to emit store instructions
	// These can be used by both the unified handler and special-case code
	
	// Emit ArrayStore instruction
	void emitArrayStore(Type element_type, int element_size_bits,
	                    std::variant<StringHandle, TempVar> array,
	                    const TypedValue& index, const TypedValue& value,
	                    int64_t member_offset, bool is_pointer_to_array,
	                    const Token& token) {
		ArrayStoreOp payload;
		payload.element_type = element_type;
		payload.element_size_in_bits = element_size_bits;
		payload.array = array;
		payload.index = index;
		payload.value = value;
		payload.member_offset = member_offset;
		payload.is_pointer_to_array = is_pointer_to_array;
		
		ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(payload), token));
	}
	
	// Emit MemberStore instruction
	void emitMemberStore(const TypedValue& value,
	                     std::variant<StringHandle, TempVar> object,
	                     StringHandle member_name, int offset,
	                     bool is_reference = false, bool is_rvalue_reference = false,
	                     bool is_pointer_to_member = false,
	                     const Token& token = Token(),
	                     std::optional<size_t> bitfield_width = std::nullopt,
	                     size_t bitfield_bit_offset = 0) {
		MemberStoreOp member_store;
		member_store.value = value;
		member_store.object = object;
		member_store.member_name = member_name;
		member_store.offset = offset;
		member_store.struct_type_info = nullptr;
		member_store.is_reference = is_reference;
		member_store.is_rvalue_reference = is_rvalue_reference;
		member_store.vtable_symbol = StringHandle();
		member_store.is_pointer_to_member = is_pointer_to_member;
		member_store.bitfield_width = bitfield_width;
		member_store.bitfield_bit_offset = bitfield_bit_offset;
		
		ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
	}
	
	// Emit DereferenceStore instruction
	void emitDereferenceStore(const TypedValue& value, Type pointee_type, [[maybe_unused]] int pointee_size_bits,
	                          std::variant<StringHandle, TempVar> pointer,
	                          const Token& token) {
		DereferenceStoreOp store_op;
		store_op.value = value;
		
		// Populate pointer TypedValue
		store_op.pointer.type = pointee_type;
		store_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
		store_op.pointer.pointer_depth = 1;  // Single pointer dereference
		// Convert std::variant<StringHandle, TempVar> to IrValue
		if (std::holds_alternative<StringHandle>(pointer)) {
			store_op.pointer.value = std::get<StringHandle>(pointer);
		} else {
			store_op.pointer.value = std::get<TempVar>(pointer);
		}
		
		ir_.addInstruction(IrInstruction(IrOpcode::DereferenceStore, std::move(store_op), token));
	}

	const DeclarationNode& requireDeclarationNode(const ASTNode& node, std::string_view context) const {
		try {
			return node.as<DeclarationNode>();
		} catch (...) {
			FLASH_LOG(Codegen, Error, "BAD DeclarationNode cast in ", context,
			          ": type_name=", node.type_name(),
			          " has_value=", node.has_value());
			throw;
		}
	}

	// Helper to generate FunctionAddress IR for a lambda's __invoke function
	// Returns the TempVar holding the function pointer address
	TempVar generateLambdaInvokeFunctionAddress(const LambdaExpressionNode& lambda) {
		std::string_view invoke_name = StringBuilder()
			.append(lambda.generate_lambda_name())
			.append("_invoke")
			.commit();
		
		// Compute the mangled name for the __invoke function
		// Lambda return type defaults to int if not specified
		Type return_type = Type::Int;
		int return_size = 32;
		if (lambda.return_type().has_value()) {
			const auto& ret_type_node = lambda.return_type()->as<TypeSpecifierNode>();
			return_type = ret_type_node.type();
			return_size = ret_type_node.size_in_bits();
		}
		TypeSpecifierNode return_type_node(return_type, 0, return_size, lambda.lambda_token());
		
		// Build parameter types
		std::vector<TypeSpecifierNode> param_type_nodes;
		for (const auto& param : lambda.parameters()) {
			if (param.is<DeclarationNode>()) {
				const auto& param_decl = param.as<DeclarationNode>();
				const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
				param_type_nodes.push_back(param_type);
			}
		}
		
		// Generate mangled name
		std::string_view mangled = generateMangledNameForCall(
			invoke_name, return_type_node, param_type_nodes, false, "");
		
		// Generate FunctionAddress instruction to get the address
		TempVar func_addr_var = var_counter.next();
		FunctionAddressOp op;
		op.result.type = Type::FunctionPointer;
		op.result.size_in_bits = 64;
		op.result.value = func_addr_var;
		op.function_name = StringTable::getOrInternStringHandle(invoke_name);
		op.mangled_name = StringTable::getOrInternStringHandle(mangled);
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionAddress, std::move(op), Token()));
		
		return func_addr_var;
	}

	// Helper to find a conversion operator in a struct that converts to the target type
	// Returns nullptr if no suitable conversion operator is found
	// Searches the struct and its base classes for "operator target_type()"
	const StructMemberFunction* findConversionOperator(
		const StructTypeInfo* struct_info,
		Type target_type,
		TypeIndex target_type_index = 0) const {
		
		if (!struct_info) return nullptr;
		
		// Build the operator name we are looking for (e.g., "operator int")
		std::string_view target_type_name;
		if (target_type == Type::Struct && target_type_index < gTypeInfo.size()) {
			target_type_name = StringTable::getStringView(gTypeInfo[target_type_index].name());
		} else {
			// For primitive types, use the helper function to get the type name
			target_type_name = getTypeName(target_type);
			if (target_type_name.empty()) {
				return nullptr;
			}
		}
		
		// Create the operator name string (e.g., "operator int")
		StringBuilder sb;
		sb.append("operator ").append(target_type_name);
		std::string_view operator_name = sb.commit();
		StringHandle operator_name_handle = StringTable::getOrInternStringHandle(operator_name);
		
		// Search member functions for the conversion operator
		for (const auto& member_func : struct_info->member_functions) {
			if (member_func.getName() == operator_name_handle) {
				return &member_func;
			}
		}
		
		// WORKAROUND: Also look for "operator user_defined" which may be a conversion operator
		// that was created with a typedef that wasn't resolved during template instantiation
		// Check if the return type matches the target type
		StringHandle user_defined_handle = StringTable::getOrInternStringHandle("operator user_defined");
		for (const auto& member_func : struct_info->member_functions) {
			if (member_func.getName() == user_defined_handle) {
				// Check if this function's return type matches our target
				if (member_func.function_decl.is<FunctionDeclarationNode>()) {
					const auto& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
					const auto& decl_node = func_decl.decl_node();
					const auto& return_type_node = decl_node.type_node();
					if (return_type_node.is<TypeSpecifierNode>()) {
						const auto& type_spec = return_type_node.as<TypeSpecifierNode>();
						Type resolved_type = type_spec.type();
						
						// If the return type is UserDefined (a type alias), try to resolve it to the actual underlying type
						// This handles cases like `operator value_type()` where `using value_type = T;`
						// Use recursive resolution to handle chains of type aliases
						if (resolved_type == Type::UserDefined && type_spec.type_index() < gTypeInfo.size()) {
							TypeIndex current_type_index = type_spec.type_index();
							int max_depth = 10;  // Prevent infinite loops from circular aliases
							while (resolved_type == Type::UserDefined && current_type_index < gTypeInfo.size() && max_depth-- > 0) {
								const TypeInfo& alias_type_info = gTypeInfo[current_type_index];
								if (alias_type_info.type_ != Type::Void && alias_type_info.type_ != Type::UserDefined) {
									resolved_type = alias_type_info.type_;
									FLASH_LOG(Codegen, Debug, "Resolved type alias in conversion operator return type: UserDefined -> ", static_cast<int>(resolved_type));
									break;
								} else if (alias_type_info.type_ == Type::UserDefined && alias_type_info.type_index_ != current_type_index) {
									// Follow the chain of aliases
									current_type_index = alias_type_info.type_index_;
								} else {
									break;
								}
							}
						}
						
						if (resolved_type == target_type) {
							// Found a match!
							FLASH_LOG(Codegen, Debug, "Found conversion operator via 'operator user_defined' workaround");
							return &member_func;
						}
						
						// FALLBACK: If the return type is still UserDefined (couldn't resolve via gTypeInfo),
						// but the size matches the target primitive type, accept it as a match.
						// This handles template type aliases like `using value_type = T;` where T is substituted
						// but the return type wasn't fully updated in the AST.
						if (resolved_type == Type::UserDefined && target_type != Type::Struct && target_type != Type::Enum) {
							int expected_size = get_type_size_bits(target_type);
							
							if (expected_size > 0 && static_cast<int>(type_spec.size_in_bits()) == expected_size) {
								FLASH_LOG(Codegen, Debug, "Found conversion operator via size matching: UserDefined(size=", 
								          type_spec.size_in_bits(), ") matches target type ", static_cast<int>(target_type), " (size=", expected_size, ")");
								return &member_func;
							}
							// Note: We intentionally don't have a permissive fallback here because it would match
							// conversion operators from pattern templates that don't have generated code, leading
							// to linker errors (undefined reference to operator user_defined).
						}
					}
				}
			}
		}
		
		// Search base classes recursively
		for (const auto& base_spec : struct_info->base_classes) {
			if (base_spec.type_index < gTypeInfo.size()) {
				const TypeInfo& base_type_info = gTypeInfo[base_spec.type_index];
				if (base_type_info.isStruct()) {
					const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
					const StructMemberFunction* result = findConversionOperator(
						base_struct_info, target_type, target_type_index);
					if (result) return result;
				}
			}
		}
		
		return nullptr;
	}

	// Helper to get the size of a type in bytes
	// Reuses the same logic as sizeof() operator
	// Used for pointer arithmetic (++/-- operators need sizeof(pointee_type))
	size_t getSizeInBytes(Type type, TypeIndex type_index, int size_in_bits) const {
		if (type == Type::Struct) {
			assert(type_index < gTypeInfo.size() && "Invalid type_index for struct");
			const TypeInfo& type_info = gTypeInfo[type_index];
			const StructTypeInfo* struct_info = type_info.getStructInfo();
			assert(struct_info && "Struct type info not found");
			return struct_info->total_size;
		}
		// For primitive types, convert bits to bytes
		return size_in_bits / 8;
	}

	// ========== Lambda Capture Helper Functions ==========

	// Get the current lambda's closure StructTypeInfo, or nullptr if not in a lambda
	const StructTypeInfo* getCurrentClosureStruct() const {
		if (!current_lambda_context_.isActive()) {
			return nullptr;
		}
		auto it = gTypesByName.find(current_lambda_context_.closure_type);
		if (it == gTypesByName.end() || !it->second->isStruct()) {
			return nullptr;
		}
		return it->second->getStructInfo();
	}

	// Check if we're in a lambda with [*this] capture
	bool isInCopyThisLambda() const {
		if (!current_lambda_context_.isActive()) {
			return false;
		}
		if (current_lambda_context_.has_copy_this) {
			return true;
		}
		if (const StructTypeInfo* closure = getCurrentClosureStruct()) {
			return closure->findMember("__copy_this") != nullptr;
		}
		return false;
	}

	// Check if we're in a lambda with [this] pointer capture
	bool isInThisPointerLambda() const {
		return current_lambda_context_.isActive() && current_lambda_context_.has_this_pointer;
	}

	// Get the offset of a member in the current lambda closure struct
	// Returns 0 if not found or not in a lambda context
	int getClosureMemberOffset(std::string_view member_name) const {
		if (const StructTypeInfo* closure = getCurrentClosureStruct()) {
			if (const StructMember* member = closure->findMember(member_name)) {
				return static_cast<int>(member->offset);
			}
		}
		return 0;
	}

	// Emit IR to load __copy_this from current lambda closure into a TempVar.
	// Returns the TempVar holding the copied object, or std::nullopt if not applicable.
	// The Token parameter is used for source location in the IR instruction.
	std::optional<TempVar> emitLoadCopyThis(const Token& token) {
		if (!isInCopyThisLambda()) {
			return std::nullopt;
		}
		const StructTypeInfo* closure_struct = getCurrentClosureStruct();
		if (!closure_struct) {
			return std::nullopt;
		}
		const StructMember* copy_this_member = closure_struct->findMember("__copy_this");
		if (!copy_this_member || current_lambda_context_.enclosing_struct_type_index == 0) {
			return std::nullopt;
		}

		TempVar copy_this_temp = var_counter.next();
		MemberLoadOp load_op;
		load_op.result.value = copy_this_temp;
		load_op.result.type = Type::Struct;
		load_op.result.size_in_bits = static_cast<int>(copy_this_member->size * 8);
		load_op.object = StringTable::getOrInternStringHandle("this");  // Lambda's this (the closure)
		load_op.member_name = StringTable::getOrInternStringHandle("__copy_this");
		load_op.offset = static_cast<int>(copy_this_member->offset);
		load_op.is_reference = false;
		load_op.is_rvalue_reference = false;
		load_op.struct_type_info = nullptr;
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), token));

		// Mark this temp var as an lvalue pointing to %this.__copy_this
		// This allows subsequent member accesses and stores to properly chain offsets
		LValueInfo lvalue_info(
			LValueInfo::Kind::Member,
			StringTable::getOrInternStringHandle("this"),
			static_cast<int>(copy_this_member->offset)
		);
		lvalue_info.member_name = StringTable::getOrInternStringHandle("__copy_this");
		lvalue_info.is_pointer_to_member = true;  // Treat closure 'this' as a pointer
		setTempVarMetadata(copy_this_temp, TempVarMetadata::makeLValue(lvalue_info));

		return copy_this_temp;
	}

	// Manage lambda context push/pop for nested lambdas
	void pushLambdaContext(const LambdaInfo& lambda_info) {
		lambda_context_stack_.push_back(current_lambda_context_);
		current_lambda_context_ = {};
		current_lambda_context_.closure_type = StringTable::getOrInternStringHandle(lambda_info.closure_type_name);
		current_lambda_context_.enclosing_struct_type_index = lambda_info.enclosing_struct_type_index;
		current_lambda_context_.has_copy_this = lambda_info.enclosing_struct_type_index > 0;
		current_lambda_context_.has_this_pointer = false;
		current_lambda_context_.is_mutable = lambda_info.is_mutable;

		size_t capture_index = 0;
		for (const auto& capture : lambda_info.captures) {
			if (capture.is_capture_all()) {
				continue;
			}
			StringHandle var_name = StringTable::getOrInternStringHandle(capture.identifier_name());
			current_lambda_context_.captures.insert(var_name);
			current_lambda_context_.capture_kinds[var_name] = capture.kind();
			if (capture.kind() == LambdaCaptureNode::CaptureKind::This ||
			    capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
				current_lambda_context_.captures.insert(StringTable::getOrInternStringHandle("this"sv));
				current_lambda_context_.capture_kinds[StringTable::getOrInternStringHandle("this"sv)] = capture.kind();
				if (capture.kind() == LambdaCaptureNode::CaptureKind::CopyThis) {
					current_lambda_context_.has_copy_this = true;
				} else if (capture.kind() == LambdaCaptureNode::CaptureKind::This) {
					current_lambda_context_.has_this_pointer = true;
				}
			} else if (capture.has_initializer()) {
				// Init-capture: infer type from initializer expression or closure struct member
				// For init-capture by reference [&y = x], look up x's type
				const ASTNode& init_node = *capture.initializer();
				if (init_node.is<IdentifierNode>()) {
					// Simple identifier like [&y = x] - look up x's type
					const auto& init_id = init_node.as<IdentifierNode>();
					std::optional<ASTNode> init_symbol = symbol_table.lookup(init_id.name());
					if (init_symbol.has_value()) {
						const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
						if (init_decl) {
							current_lambda_context_.capture_types[var_name] = init_decl->type_node().as<TypeSpecifierNode>();
						}
					}
				} else if (init_node.is<ExpressionNode>()) {
					const auto& expr_node = init_node.as<ExpressionNode>();
					if (std::holds_alternative<IdentifierNode>(expr_node)) {
						const auto& init_id = std::get<IdentifierNode>(expr_node);
						std::optional<ASTNode> init_symbol = symbol_table.lookup(init_id.name());
						if (init_symbol.has_value()) {
							const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol);
							if (init_decl) {
								current_lambda_context_.capture_types[var_name] = init_decl->type_node().as<TypeSpecifierNode>();
							}
						}
					}
				}
				// If type still not set, try to get it from closure struct member
				if (current_lambda_context_.capture_types.find(var_name) == current_lambda_context_.capture_types.end()) {
					auto type_it = gTypesByName.find(current_lambda_context_.closure_type);
					if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
						const StructTypeInfo* struct_info = type_it->second->getStructInfo();
						if (struct_info) {
							const StructMember* member = struct_info->findMember(std::string_view(StringTable::getStringView(var_name)));
							if (member) {
								// Create a TypeSpecifierNode from the member type
								TypeSpecifierNode member_type(member->type, TypeQualifier::None, static_cast<int>(member->size * 8));
								if (member->type == Type::Struct) {
									// Need to set type_index for struct types
									member_type = TypeSpecifierNode(member->type, member->type_index, static_cast<int>(member->size * 8), Token());
								}
								current_lambda_context_.capture_types[var_name] = member_type;
							}
						}
					}
				}
			} else {
				if (capture_index < lambda_info.captured_var_decls.size()) {
					const ASTNode& var_decl = lambda_info.captured_var_decls[capture_index];
					if (const DeclarationNode* decl = get_decl_from_symbol(var_decl)) {
						current_lambda_context_.capture_types[var_name] = decl->type_node().as<TypeSpecifierNode>();
					}
				}
				capture_index++;
			}
		}
		if (!current_lambda_context_.has_copy_this) {
			if (const StructTypeInfo* closure = getCurrentClosureStruct()) {
				if (closure->findMember("__copy_this")) {
					current_lambda_context_.has_copy_this = true;
				}
			}
		}
	}

	void popLambdaContext() {
		if (lambda_context_stack_.empty()) {
			current_lambda_context_ = {};
			return;
		}
		current_lambda_context_ = lambda_context_stack_.back();
		lambda_context_stack_.pop_back();
	}

	// Emit IR to load __this pointer from current lambda closure into a TempVar.
	// Returns the TempVar holding the this pointer, or std::nullopt if not applicable.
	std::optional<TempVar> emitLoadThisPointer(const Token& token) {
		if (!isInThisPointerLambda()) {
			return std::nullopt;
		}

		int this_member_offset = getClosureMemberOffset("__this");

		TempVar this_ptr = var_counter.next();
		MemberLoadOp load_op;
		load_op.result.value = this_ptr;
		load_op.result.type = Type::Void;
		load_op.result.size_in_bits = 64;
		load_op.object = StringTable::getOrInternStringHandle("this");  // Lambda's this (the closure)
		load_op.member_name = StringTable::getOrInternStringHandle("__this");
		load_op.offset = this_member_offset;
		load_op.is_reference = false;
		load_op.is_rvalue_reference = false;
		load_op.struct_type_info = nullptr;
		ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), token));

		return this_ptr;
	}

	// ========== Auto Type Deduction Helpers ==========

	// Try to extract a LambdaExpressionNode from an initializer ASTNode.
	// Returns nullptr if the node is not a lambda expression.
	static const LambdaExpressionNode* extractLambdaFromInitializer(const ASTNode& init) {
		if (init.is<LambdaExpressionNode>()) {
			return &init.as<LambdaExpressionNode>();
		}
		if (init.is<ExpressionNode>()) {
			const ExpressionNode& expr = init.as<ExpressionNode>();
			if (std::holds_alternative<LambdaExpressionNode>(expr)) {
				return &std::get<LambdaExpressionNode>(expr);
			}
		}
		return nullptr;
	}

	// Deduce the actual closure type from an auto-typed lambda variable.
	// Given a symbol from the symbol table, if it's an auto-typed variable
	// initialized with a lambda, returns the TypeSpecifierNode for the closure struct.
	// Returns std::nullopt if type cannot be deduced.
	std::optional<TypeSpecifierNode> deduceLambdaClosureType(const ASTNode& symbol,
	                                                          const Token& fallback_token) const {
		if (!symbol.is<VariableDeclarationNode>()) {
			return std::nullopt;
		}
		const VariableDeclarationNode& var_decl = symbol.as<VariableDeclarationNode>();
		const std::optional<ASTNode>& init_opt = var_decl.initializer();
		if (!init_opt.has_value()) {
			return std::nullopt;
		}

		const LambdaExpressionNode* lambda_ptr = extractLambdaFromInitializer(*init_opt);
		if (!lambda_ptr) {
			return std::nullopt;
		}

		StringHandle closure_type_name = lambda_ptr->generate_lambda_name();
		auto type_it = gTypesByName.find(closure_type_name);
		if (type_it == gTypesByName.end()) {
			return std::nullopt;
		}

		const TypeInfo* closure_type = type_it->second;
		int closure_size = closure_type->getStructInfo()
			? closure_type->getStructInfo()->total_size * 8
			: 64;
		return TypeSpecifierNode(
			Type::Struct,
			closure_type->type_index_,
			closure_size,
			fallback_token
		);
	}

	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
		if (!node.get_definition().has_value() && !node.is_implicit()) {
			return;
		}

		struct NamespaceStackGuard {
			std::vector<std::string>& target;
			std::vector<std::string> saved;
			explicit NamespaceStackGuard(std::vector<std::string>& stack)
				: target(stack), saved(stack) {}
			~NamespaceStackGuard() { target = std::move(saved); }
		} namespace_guard{ current_namespace_stack_ };

		// Deferred or synthesized function generation can lose namespace stack context.
		// Recover it from the declaration registry so unqualified lookup remains standard-compliant.
		if (current_namespace_stack_.empty() && global_symbol_table_) {
			if (auto ns_handle = global_symbol_table_->find_namespace_of_function(node); ns_handle.has_value() && !ns_handle->isGlobal()) {
				std::vector<NamespaceHandle> namespace_path;
				NamespaceHandle current = *ns_handle;
				while (current.isValid() && !current.isGlobal()) {
					namespace_path.push_back(current);
					current = gNamespaceRegistry.getParent(current);
				}
				for (auto it = namespace_path.rbegin(); it != namespace_path.rend(); ++it) {
					current_namespace_stack_.emplace_back(gNamespaceRegistry.getName(*it));
				}
			}
		}

		// Reset the temporary variable counter for each new function
		// For non-static member functions, reserve TempVar(1) for the implicit 'this' parameter
		// Static member functions have no 'this' pointer
		var_counter = (node.is_member_function() && !node.is_static()) ? TempVar(2) : TempVar();

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function name for static local variable mangling
		const DeclarationNode& func_decl = node.decl_node();
		const std::string_view func_name_view = func_decl.identifier_token().value();
		current_function_name_ = StringTable::getOrInternStringHandle(func_name_view);
		
		// Set current function return type and size for type checking in return statements
		const TypeSpecifierNode& ret_type_spec = func_decl.type_node().as<TypeSpecifierNode>();
		current_function_return_type_ = ret_type_spec.type();
		current_function_returns_reference_ = ret_type_spec.is_reference();
		
		// Get actual return size - for struct types, TypeSpecifierNode.size_in_bits() may be 0
		// so we need to look it up from gTypeInfo using the type_index
		int actual_ret_size = static_cast<int>(ret_type_spec.size_in_bits());
		if (actual_ret_size == 0 && ret_type_spec.type() == Type::Struct && ret_type_spec.type_index() > 0) {
			// Look up struct size from type info
			if (ret_type_spec.type_index() < gTypeInfo.size() && gTypeInfo[ret_type_spec.type_index()].struct_info_) {
				actual_ret_size = static_cast<int>(gTypeInfo[ret_type_spec.type_index()].struct_info_->total_size * 8);
			}
		}
		
		// For pointer return types or reference return types, use 64-bit size (pointer size on x64)
		// References are represented as pointers at the IR level
		current_function_return_size_ = (ret_type_spec.pointer_depth() > 0 || ret_type_spec.is_reference()) 
			? 64 
			: actual_ret_size;

		// Set or clear current_struct_name_ based on whether this is a member function
		// This is critical for member variable lookup in generateIdentifierIr
		if (node.is_member_function()) {
			// For member functions, set current_struct_name_ from parent_struct_name
			// Use the parent_struct_name directly (simple name like "Test") rather than
			// looking up the TypeInfo's name (which may be namespace-qualified like "ns::Test").
			// The namespace will be added during mangling from current_namespace_stack_.
			std::string_view parent_name = node.parent_struct_name();
			// If parent_struct_name is a template pattern but we have a valid struct context
			// from visitStructDeclarationNode, keep the struct context (instantiated name)
			if (!parent_name.empty() && !gTemplateRegistry.isPatternStructName(StringTable::getOrInternStringHandle(parent_name))) {
				current_struct_name_ = StringTable::getOrInternStringHandle(parent_name);
			}
			// else: keep current_struct_name_ from visitStructDeclarationNode context
		} else if (!current_struct_name_.isValid()) {
			// Clear current_struct_name_ only if we don't already have a struct context
			// (e.g., from visitStructDeclarationNode visiting this function as a member).
			// Template instantiation may not set is_member_function_ on pattern-derived functions.
			current_struct_name_ = StringHandle();
		}

		if (FLASH_LOG_ENABLED(Codegen, Debug)) {
			const TypeSpecifierNode& debug_ret_type = func_decl.type_node().as<TypeSpecifierNode>();
			FLASH_LOG(Codegen, Debug, "===== CODEGEN visitFunctionDeclarationNode: ", func_decl.identifier_token().value(), " =====");
			FLASH_LOG(Codegen, Debug, "  return_type: ", (int)debug_ret_type.type(), " size: ", (int)debug_ret_type.size_in_bits(), " ptr_depth: ", debug_ret_type.pointer_depth(), " is_ref: ", debug_ret_type.is_reference(), " is_rvalue_ref: ", debug_ret_type.is_rvalue_reference());
			FLASH_LOG(Codegen, Debug, "  is_member_function: ", node.is_member_function());
			if (node.is_member_function()) {
				FLASH_LOG(Codegen, Debug, "  parent_struct_name: ", node.parent_struct_name());
			}
			FLASH_LOG(Codegen, Debug, "  parameter_count: ", node.parameter_nodes().size());
			for (size_t i = 0; i < node.parameter_nodes().size(); ++i) {
				const auto& param = node.parameter_nodes()[i];
				if (param.is<DeclarationNode>()) {
					const DeclarationNode& param_decl = param.as<DeclarationNode>();
					const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
					FLASH_LOG(Codegen, Debug, "  param[", i, "]: name='", param_decl.identifier_token().value()
							  , "' type=", (int)param_type.type() 
							  , " size=", (int)param_type.size_in_bits()
							  , " ptr_depth=", param_type.pointer_depth()
							  , " base_cv=", (int)param_type.cv_qualifier()
							  , " is_ref=", param_type.is_reference()
							  , " is_rvalue_ref=", param_type.is_rvalue_reference());
					for (size_t j = 0; j < param_type.pointer_levels().size(); ++j) {
						FLASH_LOG(Codegen, Debug, " ptr[", j, "]_cv=", (int)param_type.pointer_levels()[j].cv_qualifier);
					}
				}
			}
			FLASH_LOG(Codegen, Debug, "=====");
		}

		// Clear static local names map for new function
		static_local_names_.clear();

		const TypeSpecifierNode& ret_type = func_decl.type_node().as<TypeSpecifierNode>();

		// Create function declaration with return type and name
		// Use FunctionDeclOp to store typed payload
		FunctionDeclOp func_decl_op;
		
		// Return type information
		func_decl_op.return_type = ret_type.type();
		
		// Get actual return size - for struct types, TypeSpecifierNode.size_in_bits() may be 0
		// so we need to look it up from gTypeInfo using the type_index
		int actual_return_size = static_cast<int>(ret_type.size_in_bits());
		if (actual_return_size == 0 && ret_type.type() == Type::Struct && ret_type.type_index() > 0) {
			// Look up struct size from type info
			if (ret_type.type_index() < gTypeInfo.size() && gTypeInfo[ret_type.type_index()].struct_info_) {
				actual_return_size = static_cast<int>(gTypeInfo[ret_type.type_index()].struct_info_->total_size * 8);
			}
		}
		
		// For pointer return types, use 64-bit size (pointer size on x64)
		// For reference return types, keep the base type size (the reference itself is 64-bit at ABI level,
		// but we display it as the base type with a reference qualifier)
		func_decl_op.return_size_in_bits = (ret_type.pointer_depth() > 0) 
			? 64 
			: actual_return_size;
		func_decl_op.return_pointer_depth = ret_type.pointer_depth();
		func_decl_op.return_type_index = ret_type.type_index();
		func_decl_op.returns_reference = ret_type.is_reference();
		func_decl_op.returns_rvalue_reference = ret_type.is_rvalue_reference();
		
		// Detect if function returns struct by value (needs hidden return parameter for RVO/NRVO)
		// Only non-pointer, non-reference struct returns need this (pointer/reference returns are in RAX like regular pointers)
		bool returns_struct_by_value = returnsStructByValue(ret_type.type(), ret_type.pointer_depth(), ret_type.is_reference());
		bool needs_hidden_return_param = needsHiddenReturnParam(ret_type.type(), ret_type.pointer_depth(), ret_type.is_reference(), actual_return_size, context_->isLLP64());
		func_decl_op.has_hidden_return_param = needs_hidden_return_param;
		
		// Track return type index and hidden parameter flag for current function context
		current_function_return_type_index_ = ret_type.type_index();
		current_function_has_hidden_return_param_ = needs_hidden_return_param;
		
		if (returns_struct_by_value) {
			if (needs_hidden_return_param) {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Function {} returns struct by value (size={} bits) - will use hidden return parameter (RVO/NRVO)",
					func_decl.identifier_token().value(), ret_type.size_in_bits());
			} else {
				FLASH_LOG_FORMAT(Codegen, Debug,
					"Function {} returns small struct by value (size={} bits) - will return in RAX",
					func_decl.identifier_token().value(), ret_type.size_in_bits());
			}
		}
		
		// Function name
		func_decl_op.function_name = func_decl.identifier_token().handle();

		// Add struct/class name for member functions
		// Use current_struct_name_ if set (for instantiated template specializations),
		// otherwise use the function node's parent_struct_name
		// For nested classes, we need to use the fully qualified name from TypeInfo
		std::string_view struct_name_for_function;
		if (current_struct_name_.isValid()) {
			struct_name_for_function = StringTable::getStringView(current_struct_name_);
		} else if (node.is_member_function()) {
			struct_name_for_function = node.parent_struct_name();
		} else {
			struct_name_for_function = ""sv;
		}
		func_decl_op.struct_name = StringTable::getOrInternStringHandle(struct_name_for_function);
		
		// Linkage and variadic flag
		func_decl_op.linkage = node.linkage();
		func_decl_op.is_variadic = node.is_variadic();
		func_decl_op.is_static_member = node.is_static();
		
		// Member functions defined inside the class body are implicitly inline (C++ standard)
		// Mark them as inline so they get weak linkage in the object file to allow duplicate definitions
		// This includes constructors, destructors, and regular member functions defined inline
		// Also mark functions in std namespace as inline to handle standard library functions that
		// are defined in headers (like std::abs) and may be instantiated multiple times
		bool is_in_std_namespace = false;
		if (!current_namespace_stack_.empty()) {
			is_in_std_namespace = (current_namespace_stack_[0] == "std");
		}
		func_decl_op.is_inline = node.is_member_function() || is_in_std_namespace;

		// Use pre-computed mangled name from AST node if available (Phase 6 migration)
		// Fall back to generating it here if not (for backward compatibility during migration)
		std::string_view mangled_name;
		
		// Don't pass namespace_stack when struct_name already includes the namespace
		// (e.g., "std::simple" already has the namespace embedded, so we shouldn't also pass ["std"])
		// This avoids double-encoding the namespace in the mangled name
		std::vector<std::string> namespace_for_mangling;
		if (struct_name_for_function.find("::") == std::string_view::npos) {
			// struct_name doesn't contain namespace, use current_namespace_stack_
			namespace_for_mangling = current_namespace_stack_;
		}
		// else: struct_name already contains namespace prefix, don't add it again
		
		if (node.has_mangled_name()) {
			mangled_name = node.mangled_name();
		} else if (node.has_non_type_template_args()) {
			// Generate mangled name with template arguments for template specializations (e.g., get<0>)
			const TypeSpecifierNode& return_type = func_decl.type_node().as<TypeSpecifierNode>();
			std::vector<TypeSpecifierNode> param_types;
			for (const auto& param : node.parameter_nodes()) {
				param_types.push_back(param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
			}
			auto mangled = NameMangling::generateMangledNameWithTemplateArgs(
				func_decl.identifier_token().value(), return_type, param_types, 
				node.non_type_template_args(), node.is_variadic(), 
				struct_name_for_function, namespace_for_mangling);
			mangled_name = mangled.view();
		} else {
			// Generate mangled name using the FunctionDeclarationNode overload
			mangled_name = generateMangledNameForCall(node, struct_name_for_function, namespace_for_mangling);
		}
		func_decl_op.mangled_name = StringTable::getOrInternStringHandle(mangled_name);

		// Skip duplicate function definitions to prevent multiple codegen of the same function
		// This is especially important for inline functions from standard headers (like std::abs)
		// that may be parsed multiple times
		if (generated_function_names_.count(func_decl_op.mangled_name) > 0) {
			FLASH_LOG(Codegen, Debug, "Skipping duplicate function definition: ", func_decl.identifier_token().value(), " (", mangled_name, ")");
			return;
		}
		generated_function_names_.insert(func_decl_op.mangled_name);

		// Add parameters to function declaration
		std::vector<CachedParamInfo> cached_params;
		cached_params.reserve(node.parameter_nodes().size());
		size_t unnamed_param_counter = 0;  // Counter for generating unique names for unnamed parameters
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			FunctionParam param_info;
			param_info.type = param_type.type();
			param_info.size_in_bits = static_cast<int>(param_type.size_in_bits());
			
			// Lvalue references (&) are treated like pointers in the IR (address at the ABI level)
			int pointer_depth = static_cast<int>(param_type.pointer_depth());
			if (param_type.is_lvalue_reference()) {
				pointer_depth += 1;  // Add 1 for lvalue reference (ABI treats it as an additional pointer level)
			}
			// Note: Rvalue references (T&&) are tracked separately via is_rvalue_reference flag.
			// While lvalue references are always implemented as pointers at the ABI level,
			// rvalue references in the context of perfect forwarding can receive values directly
			// when bound to temporaries/literals. The pointer_depth increment is omitted to allow
			// this direct value passing, while the is_rvalue_reference flag enables proper handling
			// in both the caller (materialization + address-taking) and callee (dereferencing).
			param_info.pointer_depth = pointer_depth;
			
			// Handle unnamed parameters (e.g., `operator=(const T&) = default;` without explicit param name)
			// Generate a unique name like "__param_0", "__param_1", etc. for unnamed parameters
			std::string_view param_name = param_decl.identifier_token().value();
			if (param_name.empty()) {
				// For defaulted operators (operator=, operator<=>, and synthesized comparison operators),
				// use "other" as the conventional name for the first parameter.
				std::string_view func_name_for_param = func_decl.identifier_token().value();
				bool is_defaulted_operator = unnamed_param_counter == 0 && (
					func_name_for_param == "operator=" ||
					func_name_for_param == "operator<=>" ||
					func_name_for_param == "operator==" ||
					func_name_for_param == "operator!=" ||
					func_name_for_param == "operator<" ||
					func_name_for_param == "operator>" ||
					func_name_for_param == "operator<=" ||
					func_name_for_param == "operator>=");
				if (is_defaulted_operator) {
					param_info.name = StringTable::getOrInternStringHandle("other");
				} else {
					// Generate unique name for unnamed parameter
					param_info.name = StringTable::getOrInternStringHandle(
						StringBuilder().append("__param_").append(unnamed_param_counter).commit());
				}
				unnamed_param_counter++;
			} else {
				param_info.name = StringTable::getOrInternStringHandle(param_name);
			}
			
			param_info.is_reference = param_type.is_reference();  // Tracks ANY reference (lvalue or rvalue)
			param_info.is_rvalue_reference = param_type.is_rvalue_reference();  // Specific rvalue ref flag
			param_info.cv_qualifier = param_type.cv_qualifier();

			func_decl_op.parameters.push_back(std::move(param_info));
			var_counter.next();

			CachedParamInfo cache_entry;
			cache_entry.is_reference = param_type.is_reference();
			cache_entry.is_rvalue_reference = param_type.is_rvalue_reference();
			cache_entry.is_parameter_pack = param_decl.is_parameter_pack();
			cached_params.push_back(cache_entry);
		}

		// Store cached parameter info keyed by mangled function name
		StringHandle cache_key = func_decl_op.mangled_name.isValid()
			? func_decl_op.mangled_name
			: func_decl.identifier_token().handle();
		function_param_cache_[cache_key] = std::move(cached_params);

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(func_decl_op), func_decl.identifier_token()));

		// Generate memberwise three-way comparison for defaulted operator<=>
		if (func_name_view == "operator<=>" && node.is_implicit()) {
			// Set up function scope and 'this' pointer
			symbol_table.enter_scope(ScopeType::Function);
			if (node.is_member_function()) {
				auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
				if (type_it != gTypesByName.end()) {
					const TypeInfo* struct_type_info = type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();
					if (struct_info) {
						Token this_token = func_decl.identifier_token();
						auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
							Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
						this_type.as<TypeSpecifierNode>().add_pointer_level();
						auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);
						symbol_table.insert("this"sv, this_decl);
					}
				}
			}
			for (const auto& param : node.parameter_nodes()) {
				symbol_table.insert(param.as<DeclarationNode>().identifier_token().value(), param);
			}

			// Look up struct info
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
			if (type_it != gTypesByName.end()) {
				const TypeInfo* struct_type_info = type_it->second;
				const StructTypeInfo* struct_info = struct_type_info->getStructInfo();
				if (struct_info && !struct_info->members.empty()) {
					StringHandle this_handle = StringTable::getOrInternStringHandle("this");
					StringHandle other_handle;
					if (!node.parameter_nodes().empty()) {
						std::string_view param_name = node.parameter_nodes()[0].as<DeclarationNode>().identifier_token().value();
						if (!param_name.empty()) {
							other_handle = StringTable::getOrInternStringHandle(param_name);
						}
					}
					if (!other_handle.isValid()) {
						other_handle = StringTable::getOrInternStringHandle("other");
					}

					static size_t spaceship_counter = 0;
					size_t current_spaceship = spaceship_counter++;

					for (size_t mi = 0; mi < struct_info->members.size(); ++mi) {
						const auto& member = struct_info->members[mi];
						int member_bits = static_cast<int>(member.size * 8);

						// Labels for this member's comparison
						auto diff_label = StringTable::createStringHandle(
							StringBuilder().append("spaceship_diff_").append(current_spaceship).append("_").append(mi));
						auto lt_label = StringTable::createStringHandle(
							StringBuilder().append("spaceship_lt_").append(current_spaceship).append("_").append(mi));
						auto gt_label = StringTable::createStringHandle(
							StringBuilder().append("spaceship_gt_").append(current_spaceship).append("_").append(mi));
						auto next_label = StringTable::createStringHandle(
							StringBuilder().append("spaceship_next_").append(current_spaceship).append("_").append(mi));

						// For struct members, delegate to the member's operator<=>
						if (member.type == Type::Struct && member.type_index > 0 && member.type_index < gTypeInfo.size()) {
							const TypeInfo& member_type_info = gTypeInfo[member.type_index];
							const StructTypeInfo* member_struct_info = member_type_info.getStructInfo();

							// Find operator<=> in the member struct and generate its mangled name
							StringHandle member_spaceship_mangled;
							if (member_struct_info) {
								for (const auto& mf : member_struct_info->member_functions) {
									if (mf.is_operator_overload && mf.operator_symbol == "<=>") {
										if (mf.function_decl.is<FunctionDeclarationNode>()) {
											const auto& spaceship_func = mf.function_decl.as<FunctionDeclarationNode>();
											// Use generateMangledNameForCall for consistent mangling across platforms
											std::string_view member_struct_name = StringTable::getStringView(member_type_info.name());
											member_spaceship_mangled = StringTable::getOrInternStringHandle(
												generateMangledNameForCall(spaceship_func, member_struct_name));
										}
										break;
									}
								}
							}

							if (member_spaceship_mangled.isValid()) {
								// Load addresses of this->member and other.member for the call
								TempVar lhs_val = var_counter.next();
								MemberLoadOp lhs_load;
								lhs_load.result.value = lhs_val;
								lhs_load.result.type = member.type;
								lhs_load.result.size_in_bits = member_bits;
								lhs_load.object = this_handle;
								lhs_load.member_name = member.getName();
								lhs_load.offset = static_cast<int>(member.offset);
								lhs_load.is_reference = member.is_reference;
								lhs_load.is_rvalue_reference = member.is_rvalue_reference;
								lhs_load.struct_type_info = nullptr;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(lhs_load), func_decl.identifier_token()));

								TempVar rhs_val = var_counter.next();
								MemberLoadOp rhs_load;
								rhs_load.result.value = rhs_val;
								rhs_load.result.type = member.type;
								rhs_load.result.size_in_bits = member_bits;
								rhs_load.object = other_handle;
								rhs_load.member_name = member.getName();
								rhs_load.offset = static_cast<int>(member.offset);
								rhs_load.is_reference = member.is_reference;
								rhs_load.is_rvalue_reference = member.is_rvalue_reference;
								rhs_load.struct_type_info = nullptr;
								ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(rhs_load), func_decl.identifier_token()));

								// Call member's operator<=>(this->member, other.member)
								TempVar call_result = var_counter.next();
								CallOp call_op;
								call_op.function_name = member_spaceship_mangled;
								call_op.is_member_function = true;
								call_op.return_type = Type::Int;
								call_op.return_size_in_bits = 32;
								call_op.result = call_result;

								TypedValue lhs_arg;
								lhs_arg.type = Type::Struct;
								lhs_arg.size_in_bits = 64;
								lhs_arg.value = lhs_val;
								lhs_arg.pointer_depth = 1;
								call_op.args.push_back(std::move(lhs_arg));

								TypedValue rhs_arg;
								rhs_arg.type = Type::Struct;
								rhs_arg.size_in_bits = 64;
								rhs_arg.value = rhs_val;
								rhs_arg.ref_qualifier = ReferenceQualifier::LValueReference;
								call_op.args.push_back(std::move(rhs_arg));

								ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), func_decl.identifier_token()));

								// Check if result != 0 (members not equal)
								TempVar ne_result = var_counter.next();
								BinaryOp ne_op{
									.lhs = TypedValue{.type = Type::Int, .size_in_bits = 32, .value = IrValue{call_result}, .is_signed = true},
									.rhs = TypedValue{.type = Type::Int, .size_in_bits = 32, .value = IrValue{0ULL}, .is_signed = true},
									.result = IrValue{ne_result}
								};
								ir_.addInstruction(IrInstruction(IrOpcode::NotEqual, std::move(ne_op), func_decl.identifier_token()));

								// Branch: if not equal, return the result directly
								CondBranchOp ne_branch;
								ne_branch.label_true = diff_label;
								ne_branch.label_false = next_label;
								ne_branch.condition = TypedValue{.type = Type::Bool, .size_in_bits = 8, .value = IrValue{ne_result}};
								ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(ne_branch), func_decl.identifier_token()));

								// Label: diff - return the inner <=> result
								ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = diff_label}, func_decl.identifier_token()));
								{
									ReturnOp ret_inner;
									ret_inner.return_value = IrValue{call_result};
									ret_inner.return_type = Type::Int;
									ret_inner.return_size = 32;
									ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_inner), func_decl.identifier_token()));
								}

								// Label: next - continue to next member
								ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = next_label}, func_decl.identifier_token()));
								continue;
							}
							// Fall through to primitive comparison if no operator<=> found
						}

						// Primitive member comparison
						TempVar lhs_val = var_counter.next();
						MemberLoadOp lhs_load;
						lhs_load.result.value = lhs_val;
						lhs_load.result.type = member.type;
						lhs_load.result.size_in_bits = member_bits;
						lhs_load.object = this_handle;
						lhs_load.member_name = member.getName();
						lhs_load.offset = static_cast<int>(member.offset);
						lhs_load.is_reference = member.is_reference;
						lhs_load.is_rvalue_reference = member.is_rvalue_reference;
						lhs_load.struct_type_info = nullptr;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(lhs_load), func_decl.identifier_token()));

						TempVar rhs_val = var_counter.next();
						MemberLoadOp rhs_load;
						rhs_load.result.value = rhs_val;
						rhs_load.result.type = member.type;
						rhs_load.result.size_in_bits = member_bits;
						rhs_load.object = other_handle;
						rhs_load.member_name = member.getName();
						rhs_load.offset = static_cast<int>(member.offset);
						rhs_load.is_reference = member.is_reference;
						rhs_load.is_rvalue_reference = member.is_rvalue_reference;
						rhs_load.struct_type_info = nullptr;
						ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(rhs_load), func_decl.identifier_token()));

						// Compare: lhs != rhs
						TempVar ne_result = var_counter.next();
						BinaryOp ne_op{
							.lhs = TypedValue{.type = member.type, .size_in_bits = member_bits, .value = IrValue{lhs_val}, .is_signed = isSignedType(member.type)},
							.rhs = TypedValue{.type = member.type, .size_in_bits = member_bits, .value = IrValue{rhs_val}, .is_signed = isSignedType(member.type)},
							.result = IrValue{ne_result}
						};
						ir_.addInstruction(IrInstruction(IrOpcode::NotEqual, std::move(ne_op), func_decl.identifier_token()));

						// Branch: if not equal, go to diff handling
						CondBranchOp ne_branch;
						ne_branch.label_true = diff_label;
						ne_branch.label_false = next_label;
						ne_branch.condition = TypedValue{.type = Type::Bool, .size_in_bits = 8, .value = IrValue{ne_result}};
						ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(ne_branch), func_decl.identifier_token()));

						// Label: diff - members are not equal
						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = diff_label}, func_decl.identifier_token()));

						// Compare: lhs < rhs
						TempVar lt_result = var_counter.next();
						BinaryOp lt_op{
							.lhs = TypedValue{.type = member.type, .size_in_bits = member_bits, .value = IrValue{lhs_val}, .is_signed = isSignedType(member.type)},
							.rhs = TypedValue{.type = member.type, .size_in_bits = member_bits, .value = IrValue{rhs_val}, .is_signed = isSignedType(member.type)},
							.result = IrValue{lt_result}
						};
						ir_.addInstruction(IrInstruction(IrOpcode::LessThan, std::move(lt_op), func_decl.identifier_token()));

						// Branch: if lhs < rhs, return -1, else return 1
						CondBranchOp lt_branch;
						lt_branch.label_true = lt_label;
						lt_branch.label_false = gt_label;
						lt_branch.condition = TypedValue{.type = Type::Bool, .size_in_bits = 8, .value = IrValue{lt_result}};
						ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(lt_branch), func_decl.identifier_token()));

						// Label: lt - return -1 (two's complement: 0xFFFFFFFF in 32-bit)
						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = lt_label}, func_decl.identifier_token()));
						{
							ReturnOp ret_neg;
							ret_neg.return_value = IrValue{0xFFFFFFFFULL};
							ret_neg.return_type = Type::Int;
							ret_neg.return_size = 32;
							ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_neg), func_decl.identifier_token()));
						}

						// Label: gt - return 1
						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = gt_label}, func_decl.identifier_token()));
						{
							ReturnOp ret_pos;
							ret_pos.return_value = IrValue{1ULL};
							ret_pos.return_type = Type::Int;
							ret_pos.return_size = 32;
							ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_pos), func_decl.identifier_token()));
						}

						// Label: next - continue to next member
						ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = next_label}, func_decl.identifier_token()));
					}
				}
			}

			// All members equal - return 0
			ReturnOp ret_zero;
			ret_zero.return_value = IrValue{0ULL};
			ret_zero.return_type = Type::Int;
			ret_zero.return_size = 32;
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_zero), func_decl.identifier_token()));
			symbol_table.exit_scope();
			return;
		}

		// Synthesized comparison operators from operator<=> - generate memberwise comparison directly
		// Determine comparison opcode once from the operator name
		std::optional<IrOpcode> synthesized_cmp_opcode;
		if (node.is_implicit()) {
			if (func_name_view == "operator==") { synthesized_cmp_opcode = IrOpcode::Equal; }
			else if (func_name_view == "operator!=") { synthesized_cmp_opcode = IrOpcode::NotEqual; }
			else if (func_name_view == "operator<") { synthesized_cmp_opcode = IrOpcode::LessThan; }
			else if (func_name_view == "operator>") { synthesized_cmp_opcode = IrOpcode::GreaterThan; }
			else if (func_name_view == "operator<=") { synthesized_cmp_opcode = IrOpcode::LessEqual; }
			else if (func_name_view == "operator>=") { synthesized_cmp_opcode = IrOpcode::GreaterEqual; }
		}
		if (synthesized_cmp_opcode) {
			// Instead of processing the parser-generated body (which has auto return type issues),
			// generate direct memberwise comparison. This calls operator<=> and compares result with 0.
			symbol_table.enter_scope(ScopeType::Function);
			if (node.is_member_function()) {
				auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
				if (type_it != gTypesByName.end()) {
					const TypeInfo* struct_type_info = type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();
					if (struct_info) {
						Token this_token = func_decl.identifier_token();
						auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
							Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
						this_type.as<TypeSpecifierNode>().add_pointer_level();
						auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);
						symbol_table.insert("this"sv, this_decl);
					}
				}
			}
			for (const auto& param : node.parameter_nodes()) {
				std::string_view pname = param.as<DeclarationNode>().identifier_token().value();
				if (!pname.empty()) {
					symbol_table.insert(pname, param);
				}
			}

			// Find the operator<=> to call it - generate mangled name from the function signature
			// (AST mangled name may not be set for user-defined operator<=>)
			StringHandle spaceship_mangled;
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
			if (type_it != gTypesByName.end()) {
				const StructTypeInfo* struct_info = type_it->second->getStructInfo();
				if (struct_info) {
					for (const auto& mf : struct_info->member_functions) {
						if (mf.is_operator_overload && mf.operator_symbol == "<=>") {
							if (mf.function_decl.is<FunctionDeclarationNode>()) {
								const auto& spaceship_func = mf.function_decl.as<FunctionDeclarationNode>();
								// Use generateMangledNameForCall for consistent mangling across platforms
								spaceship_mangled = StringTable::getOrInternStringHandle(
									generateMangledNameForCall(spaceship_func, node.parent_struct_name()));
							}
							break;
						}
					}
				}
			}

			if (spaceship_mangled.isValid()) {
				// Generate: call operator<=>(this, other) -> int result
				TempVar call_result = var_counter.next();
				CallOp call_op;
				call_op.function_name = spaceship_mangled;
				call_op.is_member_function = true;
				call_op.return_type = Type::Int;
				call_op.return_size_in_bits = 32;
				call_op.result = call_result;

				// Pass 'this' as first arg
				StringHandle this_handle = StringTable::getOrInternStringHandle("this");
				TypedValue this_arg;
				this_arg.type = Type::Struct;
				this_arg.size_in_bits = 64;
				this_arg.value = this_handle;
				this_arg.pointer_depth = 1;
				call_op.args.push_back(std::move(this_arg));

				// Pass 'other' as second arg (reference = pointer)
				StringHandle other_handle;
				if (!node.parameter_nodes().empty()) {
					std::string_view param_name = node.parameter_nodes()[0].as<DeclarationNode>().identifier_token().value();
					if (!param_name.empty()) {
						other_handle = StringTable::getOrInternStringHandle(param_name);
					}
				}
				if (!other_handle.isValid()) {
					other_handle = StringTable::getOrInternStringHandle("other");
				}
				TypedValue other_arg;
				other_arg.type = Type::Struct;
				other_arg.size_in_bits = 64;
				other_arg.value = other_handle;
				other_arg.ref_qualifier = ReferenceQualifier::LValueReference;
				call_op.args.push_back(std::move(other_arg));

				ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), func_decl.identifier_token()));

				// Compare result with 0 using the pre-determined comparison opcode
				TempVar cmp_result = var_counter.next();
				BinaryOp cmp_op{
					.lhs = TypedValue{.type = Type::Int, .size_in_bits = 32, .value = IrValue{call_result}, .is_signed = true},
					.rhs = TypedValue{.type = Type::Int, .size_in_bits = 32, .value = IrValue{0ULL}, .is_signed = true},
					.result = IrValue{cmp_result}
				};
				ir_.addInstruction(IrInstruction(*synthesized_cmp_opcode, std::move(cmp_op), func_decl.identifier_token()));

				// Return the boolean result
				ReturnOp ret_op;
				ret_op.return_value = IrValue{cmp_result};
				ret_op.return_type = Type::Bool;
				ret_op.return_size = 8;
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
			} else {
				// Fallback: operator<=> not found, return false for all synthesized operators
				ReturnOp ret_op;
				ret_op.return_value = IrValue{0ULL};
				ret_op.return_type = Type::Bool;
				ret_op.return_size = 8;
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
			}

			symbol_table.exit_scope();
			return;
		}

		symbol_table.enter_scope(ScopeType::Function);

		// For non-static member functions, add implicit 'this' pointer to symbol table
		// Static member functions have no 'this' pointer
		if (node.is_member_function() && !node.is_static()) {
			// Look up the struct type to get its type index and size
			auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
			if (type_it != gTypesByName.end()) {
				const TypeInfo* struct_type_info = type_it->second;
				const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

				if (struct_info) {
					// Create a type specifier for the struct pointer (this is a pointer, so 64 bits)
					Token this_token = func_decl.identifier_token();  // Use function token for location
					auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
						Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
					// Mark 'this' as a pointer to struct (not a struct value)
					this_type.as<TypeSpecifierNode>().add_pointer_level();
					auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);

					// Add 'this' to symbol table (it's the implicit first parameter)
					symbol_table.insert("this"sv, this_decl);
				}
			}
		}

		// Allocate stack space for local variables and parameters
		// Parameters are already in their registers, we just need to allocate space for them
		//size_t paramIndex = 0;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			//const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			symbol_table.insert(param_decl.identifier_token().value(), param);
			//paramIndex++;
		}

		// Check if this is an implicit operator= that needs code generation
		if (node.is_implicit() && node.is_member_function()) {
			std::string_view func_name = func_decl.identifier_token().value();
			if (func_name == "operator=") {
				// This is an implicit copy or move assignment operator
				// Determine which one by checking the parameter type
// 				bool is_move_assignment = false;
// 				if (node.parameter_nodes().size() == 1) {
// 					const auto& param_decl = node.parameter_nodes()[0].as<DeclarationNode>();
// 					const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
// 					if (param_type.is_rvalue_reference()) {
// 						is_move_assignment = true;
// 					}
// 				}

				// Generate memberwise assignment from source parameter to 'this'
				// (same code for both copy and move assignment - memberwise copy/move)
				
				// Get the parameter name from the function declaration
				// For defaulted operator= without explicit parameter name (e.g., `operator=(const T&) = default;`),
				// the parameter name might be empty. Use "other" as the default name.
				// This name must match what's in func_decl_op.parameters.
				StringHandle source_param_name_handle;
				if (!node.parameter_nodes().empty()) {
					const auto& param_node = node.parameter_nodes()[0];
					if (param_node.is<DeclarationNode>()) {
						std::string_view param_name = param_node.as<DeclarationNode>().identifier_token().value();
						if (!param_name.empty()) {
							source_param_name_handle = StringTable::getOrInternStringHandle(param_name);
						}
					}
				}
				// Default to "other" if no parameter name found
				if (!source_param_name_handle.isValid()) {
					source_param_name_handle = StringTable::getOrInternStringHandle("other");
				}

				// Look up the struct type
				auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(node.parent_struct_name()));
				if (type_it != gTypesByName.end()) {
					const TypeInfo* struct_type_info = type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

					if (struct_info) {
						// Generate memberwise assignment
						for (const auto& member : struct_info->members) {
							// First, load the member from source parameter
							TempVar member_value = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = member_value;
							member_load.result.type = member.type;
							member_load.result.size_in_bits = static_cast<int>(member.size * 8);
							member_load.object = source_param_name_handle;  // Load from source parameter
							member_load.member_name = member.getName();
							member_load.offset = static_cast<int>(member.offset);
							member_load.is_reference = member.is_reference;
							member_load.is_rvalue_reference = member.is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), func_decl.identifier_token()));

							// Then, store the member to 'this'
							// Format: [member_type, member_size, object_name, member_name, offset, is_ref, is_rvalue_ref, ref_size_bits, value]
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = StringTable::getOrInternStringHandle("this");
							member_store.member_name = member.getName();
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference;
							member_store.is_rvalue_reference = member.is_rvalue_reference;
							member_store.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), func_decl.identifier_token()));
						}

						// Return *this (the return value is the 'this' pointer dereferenced)
						// Generate: %temp = dereference [Type][Size] %this
						//           return [Type][Size] %temp
						TempVar this_deref = var_counter.next();
						std::vector<IrOperand> deref_operands;
						deref_operands.emplace_back(this_deref);  // result variable
						DereferenceOp deref_op;
						deref_op.result = this_deref;
						deref_op.pointer.type = Type::Struct;
						deref_op.pointer.size_in_bits = 64;  // Pointer is always 64 bits
						deref_op.pointer.value = StringTable::getOrInternStringHandle("this");

						ir_.addInstruction(IrInstruction(IrOpcode::Dereference, std::move(deref_op), func_decl.identifier_token()));

						// Return the dereferenced value
						ReturnOp ret_op;
						ret_op.return_value = this_deref;
						ret_op.return_type = Type::Struct;
						ret_op.return_size = static_cast<int>(struct_info->total_size * 8);
						ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
					}
				}
			}
		} else {
			// User-defined function body
			// Enter a scope for the function body to track destructors
			enterScope();
			const BlockNode& block = node.get_definition().value().as<BlockNode>();
			block.get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		}

		// Exit the function body scope and call destructors before returning
		// Only do this for user-defined function bodies where we called enterScope()
		if (!node.is_implicit() || !node.is_member_function()) {
			exitScope();
		}

		// Add implicit return if needed
		// Check if the last instruction is a return
		bool ends_with_return = false;
		if (!ir_.getInstructions().empty()) {
			const auto& last_instr = ir_.getInstructions().back();
			ends_with_return = (last_instr.getOpcode() == IrOpcode::Return);
		}

		if (!ends_with_return) {
			// Add implicit return for void functions
			if (ret_type.type() == Type::Void) {
				ReturnOp ret_op;  // No return value for void
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
			}
			// Special case: main() implicitly returns 0 if no return statement
			else if (func_decl.identifier_token().value() == "main") {
				ReturnOp ret_op;
				ret_op.return_value = 0ULL;  // Implicit return 0
				ret_op.return_type = Type::Int;
				ret_op.return_size = 32;
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), func_decl.identifier_token()));
			}
			// For other non-void functions, this is an error (missing return statement)
			// TODO: This should be a compile error, but for now we'll allow it
			// Full implementation requires control flow analysis to check all paths
		}

		symbol_table.exit_scope();
		// Don't clear current_function_name_ here - let the top-level visitor manage it
		// This allows nested contexts (like local struct member functions) to work properly
	}

	void visitStructDeclarationNode(const StructDeclarationNode& node) {
		// Struct declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system



		// Skip pattern structs - they're templates and shouldn't generate code
		if (gTemplateRegistry.isPatternStructName(node.name())) {
			return;
		}
		
		// Skip structs with incomplete instantiation - they have unresolved template params
		{
			auto incomplete_it = gTypesByName.find(node.name());
			if (incomplete_it != gTypesByName.end() && incomplete_it->second->is_incomplete_instantiation_) {
				FLASH_LOG(Codegen, Debug, "Skipping struct '", StringTable::getStringView(node.name()), "' (incomplete instantiation)");
				return;
			}
		}

		std::string_view struct_name = StringTable::getStringView(node.name());

		// Generate member functions for both global and local structs
		// Save the enclosing function context so member function visits don't clobber it
		StringHandle saved_enclosing_function = current_function_name_;
		StringHandle saved_struct_name = current_struct_name_;
		
		// Check if this is a local struct (declared inside a function)
		bool is_local_struct = current_function_name_.isValid();
		
		// Set struct context so member functions know which struct they belong to
		// NOTE: We don't clear this until the next struct - the string must persist
		// because IrOperands store string_view references to it
		// For nested classes, we need to use the fully qualified name from TypeInfo
		// If current_struct_name_ is valid, this is a nested class, so construct fully qualified name
		StringHandle lookup_name;
		if (current_struct_name_.isValid()) {
			// This is a nested class - construct fully qualified name like "Outer::Inner"
			StringBuilder qualified_name_builder;
			qualified_name_builder.append(StringTable::getStringView(current_struct_name_))
			                     .append("::")
			                     .append(struct_name);
			lookup_name = StringTable::getOrInternStringHandle(qualified_name_builder.commit());
		} else {
			// Top-level class - first try simple name, then look for namespace-qualified version
			lookup_name = StringTable::getOrInternStringHandle(struct_name);
		}
		
		auto type_it = gTypesByName.find(lookup_name);
		if (type_it != gTypesByName.end()) {
			current_struct_name_ = type_it->second->name();
		} else {
			// If simple name lookup failed, search for namespace-qualified version
			// e.g., for "simple", look for "std::simple" or other qualified names
			bool found_qualified = false;
			for (const auto& [name_handle, type_info] : gTypesByName) {
				std::string_view qualified_name = StringTable::getStringView(name_handle);
				// Check if this name ends with "::" + struct_name
				if (qualified_name.size() > struct_name.size() + 2) {
					size_t expected_pos = qualified_name.size() - struct_name.size();
					if (qualified_name.substr(expected_pos) == struct_name &&
					    qualified_name.substr(expected_pos - 2, 2) == "::") {
						current_struct_name_ = name_handle;
						found_qualified = true;
						break;
					}
				}
			}
			if (!found_qualified) {
				current_struct_name_ = lookup_name;
			}
		}
		
		// For local structs, collect member functions for deferred generation
		// For global structs, visit them immediately
		if (is_local_struct) {
			for (const auto& member_func : node.member_functions()) {
				LocalStructMemberInfo info;
				info.struct_name = current_struct_name_;
				info.enclosing_function_name = saved_enclosing_function;
				info.member_function_node = member_func.function_declaration;
				collected_local_struct_members_.push_back(std::move(info));
			}
		} else {
			FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - visiting members immediately, count=", node.member_functions().size());
			for (const auto& member_func : node.member_functions()) {
				// Each member function can be a FunctionDeclarationNode, ConstructorDeclarationNode, or DestructorDeclarationNode
				FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - processing member function, is_constructor=", member_func.is_constructor);
				try {
					// Call the specific visitor directly instead of visit() to avoid clearing current_function_name_
					const ASTNode& func_decl = member_func.function_declaration;
					if (func_decl.is<FunctionDeclarationNode>()) {
						const auto& fn = func_decl.as<FunctionDeclarationNode>();
						// Skip functions with unresolved auto parameters (abbreviated templates)
						// These will be instantiated when called with concrete types
						bool fn_has_auto = false;
						for (const auto& p : fn.parameter_nodes()) {
							if (p.is<DeclarationNode>()) {
								const auto& pt = p.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
								if (pt.type() == Type::Auto) {
									fn_has_auto = true;
									break;
								}
							}
						}
						if (!fn_has_auto) {
							visitFunctionDeclarationNode(fn);
							// If the function was skipped (lazy stub - no body yet), queue it for
							// deferred lazy instantiation so the body gets generated.
							if (!fn.get_definition().has_value() && !fn.is_implicit() && parser_) {
								StringHandle member_handle = fn.decl_node().identifier_token().handle();
								if (LazyMemberInstantiationRegistry::getInstance().needsInstantiation(current_struct_name_, member_handle)) {
									DeferredMemberFunctionInfo deferred_info;
									deferred_info.struct_name = current_struct_name_;
									deferred_info.function_node = func_decl;
									deferred_member_functions_.push_back(std::move(deferred_info));
									FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - queued lazy member function '",
									          fn.decl_node().identifier_token().value(), "' for deferred instantiation");
								}
							}
						} else {
							FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - skipping member function with auto params (will be instantiated on call)");
						}
					} else if (func_decl.is<ConstructorDeclarationNode>()) {
						const auto& ctor = func_decl.as<ConstructorDeclarationNode>();
						// Skip constructors with unresolved auto parameters (member function templates)
						// These will be instantiated when called with concrete types
						bool ctor_has_auto = false;
						for (const auto& p : ctor.parameter_nodes()) {
							if (p.is<DeclarationNode>()) {
								const auto& pt = p.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
								if (pt.type() == Type::Auto) {
									ctor_has_auto = true;
									break;
								}
							}
						}
						if (!ctor_has_auto) {
							visitConstructorDeclarationNode(ctor);
						} else {
							FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - skipping template constructor with auto params (will be instantiated on call)");
						}
					} else if (func_decl.is<DestructorDeclarationNode>()) {
						visitDestructorDeclarationNode(func_decl.as<DestructorDeclarationNode>());
					} else if (func_decl.is<TemplateFunctionDeclarationNode>()) {
						// For member functions of class template instantiations that are wrapped in
						// TemplateFunctionDeclarationNode. If the inner function has a definition,
						// check if all parameter types are resolved. If any parameter still has
						// Type::Auto, this is a member function template (e.g., abbreviated template
						// from constrained auto) that should only be instantiated when called.
						const auto& tmpl = func_decl.as<TemplateFunctionDeclarationNode>();
						if (tmpl.function_declaration().is<FunctionDeclarationNode>()) {
							const auto& inner_func = tmpl.function_declaration().as<FunctionDeclarationNode>();
							if (inner_func.get_definition().has_value()) {
								// Check if any parameter has unresolved Auto type
								bool has_auto_param = false;
								for (const auto& p : inner_func.parameter_nodes()) {
									if (p.is<DeclarationNode>()) {
										const auto& pt = p.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
										if (pt.type() == Type::Auto) {
											has_auto_param = true;
											break;
										}
									}
								}
								if (!has_auto_param) {
									visitFunctionDeclarationNode(inner_func);
								} else {
									FLASH_LOG(Codegen, Debug, "[STRUCT] ", struct_name, " - skipping member function template with auto params (will be instantiated on call)");
								}
							}
						}
					}
				} catch (const std::exception& ex) {
					FLASH_LOG(Codegen, Error, "Exception while visiting member function in struct ",
					          struct_name, ": ", ex.what());
					throw;
				} catch (...) {
					FLASH_LOG(Codegen, Error, "Unknown exception while visiting member function in struct ",
					          struct_name);
					throw;
				}
			}
		}  // End of if-else for local vs global struct
		
		// Clear current_function_name_ before visiting nested classes
		// Nested classes should not be treated as local structs even if we're inside
		// a member function context (e.g., after visiting constructors which set current_function_name_)
		// Nested classes are always at class scope, not function scope
		current_function_name_ = StringHandle();
		
		// Save current_struct_name_ before visiting nested classes so each nested class
		// gets the correct parent context (important when there are multiple nested classes)
		StringHandle parent_struct_name = current_struct_name_;
			
			// Visit nested classes recursively
			for (const auto& nested_class_node : node.nested_classes()) {
				if (nested_class_node.is<StructDeclarationNode>()) {
					FLASH_LOG(Codegen, Debug, "  Visiting nested class");
					// Restore parent context before each nested class visit
					current_struct_name_ = parent_struct_name;
					visitStructDeclarationNode(nested_class_node.as<StructDeclarationNode>());
				}
			}

			// Generate global storage for static members
			auto static_member_type_it = gTypesByName.find(node.name());
			if (static_member_type_it != gTypesByName.end()) {
				const TypeInfo* type_info = static_member_type_it->second;
				
				// Skip if we've already processed this TypeInfo pointer
				// (same struct can be registered under multiple keys in gTypesByName)
				if (processed_type_infos_.count(type_info) > 0) {
					// Already processed in generateStaticMemberDeclarations() or earlier visit
				} else {
					processed_type_infos_.insert(type_info);
					
					const StructTypeInfo* struct_info = type_info->getStructInfo();
					if (struct_info) {
						for (const auto& static_member : struct_info->static_members) {
							// Build the qualified name for deduplication using type_info->name()
							// This ensures consistency with generateStaticMemberDeclarations() which uses
							// the type name from gTypesByName iterator (important for template instantiations)
							StringBuilder qualified_name_sb;
							qualified_name_sb.append(StringTable::getStringView(type_info->name())).append("::").append(StringTable::getStringView(static_member.getName()));
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
							op.var_name = name_handle;  // Phase 3: Now using StringHandle instead of string_view

							// Check if static member has an initializer
							op.is_initialized = static_member.initializer.has_value();
							if (op.is_initialized) {
								// Evaluate the initializer expression
								auto init_operands = visitExpressionNode(static_member.initializer->as<ExpressionNode>());
								// Convert to raw bytes
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
							ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), Token()));
						}
					}
				}
			}
			// Clear current_struct_name_ for top-level structs
		
			if (current_struct_name_.isValid()) {
				std::string_view current_name = StringTable::getStringView(current_struct_name_);
				if (current_name.find("::") == std::string_view::npos) {
					current_struct_name_ = StringHandle();
				}
			}
		// Restore the enclosing function and struct context
		current_function_name_ = saved_enclosing_function;
		current_struct_name_ = saved_struct_name;
	}

	void visitEnumDeclarationNode([[maybe_unused]] const EnumDeclarationNode& node) {
		// Enum declarations themselves don't generate IR - they just define types
		// The type information is already registered in the global type system
		// Enumerators are treated as compile-time constants and don't need runtime code generation
		// For unscoped enums, the enumerators are already added to the symbol table during parsing
	}

	void visitConstructorDeclarationNode(const ConstructorDeclarationNode& node) {
		// If no definition and not explicit, check if implicit
		if (!node.get_definition().has_value()) {
			if (node.is_implicit()) {
				// Implicit constructors might not have a body if trivial, but we must emit the symbol
				// so the linker can find it if referenced.
				// Proceed to generate an empty function body.
			} else {
				return;
			}
		}

		// Reset the temporary variable counter for each new constructor
		// Constructors are always member functions, so reserve TempVar(1) for 'this'
		var_counter = TempVar(2);

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function name for static local variable mangling
		current_function_name_ = node.name();
		static_local_names_.clear();

		// Create constructor declaration with typed payload
		FunctionDeclOp ctor_decl_op;
		// For nested classes, use current_struct_name_ which contains the fully qualified name
		std::string_view struct_name_for_ctor = current_struct_name_.isValid() ? StringTable::getStringView(current_struct_name_) : StringTable::getStringView(node.struct_name());
		
		// Extract just the last component of the class name for the constructor function name
		// For "Outer::Inner", we want "Inner" as the function name
		std::string_view ctor_function_name = struct_name_for_ctor;
		std::string_view parent_class_name;  // For mangling - all components except the last
		size_t last_colon = struct_name_for_ctor.rfind("::");
		if (last_colon != std::string_view::npos) {
			ctor_function_name = struct_name_for_ctor.substr(last_colon + 2);  // "Inner"
			parent_class_name = struct_name_for_ctor.substr(0, last_colon);     // "Outer"
		} else {
			parent_class_name = struct_name_for_ctor;  // Not nested, use as-is
		}
		
		ctor_decl_op.function_name = StringTable::getOrInternStringHandle(ctor_function_name);  // Constructor name (last component)
		ctor_decl_op.struct_name = StringTable::getOrInternStringHandle(struct_name_for_ctor);  // Struct name for member function (fully qualified)
		ctor_decl_op.return_type = Type::Void;  // Constructors don't have a return type
		ctor_decl_op.return_size_in_bits = 0;  // Size is 0 for void
		ctor_decl_op.return_pointer_depth = 0;  // Pointer depth is 0 for void
		ctor_decl_op.linkage = Linkage::CPlusPlus;  // C++ linkage for constructors
		ctor_decl_op.is_variadic = false;  // Constructors are never variadic
		// Constructors defined inside class body are implicitly inline (C++ standard)
		// Mark them as inline so they get weak linkage in the object file
		ctor_decl_op.is_inline = true;

		// Generate mangled name for constructor
		// For template instantiations, use struct_name_for_ctor which has the correct instantiated name
		// (e.g., "Base_char" instead of "Base")
		{
			std::vector<std::string_view> empty_namespace_path;

			// Use the appropriate mangling based on the style
			if (NameMangling::g_mangling_style == NameMangling::ManglingStyle::MSVC) {
				// MSVC uses dedicated constructor mangling (??0ClassName@@...)
				ctor_decl_op.mangled_name = StringTable::getOrInternStringHandle(
					NameMangling::generateMangledNameForConstructor(struct_name_for_ctor, node.parameter_nodes(), empty_namespace_path)
				);
			} else if (NameMangling::g_mangling_style == NameMangling::ManglingStyle::Itanium) {
				// Itanium uses regular mangling with class name as function name (produces C1 marker)
				TypeSpecifierNode return_type(Type::Void, TypeQualifier::None, 0);
				ctor_decl_op.mangled_name = StringTable::getOrInternStringHandle(NameMangling::generateMangledName(
					ctor_function_name, return_type, node.parameter_nodes(),
					false, struct_name_for_ctor, empty_namespace_path, Linkage::CPlusPlus));
			} else {
				assert(false && "Unhandled name mangling type");
			}
		}
		
		// Note: 'this' pointer is added implicitly by handleFunctionDecl for all member functions
		// We don't add it here to avoid duplication

		// Add parameter types to constructor declaration
		size_t ctor_unnamed_param_counter = 0;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = requireDeclarationNode(param, "ctor decl operands");
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			FunctionParam func_param;
			func_param.type = param_type.type();
			func_param.size_in_bits = static_cast<int>(param_type.size_in_bits());
			func_param.pointer_depth = static_cast<int>(param_type.pointer_depth());
			
			// Handle empty parameter names (e.g., from defaulted constructors)
			std::string_view param_name = param_decl.identifier_token().value();
			if (param_name.empty()) {
				// For copy/move constructors (first parameter is a reference to same struct type),
				// use "other" as the conventional name. This must match the body generation code
				// that references "other" for memberwise copy operations.
				bool is_copy_or_move_param = ctor_unnamed_param_counter == 0 &&
					(param_type.is_reference() || param_type.is_rvalue_reference()) &&
					node.parameter_nodes().size() == 1;
				
				if (is_copy_or_move_param) {
					func_param.name = StringTable::getOrInternStringHandle("other");
				} else {
					func_param.name = StringTable::getOrInternStringHandle(
						StringBuilder().append("__param_").append(ctor_unnamed_param_counter).commit());
				}
				ctor_unnamed_param_counter++;
			} else {
				func_param.name = StringTable::getOrInternStringHandle(param_name);
			}
			
			func_param.is_reference = param_type.is_reference();
			func_param.is_rvalue_reference = param_type.is_rvalue_reference();
			func_param.cv_qualifier = param_type.cv_qualifier();
			ctor_decl_op.parameters.push_back(func_param);
		}

		// Skip duplicate constructor definitions (e.g. when a static member call queues all struct members)
		if (generated_function_names_.count(ctor_decl_op.mangled_name) > 0) {
			FLASH_LOG(Codegen, Debug, "Skipping duplicate constructor definition: ", StringTable::getStringView(ctor_decl_op.mangled_name));
			return;
		}
		generated_function_names_.insert(ctor_decl_op.mangled_name);

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(ctor_decl_op), node.name_token()));
		
		symbol_table.enter_scope(ScopeType::Function);

		// Add 'this' pointer to symbol table for member access
		// Look up the struct type to get its type index and size
		// Use struct_name_for_ctor (which is fully qualified) instead of node.struct_name()
		// to handle nested classes correctly (node.struct_name() might be just "Inner" instead of "Outer::Inner")
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_for_ctor));
		if (type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Create a type specifier for the struct pointer (this is a pointer, so 64 bits)
				Token this_token = node.name_token();  // Use constructor token for location
				auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
					Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
				// Mark 'this' as a pointer to struct (not a struct value)
				this_type.as<TypeSpecifierNode>().add_pointer_level();
				auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);

				// Add 'this' to symbol table (it's the implicit first parameter)
				symbol_table.insert("this"sv, this_decl);
			}
		}

		// Add parameters to symbol table
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = requireDeclarationNode(param, "ctor symbol table");
			symbol_table.insert(param_decl.identifier_token().value(), param);
		}

		// C++11 Delegating constructor: if present, ONLY call the target constructor
		// No base class or member initialization should happen
		if (node.delegating_initializer().has_value()) {
			const auto& delegating_init = node.delegating_initializer().value();
			
			// Build constructor call: StructName::StructName(this, args...)
			ConstructorCallOp ctor_op;
			ctor_op.struct_name = StringTable::getOrInternStringHandle(struct_name_for_ctor);
			ctor_op.object = StringTable::getOrInternStringHandle("this");

			// Add constructor arguments from delegating initializer
			for (const auto& arg : delegating_init.arguments) {
				auto arg_operands = visitExpressionNode(arg.as<ExpressionNode>());
				// arg_operands = [type, size, value]
				if (arg_operands.size() >= 3) {
					TypedValue tv = toTypedValue(arg_operands);
					ctor_op.arguments.push_back(std::move(tv));
				}
			}

			ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
			
			// Delegating constructors don't execute the body or initialize members
			// Just return
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.name_token()));
			return;
		}

		// C++ construction order:
		// 1. Base class constructors (in declaration order)
		// 2. Member variables (in declaration order)
		// 3. Constructor body

		// Look up the struct type to get base class and member information
		// Use struct_name_for_ctor (fully qualified) instead of node.struct_name()
		auto struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_for_ctor));
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Step 1: Call base class constructors (in declaration order)
				for (const auto& base : struct_info->base_classes) {
					// Check if there's an explicit base initializer
					const BaseInitializer* base_init = nullptr;
					for (const auto& init : node.base_initializers()) {
						StringHandle base_name_handle = StringTable::getOrInternStringHandle(base.name);
						if (init.getBaseClassName() == base_name_handle) {
							base_init = &init;
							break;
						}
						// For template instantiations, the base initializer stores the un-substituted
						// name (e.g., "Base") but struct_info has the instantiated name (e.g., "Base$hash").
						// Also match against the base template name.
						if (base.type_index < gTypeInfo.size()) {
							const TypeInfo& base_ti = gTypeInfo[base.type_index];
							if (base_ti.isTemplateInstantiation() && init.getBaseClassName() == base_ti.baseTemplateName()) {
								base_init = &init;
								break;
							}
						}
					}

					// Get base class type info
					if (base.type_index >= gTypeInfo.size()) {
						continue;  // Invalid base type index
					}
					const TypeInfo& base_type_info = gTypeInfo[base.type_index];

					// Build constructor call: Base::Base(this, args...)
					ConstructorCallOp ctor_op;
					ctor_op.struct_name = base_type_info.name();
					ctor_op.object = StringTable::getOrInternStringHandle("this");
					// For multiple inheritance, the 'this' pointer must be adjusted to point to the base subobject
					assert(base.offset <= static_cast<size_t>(std::numeric_limits<int>::max()) && "Base class offset exceeds int range");
					ctor_op.base_class_offset = static_cast<int>(base.offset);

					// Add constructor arguments from base initializer
					if (base_init) {
						for (const auto& arg : base_init->arguments) {
							auto arg_operands = visitExpressionNode(arg.as<ExpressionNode>());
							// arg_operands = [type, size, value]
							if (arg_operands.size() >= 3) {
								TypedValue tv = toTypedValue(arg_operands);
								ctor_op.arguments.push_back(std::move(tv));
							}
						}
						// If there's an explicit initializer, generate the constructor call
						ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
					}
					// If no explicit initializer and this is NOT an implicit copy/move constructor,
					// call default constructor (no args)
					// For implicit copy/move constructors, the base constructor call is generated
					// in the implicit constructor generation code below
					// Note: implicit DEFAULT constructors (0 params) SHOULD call base default constructors
					else {
						bool is_implicit_default_ctor = node.is_implicit() && node.parameter_nodes().size() == 0;
						if (!node.is_implicit() || is_implicit_default_ctor) {
							// Only call base default constructor if the base class actually has constructors
							// This avoids link errors when inheriting from classes without constructors
							const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
							if (base_struct_info && base_struct_info->hasAnyConstructor()) {
								// Call default constructor with no arguments
								ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
							}
						}
					}
				}
				
				// Step 1.5: Initialize vptr if this class has virtual functions
				// This must happen after base constructor calls (which set up base vptr)
				// but before member initialization
				if (struct_info->has_vtable) {
					// Use the pre-generated vtable symbol from struct_info
					// The vtable symbol is generated once during buildVTable()
					auto vtable_symbol = StringTable::getOrInternStringHandle(struct_info->vtable_symbol);
					
					// Create a MemberStore instruction to store vtable address to offset 0 (vptr)
					MemberStoreOp vptr_store;
					vptr_store.object = StringTable::getOrInternStringHandle("this");
					vptr_store.member_name = StringTable::getOrInternStringHandle("__vptr");  // Virtual pointer (synthetic member)
					vptr_store.offset = 0;  // vptr is always at offset 0
					vptr_store.struct_type_info = struct_type_info;  // Use TypeInfo pointer
					vptr_store.is_reference = false;
					vptr_store.is_rvalue_reference = false;
					vptr_store.vtable_symbol = vtable_symbol;  // Store vtable symbol as string_view
					
					// The value is a vtable symbol reference
					// Type is pointer (Type::Void with pointer semantics), size is 64 bits (8 bytes)
					// The actual symbol will be loaded using the vtable_symbol field
					vptr_store.value.type = Type::Void;
					vptr_store.value.size_in_bits = 64;
					vptr_store.value.value = static_cast<unsigned long long>(0);  // Placeholder
					
					ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(vptr_store), node.name_token()));
				}
			}
		}

		// Step 2: Generate IR for member initializers (executed before constructor body)
		// Look up the struct type to get member information
		// Use struct_name_for_ctor (fully qualified) instead of node.struct_name()
		struct_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(struct_name_for_ctor));
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// If this is an implicit constructor, generate appropriate initialization
				if (node.is_implicit()) {
					// Check if this is a copy or move constructor (has one parameter that is a reference)
					bool is_copy_constructor = false;
					bool is_move_constructor = false;
					if (node.parameter_nodes().size() == 1) {
						const auto& param_decl = node.parameter_nodes()[0].as<DeclarationNode>();
						const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
						if (param_type.is_reference() && param_type.type() == Type::Struct) {
							if (param_type.is_rvalue_reference()) {
								is_move_constructor = true;
							} else {
								is_copy_constructor = true;
							}
						}
					}

					if (is_copy_constructor || is_move_constructor) {
						// Implicit copy/move constructor: call base class copy/move constructors first, then memberwise copy/move from 'other' to 'this'

						// Step 1: Call base class copy/move constructors (in declaration order)
						for (const auto& base : struct_info->base_classes) {
							// Get base class type info
							if (base.type_index >= gTypeInfo.size()) {
								continue;  // Invalid base type index
							}
							const TypeInfo& base_type_info = gTypeInfo[base.type_index];

							// Only call base copy/move constructor if the base class actually has constructors
							// This avoids link errors when inheriting from classes without constructors
							const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
							if (!base_struct_info || !base_struct_info->hasAnyConstructor()) {
								continue;  // Skip if base has no constructors
							}

							// Build constructor call: Base::Base(this, other)
							// For copy constructors, pass 'other' as the copy source (cast to base class reference)
							// For move constructors, pass 'other' as the move source
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = base_type_info.name();
							ctor_op.object = StringTable::getOrInternStringHandle("this");
							// For multiple inheritance, the 'this' pointer must be adjusted to point to the base subobject
							assert(base.offset <= static_cast<size_t>(std::numeric_limits<int>::max()) && "Base class offset exceeds int range");
							ctor_op.base_class_offset = static_cast<int>(base.offset);
							// Add 'other' parameter for copy/move constructor
							// IMPORTANT: Use BASE CLASS type_index, not derived class, for proper name mangling
							TypedValue other_arg;
							other_arg.type = Type::Struct;  // Parameter type (struct reference)
							other_arg.size_in_bits = static_cast<int>(base_type_info.struct_info_ ? base_type_info.struct_info_->total_size * 8 : struct_info->total_size * 8);
							other_arg.value = StringTable::getOrInternStringHandle("other");  // Parameter value ('other' object)
							other_arg.type_index = base.type_index;  // Use BASE class type index for proper mangling
							if (is_copy_constructor) {
								other_arg.ref_qualifier = ReferenceQualifier::LValueReference;  // Copy ctor takes lvalue reference
								other_arg.cv_qualifier = CVQualifier::Const;  // Copy ctor takes const reference
							} else if (is_move_constructor) {
								other_arg.ref_qualifier = ReferenceQualifier::RValueReference;  // Move ctor takes rvalue reference
							}
							ctor_op.arguments.push_back(std::move(other_arg));

							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
						}

						// Step 2: Memberwise copy/move from 'other' to 'this'
						for (const auto& member : struct_info->members) {
							// First, load the member from 'other'
							TempVar member_value = var_counter.next();
							MemberLoadOp member_load;
							member_load.result.value = member_value;
							member_load.result.type = member.type;
							member_load.result.size_in_bits = static_cast<int>(member.size * 8);
							member_load.object = StringTable::getOrInternStringHandle("other"sv);  // Load from 'other' parameter
							member_load.member_name = member.getName();
							member_load.offset = static_cast<int>(member.offset);
							member_load.is_reference = member.is_reference;
							member_load.is_rvalue_reference = member.is_rvalue_reference;
							member_load.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(member_load), node.name_token()));

							// Then, store the member to 'this'
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = StringTable::getOrInternStringHandle("this"sv);
							member_store.member_name = member.getName();
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference;
							member_store.is_rvalue_reference = member.is_rvalue_reference;
							member_store.struct_type_info = nullptr;

							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), node.name_token()));
						}
					} else {
						// Implicit default constructor: use default member initializers or zero-initialize

						// Step 1: Handle bitfield members - combine into single per-unit stores
						{
							std::unordered_map<size_t, unsigned long long> combined_bitfield_values;
							std::unordered_set<size_t> bitfield_offsets;
							for (const auto& member : struct_info->members) {
								if (member.bitfield_width.has_value()) {
									bitfield_offsets.insert(member.offset);
									unsigned long long val = 0;
									if (member.default_initializer.has_value()) {
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
									}
									size_t width = *member.bitfield_width;
									unsigned long long mask = (width < 64) ? ((1ULL << width) - 1) : ~0ULL;
									combined_bitfield_values[member.offset] |= ((val & mask) << member.bitfield_bit_offset);
								}
							}
							for (auto offset : bitfield_offsets) {
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
										ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(combined_store), node.name_token()));
										break;
									}
								}
							}
						}

						// Step 2: Handle non-bitfield members
						for (const auto& member : struct_info->members) {
							if (member.bitfield_width.has_value()) continue; // handled above
							// Generate MemberStore IR to initialize the member
							// Format: [member_type, member_size, object_name, member_name, offset, value]
							
							// Determine the initial value
							IrValue member_value;
							// Check if member has a default initializer (C++11 feature)
							if (member.default_initializer.has_value()) {
								const ASTNode& init_node = member.default_initializer.value();
								if (init_node.has_value() && init_node.is<ExpressionNode>()) {
									// Use the default member initializer
									auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
									// Extract just the value (third element of init_operands)
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
								} else if (init_node.has_value() && init_node.is<InitializerListNode>()) {
									// Handle brace initializers like `B b1 = { .a = 1 };`
									const InitializerListNode& init_list = init_node.as<InitializerListNode>();
									const auto& initializers = init_list.initializers();
									
									// For struct members with brace initializers, we need to handle them specially
									// Get the type info for this member
									TypeIndex member_type_index = member.type_index;
									if (member_type_index < gTypeInfo.size()) {
										const TypeInfo& member_type_info = gTypeInfo[member_type_index];
										
										// If this is a struct type, we need to initialize its members
										if (member_type_info.struct_info_ && !member_type_info.struct_info_->members.empty()) {
											// Build a map of member names to initializer expressions
											std::unordered_map<StringHandle, const ASTNode*> member_values;
											size_t positional_index = 0;
											
											for (size_t i = 0; i < initializers.size(); ++i) {
												if (init_list.is_designated(i)) {
													// Designated initializer - use member name
													StringHandle member_name = init_list.member_name(i);
													member_values[member_name] = &initializers[i];
												} else {
													// Positional initializer - map to member by index
													if (positional_index < member_type_info.struct_info_->members.size()) {
														StringHandle member_name = member_type_info.struct_info_->members[positional_index].getName();
														member_values[member_name] = &initializers[i];
														positional_index++;
													}
												}
											}
											
											// Generate nested member stores for each member of the nested struct
											for (const StructMember& nested_member : member_type_info.struct_info_->members) {
												// Determine initial value for nested member
												std::optional<IrValue> nested_member_value;
												StringHandle nested_member_name_handle = nested_member.getName();
												
												if (member_values.count(nested_member_name_handle)) {
													const ASTNode& init_expr = *member_values[nested_member_name_handle];
														
													// Check if this is a nested braced initializer (two-level nesting)
													if (init_expr.is<InitializerListNode>()) {
														// Handle nested braced initializers using the recursive helper
														const InitializerListNode& nested_init_list = init_expr.as<InitializerListNode>();
															
														// Get the type info for the nested member
														TypeIndex nested_member_type_index = nested_member.type_index;
														if (nested_member_type_index < gTypeInfo.size()) {
															const TypeInfo& nested_member_type_info = gTypeInfo[nested_member_type_index];
																
															// If this is a struct type, use the recursive helper
															if (nested_member_type_info.struct_info_ && !nested_member_type_info.struct_info_->members.empty()) {
																generateNestedMemberStores(
																	*nested_member_type_info.struct_info_,
																	nested_init_list,
																	StringTable::getOrInternStringHandle("this"),
																	static_cast<int>(member.offset + nested_member.offset),
																	node.name_token()
																);
																continue;  // Skip the nested member store
															} else {
																// For non-struct types with single-element initializer lists
																const auto& nested_initializers = nested_init_list.initializers();
																if (nested_initializers.size() == 1 && nested_initializers[0].is<ExpressionNode>()) {
																	auto nested_init_operands = visitExpressionNode(nested_initializers[0].as<ExpressionNode>());
																	if (std::holds_alternative<TempVar>(nested_init_operands[2])) {
																		nested_member_value = std::get<TempVar>(nested_init_operands[2]);
																	} else if (std::holds_alternative<unsigned long long>(nested_init_operands[2])) {
																		nested_member_value = std::get<unsigned long long>(nested_init_operands[2]);
																	} else if (std::holds_alternative<double>(nested_init_operands[2])) {
																		nested_member_value = std::get<double>(nested_init_operands[2]);
																	} else if (std::holds_alternative<StringHandle>(nested_init_operands[2])) {
																		nested_member_value = std::get<StringHandle>(nested_init_operands[2]);
																	}
																}
															}
														}
													} else if (init_expr.is<ExpressionNode>()) {
														auto init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
														if (std::holds_alternative<TempVar>(init_operands[2])) {
															nested_member_value = std::get<TempVar>(init_operands[2]);
														} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
															nested_member_value = std::get<unsigned long long>(init_operands[2]);
														} else if (std::holds_alternative<double>(init_operands[2])) {
															nested_member_value = std::get<double>(init_operands[2]);
														} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
															nested_member_value = std::get<StringHandle>(init_operands[2]);
														}
													}
												}
												
												if (nested_member_value.has_value()) {
													// Generate nested member store
													MemberStoreOp nested_member_store;
													nested_member_store.value.type = nested_member.type;
													nested_member_store.value.size_in_bits = static_cast<int>(nested_member.size * 8);
													nested_member_store.value.value = nested_member_value.value();
													nested_member_store.object = StringTable::getOrInternStringHandle("this");
													nested_member_store.member_name = nested_member.getName();
													// Calculate offset: parent member offset + nested member offset
													nested_member_store.offset = static_cast<int>(member.offset + nested_member.offset);
													nested_member_store.is_reference = nested_member.is_reference;
													nested_member_store.is_rvalue_reference = nested_member.is_rvalue_reference;
													nested_member_store.struct_type_info = nullptr;
												
													ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(nested_member_store), node.name_token()));
												}
											}
											
											// Skip the outer member store since we've already generated nested stores
											continue;
										} else {
											// For non-struct types with single-element initializer lists
											if (initializers.size() == 1 && initializers[0].is<ExpressionNode>()) {
												auto init_operands = visitExpressionNode(initializers[0].as<ExpressionNode>());
												if (std::holds_alternative<TempVar>(init_operands[2])) {
													member_value = std::get<TempVar>(init_operands[2]);
												} else if (std::holds_alternative<unsigned long long>(init_operands[2])) {
													member_value = std::get<unsigned long long>(init_operands[2]);
												} else if (std::holds_alternative<double>(init_operands[2])) {
													member_value = std::get<double>(init_operands[2]);
												} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
													member_value = std::get<StringHandle>(init_operands[2]);
												} else {
													member_value = 0ULL;
												}
											} else {
												member_value = 0ULL;
											}
										}
									} else {
										member_value = 0ULL;
									}
								} else {
									// Default initializer exists but isn't an expression, zero-initialize
									if (member.type == Type::Int || member.type == Type::Long ||
									    member.type == Type::Short || member.type == Type::Char) {
										member_value = 0ULL;  // Zero for integer types
									} else if (member.type == Type::Float || member.type == Type::Double) {
										member_value = 0.0;  // Zero for floating-point types
									} else if (member.type == Type::Bool) {
										member_value = 0ULL;  // False for bool (0)
									} else {
										member_value = 0ULL;  // Default to zero
									}
								}
							} else {
								// Check if this is a struct type with a constructor
								bool is_struct_with_constructor = false;
								if (member.type == Type::Struct && member.type_index < gTypeInfo.size()) {
									const TypeInfo& member_type_info = gTypeInfo[member.type_index];
									if (member_type_info.struct_info_ && member_type_info.struct_info_->hasAnyConstructor()) {
										is_struct_with_constructor = true;
									}
								}
								
								if (is_struct_with_constructor) {
									// Call the nested struct's default constructor instead of zero-initializing
									const TypeInfo& member_type_info = gTypeInfo[member.type_index];
									ConstructorCallOp ctor_op;
									ctor_op.struct_name = member_type_info.name();
									ctor_op.object = StringTable::getOrInternStringHandle("this");
									// No arguments for default constructor
									// Use base_class_offset to specify the member's offset within the parent struct
									assert(member.offset <= static_cast<size_t>(std::numeric_limits<int>::max()) && "Member offset exceeds int range");
									ctor_op.base_class_offset = static_cast<int>(member.offset);
									ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
									continue;  // Skip the MemberStore since constructor handles initialization
								} else {
									// Zero-initialize based on type
									if (member.type == Type::Int || member.type == Type::Long ||
									    member.type == Type::Short || member.type == Type::Char) {
										member_value = 0ULL;  // Zero for integer types
									} else if (member.type == Type::Float || member.type == Type::Double) {
										member_value = 0.0;  // Zero for floating-point types
									} else if (member.type == Type::Bool) {
										member_value = 0ULL;  // False for bool (0)
									} else {
										member_value = 0ULL;  // Default to zero
									}
								}
							}
	
							MemberStoreOp member_store;
							member_store.value.type = member.type;
							member_store.value.size_in_bits = static_cast<int>(member.size * 8);
							member_store.value.value = member_value;
							member_store.object = StringTable::getOrInternStringHandle("this");
							member_store.member_name = member.getName();
							member_store.offset = static_cast<int>(member.offset);
							member_store.is_reference = member.is_reference;
							member_store.is_rvalue_reference = member.is_rvalue_reference;
							member_store.struct_type_info = nullptr;
	
							ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), node.name_token()));
						}
					}
				} else {
					// User-defined constructor: initialize all members
					// Precedence: explicit initializer > default initializer > zero-initialize

					// Build a map of explicit member initializers for quick lookup
					std::unordered_map<std::string, const MemberInitializer*> explicit_inits;
					for (const auto& initializer : node.member_initializers()) {
						explicit_inits[std::string(initializer.member_name)] = &initializer;
					}

					// Initialize all members
					for (const auto& member : struct_info->members) {
						// Generate MemberStore IR to initialize the member
						
						// Determine the initial value
						IrValue member_value;
						// Check for explicit initializer first (highest precedence)
						auto explicit_it = explicit_inits.find(std::string(StringTable::getStringView(member.getName())));
						if (explicit_it != explicit_inits.end()) {
							// Special handling for reference members initialized with reference variables/parameters
							// When initializing a reference member (int& ref) with a reference parameter (int& r),
							// we need to use the pointer value that the parameter holds, not dereference it
							bool handled_as_reference_init = false;
							if (member.is_reference || member.is_rvalue_reference) {
								// Check if the initializer is a simple identifier
								const ASTNode& init_expr = explicit_it->second->initializer_expr;
								if (init_expr.is<ExpressionNode>()) {
									const auto& expr_node = init_expr.as<ExpressionNode>();
									if (std::holds_alternative<IdentifierNode>(expr_node)) {
										const auto& id_node = std::get<IdentifierNode>(expr_node);
										auto init_name = StringTable::getOrInternStringHandle(id_node.name());
										
										// Look up the identifier in the symbol table
										std::optional<ASTNode> init_symbol = symbol_table.lookup(init_name);
										if (init_symbol.has_value() && init_symbol->is<DeclarationNode>()) {
											const auto& init_decl = init_symbol->as<DeclarationNode>();
											const auto& init_type = init_decl.type_node().as<TypeSpecifierNode>();
											
											// If the initializer is a reference, use its value directly (it's already a pointer)
											// Don't dereference it - just use the string_view to refer to the variable
											if (init_type.is_reference() || init_type.is_rvalue_reference()) {
												member_value = init_name;
												handled_as_reference_init = true;
											}
										}
									}
								}
							}
							
							if (!handled_as_reference_init) {
								// Use explicit initializer from constructor initializer list
								auto init_operands = visitExpressionNode(explicit_it->second->initializer_expr.as<ExpressionNode>());
								// Extract just the value (third element of init_operands)
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
							}
						} else if (member.default_initializer.has_value()) {
							const ASTNode& init_node = member.default_initializer.value();
							if (init_node.has_value() && init_node.is<ExpressionNode>()) {
								// Use default member initializer (C++11 feature)
								auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
								// Extract just the value (third element of init_operands)
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
							} else {
								// Default initializer exists but isn't an expression, zero-initialize
								if (member.type == Type::Int || member.type == Type::Long ||
								    member.type == Type::Short || member.type == Type::Char) {
									member_value = 0ULL;
								} else if (member.type == Type::Float || member.type == Type::Double) {
									member_value = 0.0;
								} else if (member.type == Type::Bool) {
									member_value = 0ULL;  // False for bool (0)
								} else {
									member_value = 0ULL;
								}
							}
						} else {
							// Check if this is a struct type with a constructor
							bool is_struct_with_constructor = false;
							if (member.type == Type::Struct && member.type_index < gTypeInfo.size()) {
								const TypeInfo& member_type_info = gTypeInfo[member.type_index];
								if (member_type_info.struct_info_ && member_type_info.struct_info_->hasAnyConstructor()) {
									is_struct_with_constructor = true;
								}
							}
							
							if (is_struct_with_constructor) {
								// Call the nested struct's default constructor instead of zero-initializing
								const TypeInfo& member_type_info = gTypeInfo[member.type_index];
								ConstructorCallOp ctor_op;
								ctor_op.struct_name = member_type_info.name();
								ctor_op.object = StringTable::getOrInternStringHandle("this");
								// No arguments for default constructor
								// Use base_class_offset to specify the member's offset within the parent struct
								assert(member.offset <= static_cast<size_t>(std::numeric_limits<int>::max()) && "Member offset exceeds int range");
								ctor_op.base_class_offset = static_cast<int>(member.offset);
								ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), node.name_token()));
								continue;  // Skip the MemberStore since constructor handles initialization
							} else {
								// Zero-initialize based on type
								if (member.type == Type::Int || member.type == Type::Long ||
								    member.type == Type::Short || member.type == Type::Char) {
									member_value = 0ULL;  // Zero for integer types
								} else if (member.type == Type::Float || member.type == Type::Double) {
									member_value = 0.0;  // Zero for floating-point types
								} else if (member.type == Type::Bool) {
									member_value = 0ULL;  // False for bool (0)
								} else {
									member_value = 0ULL;  // Default to zero
								}
							}
						}
	
						MemberStoreOp member_store;
						member_store.value.type = member.type;
						member_store.value.size_in_bits = static_cast<int>(member.size * 8);
						member_store.value.value = member_value;
						member_store.object = StringTable::getOrInternStringHandle("this");
						member_store.member_name = member.getName();
						member_store.offset = static_cast<int>(member.offset);
						member_store.is_reference = member.is_reference;
						member_store.is_rvalue_reference = member.is_rvalue_reference;
						member_store.struct_type_info = nullptr;
						member_store.bitfield_width = member.bitfield_width;
						member_store.bitfield_bit_offset = member.bitfield_bit_offset;

						ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), node.name_token()));
					}
				}
			}
		}

		// Visit the constructor body
		const BlockNode& block = node.get_definition().value().as<BlockNode>();
		block.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		// Add implicit return for constructor (constructors don't have explicit return statements)
		ReturnOp ret_op;  // No return value for void
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.name_token()));

		symbol_table.exit_scope();
		// Don't clear current_function_name_ here - let the top-level visitor manage it
	}

	void visitDestructorDeclarationNode(const DestructorDeclarationNode& node) {
		if (!node.get_definition().has_value())
			return;

		// Reset the temporary variable counter for each new destructor
		// Destructors are always member functions, so reserve TempVar(1) for 'this'
		var_counter = TempVar(2);

		// Clear global TempVar metadata to prevent stale data from bleeding into this function
		GlobalTempVarMetadataStorage::instance().clear();

		// Set current function name for static local variable mangling
		current_function_name_ = node.name();
	static_local_names_.clear();

	// Create destructor declaration with typed payload
	FunctionDeclOp dtor_decl_op;
	dtor_decl_op.function_name = StringTable::getOrInternStringHandle(StringBuilder().append("~"sv).append(node.struct_name()));  // Destructor name
	dtor_decl_op.struct_name = node.struct_name();
	dtor_decl_op.return_type = Type::Void;  // Destructors don't have a return type
	dtor_decl_op.return_size_in_bits = 0;  // Size is 0 for void
	dtor_decl_op.return_pointer_depth = 0;  // Pointer depth is 0 for void
	dtor_decl_op.linkage = Linkage::CPlusPlus;  // C++ linkage for destructors
	dtor_decl_op.is_variadic = false;  // Destructors are never variadic

	// Generate mangled name for destructor
	// Use the dedicated mangling function for destructors to ensure correct platform-specific mangling
	// (e.g., MSVC uses ??1ClassName@... format)
	dtor_decl_op.mangled_name = NameMangling::generateMangledNameFromNode(node);

	// Note: 'this' pointer is added implicitly by handleFunctionDecl for all member functions
	// We don't add it here to avoid duplication

	ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(dtor_decl_op), node.name_token()));		symbol_table.enter_scope(ScopeType::Function);

		// Add 'this' pointer to symbol table for member access
		// Look up the struct type to get its type index and size
		auto type_it = gTypesByName.find(node.struct_name());
		if (type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info) {
				// Create a type specifier for the struct pointer (this is a pointer, so 64 bits)
				Token this_token = node.name_token();  // Use destructor token for location
				auto this_type = ASTNode::emplace_node<TypeSpecifierNode>(
					Type::Struct, struct_type_info->type_index_, 64, this_token, CVQualifier::None);
				// Mark 'this' as a pointer to struct (not a struct value)
				this_type.as<TypeSpecifierNode>().add_pointer_level();
				auto this_decl = ASTNode::emplace_node<DeclarationNode>(this_type, this_token);

				// Add 'this' to symbol table (it's the implicit first parameter)
				symbol_table.insert("this"sv, this_decl);
			}
		}

		// C++ destruction order:
		// 1. Destructor body
		// 2. Member variables destroyed (automatic for non-class types)
		// 3. Base class destructors (in REVERSE declaration order)

		// Step 1: Visit the destructor body
		const BlockNode& block = node.get_definition().value().as<BlockNode>();
		block.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		// Step 2: Member destruction is automatic for primitive types (no action needed)

		// Step 3: Call base class destructors in REVERSE order
		auto struct_type_it = gTypesByName.find(node.struct_name());
		if (struct_type_it != gTypesByName.end()) {
			const TypeInfo* struct_type_info = struct_type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			if (struct_info && !struct_info->base_classes.empty()) {
				// Iterate through base classes in reverse order
				for (auto it = struct_info->base_classes.rbegin(); it != struct_info->base_classes.rend(); ++it) {
					const auto& base = *it;

					// Get base class type info
					if (base.type_index >= gTypeInfo.size()) {
						continue;  // Invalid base type index
					}
					const TypeInfo& base_type_info = gTypeInfo[base.type_index];

					// Build destructor call: Base::~Base(this)
					DestructorCallOp dtor_op;
					dtor_op.struct_name = base_type_info.name();
					dtor_op.object = StringTable::getOrInternStringHandle("this");

					ir_.addInstruction(IrInstruction(IrOpcode::DestructorCall, std::move(dtor_op), node.name_token()));
				}
			}
		}

		// Add implicit return for destructor (destructors don't have explicit return statements)
		ReturnOp ret_op;  // No return value for void
		ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.name_token()));

		symbol_table.exit_scope();
		// Don't clear current_function_name_ here - let the top-level visitor manage it
	}

	void visitNamespaceDeclarationNode(const NamespaceDeclarationNode& node) {
		// Namespace declarations themselves don't generate IR - they just provide scope
		// Track the current namespace for proper name mangling
		// For anonymous namespaces, push empty string which will be handled specially by mangling
		current_namespace_stack_.push_back(std::string(node.name()));
		
		// Visit all declarations within the namespace
		for (const auto& decl : node.declarations()) {
			visit(decl);
		}
		
		// Pop the namespace from the stack
		current_namespace_stack_.pop_back();
	}

	void visitUsingDirectiveNode(const UsingDirectiveNode& node) {
		// Using directives don't generate IR - they affect name lookup in the symbol table
		// Add the namespace to the current scope's using directives in the local symbol table
		// (not gSymbolTable, which is the parser's symbol table and has different scope management)
		symbol_table.add_using_directive(node.namespace_handle());
	}

	void visitUsingDeclarationNode(const UsingDeclarationNode& node) {
		// Using declarations don't generate IR - they import a specific name into the current scope
		// Add the using declaration to the local symbol table (not gSymbolTable)
		FLASH_LOG(Codegen, Debug, "Adding using declaration: ", node.identifier_name(), " from namespace handle=", node.namespace_handle().index);
		symbol_table.add_using_declaration(
			node.identifier_name(),
			node.namespace_handle(),
			node.identifier_name()
		);
	}

	void visitUsingEnumNode(const UsingEnumNode& node) {
		// C++20 using enum - brings all enumerators of a scoped enum into the current scope
		// Look up the enum type and add all enumerators to the local symbol table
		StringHandle enum_name = node.enum_type_name();
		
		auto type_it = gTypesByName.find(enum_name);
		if (type_it != gTypesByName.end() && type_it->second->getEnumInfo()) {
			const EnumTypeInfo* enum_info = type_it->second->getEnumInfo();
			TypeIndex enum_type_index = type_it->second->type_index_;
			
			// Add each enumerator to the local symbol table
			for (const auto& enumerator : enum_info->enumerators) {
				// Create a type node for the enum type
				Token enum_type_token(Token::Type::Identifier, 
					StringTable::getStringView(enum_name), 0, 0, 0);
				auto enum_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
					Type::Enum, enum_type_index, enum_info->underlying_size, enum_type_token);
				
				// Create a declaration node for the enumerator
				Token enumerator_token(Token::Type::Identifier, 
					StringTable::getStringView(enumerator.getName()), 0, 0, 0);
				auto enumerator_decl = ASTNode::emplace_node<DeclarationNode>(enum_type_node, enumerator_token);
				
				// Insert into local symbol table
				symbol_table.insert(StringTable::getStringView(enumerator.getName()), enumerator_decl);
			}
			
			FLASH_LOG(Codegen, Debug, "Using enum '", StringTable::getStringView(enum_name), 
				"' - added ", enum_info->enumerators.size(), " enumerators to local scope");
		} else {
			FLASH_LOG(General, Error, "Enum type '", StringTable::getStringView(enum_name), 
				"' not found for 'using enum' declaration");
		}
	}

	void visitNamespaceAliasNode(const NamespaceAliasNode& node) {
		// Namespace aliases don't generate IR - they create an alias for a namespace
		// Add the alias to the local symbol table (not gSymbolTable)
		symbol_table.add_namespace_alias(node.alias_name(), node.target_namespace());
	}

	void visitReturnStatementNode(const ReturnStatementNode& node) {
		if (node.expression()) {
			const auto& expr_opt = node.expression();
			
			// Handle InitializerListNode for braced initializers in return statements
			if (expr_opt->is<InitializerListNode>()) {
				// Create a temporary variable to hold the initialized struct
				TempVar temp_var = var_counter.next();
				
				// Generate initialization code similar to variable declarations
				const InitializerListNode& init_list = expr_opt->as<InitializerListNode>();
				
				// Get struct type information
				Type return_type = current_function_return_type_;
				int return_size = current_function_return_size_;
				
				if (return_type != Type::Struct) {
					FLASH_LOG(Codegen, Error, "InitializerListNode in return statement for non-struct type");
					return;
				}
				
				// Find the struct info
				const StructTypeInfo* struct_info = nullptr;
				
				// Look up the struct by return type index or name
				for (size_t i = 0; i < gTypeInfo.size(); ++i) {
					if (gTypeInfo[i].struct_info_ &&
					    static_cast<int>(gTypeInfo[i].struct_info_->total_size * 8) == return_size) {
						struct_info = gTypeInfo[i].struct_info_.get();
						break;
					}
				}
				
				if (!struct_info) {
					FLASH_LOG(Codegen, Error, "Could not find struct type info for return type");
					return;
				}
				
				// Process initializer list to generate member stores
				const auto& initializers = init_list.initializers();
				std::unordered_map<StringHandle, const ASTNode*> member_values;
				size_t positional_index = 0;
				
				for (size_t i = 0; i < initializers.size(); ++i) {
					if (init_list.is_designated(i)) {
						// Designated initializer - use member name
						StringHandle member_name = init_list.member_name(i);
						member_values[member_name] = &initializers[i];
					} else {
						// Positional initializer - map to member by index
						if (positional_index < struct_info->members.size()) {
							StringHandle member_name = struct_info->members[positional_index].getName();
							member_values[member_name] = &initializers[i];
							positional_index++;
						}
					}
				}
				
				// Generate member stores for each initialized member
				for (const StructMember& member : struct_info->members) {
					StringHandle member_name_handle = member.getName();
					auto it = member_values.find(member_name_handle);
					
					if (it != member_values.end()) {
						// Evaluate the initializer expression
						const ASTNode* init_expr = it->second;
						if (init_expr->is<ExpressionNode>()) {
							auto init_operands = visitExpressionNode(init_expr->as<ExpressionNode>());
							
							if (init_operands.size() >= 3) {
								// Generate member store
								MemberStoreOp store_op;
								store_op.object = temp_var;
								store_op.member_name = member.getName();
								store_op.offset = static_cast<int>(member.offset);
								
								// Create TypedValue from operands
								Type value_type = std::get<Type>(init_operands[0]);
								int value_size = std::get<int>(init_operands[1]);
								IrValue ir_value;
								
								// Extract value from operands
								if (std::holds_alternative<unsigned long long>(init_operands[2])) {
									ir_value = std::get<unsigned long long>(init_operands[2]);
								} else if (std::holds_alternative<TempVar>(init_operands[2])) {
									ir_value = std::get<TempVar>(init_operands[2]);
								} else if (std::holds_alternative<StringHandle>(init_operands[2])) {
									ir_value = std::get<StringHandle>(init_operands[2]);
								} else if (std::holds_alternative<double>(init_operands[2])) {
									ir_value = std::get<double>(init_operands[2]);
								}
								
								store_op.value = { value_type, value_size, ir_value };
								store_op.is_reference = false;
								
								ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(store_op), node.return_token()));
							}
						}
					}
				}
				
				// Call any enclosing __finally funclets before returning
				emitSehFinallyCallsBeforeReturn(node.return_token());

				// Now return the temporary variable
				ReturnOp ret_op;
				ret_op.return_value = temp_var;
				ret_op.return_type = return_type;
				ret_op.return_size = return_size;
				ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
				return;
			}
			
			// Original handling for ExpressionNode
			assert(expr_opt->is<ExpressionNode>());
			
			// Set flag if we should use RVO (returning struct by value with hidden return param)
			if (current_function_has_hidden_return_param_) {
				in_return_statement_with_rvo_ = true;
			}
			
			// Fast path: reference return of '*this' can directly return the this pointer
			if (current_function_returns_reference_ && expr_opt->is<ExpressionNode>()) {
				const auto& ret_expr = expr_opt->as<ExpressionNode>();
				if (std::holds_alternative<UnaryOperatorNode>(ret_expr)) {
					const auto& unary = std::get<UnaryOperatorNode>(ret_expr);
					if (unary.op() == "*" && unary.get_operand().is<ExpressionNode>()) {
						const auto& operand_expr = unary.get_operand().as<ExpressionNode>();
						if (std::holds_alternative<IdentifierNode>(operand_expr)) {
							const auto& ident = std::get<IdentifierNode>(operand_expr);
							if (ident.name() == "this") {
								emitSehFinallyCallsBeforeReturn(node.return_token());
								ReturnOp ret_op;
								ret_op.return_value = StringTable::getOrInternStringHandle("this");
								ret_op.return_type = current_function_return_type_;
								ret_op.return_size = current_function_return_size_;
								ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
								return;
							}
						}
					}
				}
			}

			// For reference return types, use LValueAddress context to get the address instead of the value
			// This ensures "return *this" returns the address (this pointer), not the dereferenced value
			ExpressionContext return_context = current_function_returns_reference_ 
				? ExpressionContext::LValueAddress 
				: ExpressionContext::Load;
			auto operands = visitExpressionNode(expr_opt->as<ExpressionNode>(), return_context);
			
			// Clear the RVO flag after evaluation
			in_return_statement_with_rvo_ = false;
			
			// Check if this is a void return with a void expression (e.g., return void_func();)
			if (!operands.empty() && operands.size() >= 1) {
				Type expr_type = std::get<Type>(operands[0]);
				
				// If returning a void expression in a void function, just emit void return
				// (the expression was already evaluated for its side effects)
				if (expr_type == Type::Void && current_function_return_type_ == Type::Void) {
					emitSehFinallyCallsBeforeReturn(node.return_token());
					ReturnOp ret_op;  // No return value for void
					ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
					return;
				}
			}
			
			// If the current function has auto return type, deduce it from the return expression
			if (current_function_return_type_ == Type::Auto && !operands.empty() && operands.size() >= 2) {
				Type expr_type = std::get<Type>(operands[0]);
				int expr_size = std::get<int>(operands[1]);
				
				// Build a TypeSpecifierNode for the deduced type
				TypeSpecifierNode deduced_type(expr_type, TypeQualifier::None, expr_size, node.return_token());
				
				// If we have type_index information (for structs), include it
				if (operands.size() >= 4) {
					if (std::holds_alternative<unsigned long long>(operands[3])) {
						TypeIndex type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operands[3]));
						deduced_type = TypeSpecifierNode(expr_type, TypeQualifier::None, expr_size, node.return_token());
						deduced_type.set_type_index(type_index);
					}
				}
				
				// Store the deduced type for this function
				if (current_function_name_.isValid()) {
					deduced_auto_return_types_[std::string(StringTable::getStringView(current_function_name_))] = deduced_type;
				}
				
				// Update current function return type for subsequent return statements
				current_function_return_type_ = expr_type;
				current_function_return_size_ = expr_size;
			}
			
			// Convert to the function's return type if necessary
			// Skip type conversion for reference returns - the expression already has the correct representation
			if (!current_function_returns_reference_ && !operands.empty() && operands.size() >= 2) {
				Type expr_type = std::get<Type>(operands[0]);
				int expr_size = std::get<int>(operands[1]);
		
				// Get the current function's return type
				Type return_type = current_function_return_type_;
				int return_size = current_function_return_size_;
		
				// Convert if types don't match
				if (expr_type != return_type || expr_size != return_size) {
					// Check for user-defined conversion operator
					// If expr is a struct type with a conversion operator to return_type, call it
					if (expr_type == Type::Struct && operands.size() >= 4) {
						TypeIndex expr_type_index = 0;
						if (std::holds_alternative<unsigned long long>(operands[3])) {
							expr_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operands[3]));
						}
						
						if (expr_type_index > 0 && expr_type_index < gTypeInfo.size()) {
							const TypeInfo& source_type_info = gTypeInfo[expr_type_index];
							const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
							
							// Look for a conversion operator to the return type
							const StructMemberFunction* conv_op = findConversionOperator(
								source_struct_info, return_type, 0);
							
							if (conv_op) {
								FLASH_LOG(Codegen, Debug, "Found conversion operator in return statement from ", 
									StringTable::getStringView(source_type_info.name()), 
									" to return type");
								
								// Generate call to the conversion operator
								TempVar result_var = var_counter.next();
								
								// Get the source variable value
								IrValue source_value = std::visit([](auto&& arg) -> IrValue {
									using T = std::decay_t<decltype(arg)>;
									if constexpr (std::is_same_v<T, TempVar> || std::is_same_v<T, StringHandle> ||
									              std::is_same_v<T, unsigned long long> || std::is_same_v<T, double>) {
										return arg;
									} else {
										return 0ULL;
									}
								}, operands[2]);
								
								// Build the mangled name for the conversion operator
								StringHandle struct_name_handle = source_type_info.name();
								std::string_view struct_name = StringTable::getStringView(struct_name_handle);
								
								// Generate the call using CallOp (member function call)
								if (conv_op->function_decl.is<FunctionDeclarationNode>()) {
									const auto& func_decl = conv_op->function_decl.as<FunctionDeclarationNode>();
									std::string_view mangled_name;
									if (func_decl.has_mangled_name()) {
										mangled_name = func_decl.mangled_name();
									} else {
										// Generate mangled name for the conversion operator
										// Use the function's parent struct name, not the source type name,
										// because the conversion operator may be inherited from a base class
										// and we need to call the version defined in the base class.
										std::string_view operator_struct_name = func_decl.parent_struct_name();
										if (operator_struct_name.empty()) {
											operator_struct_name = struct_name;
										}
										mangled_name = generateMangledNameForCall(func_decl, operator_struct_name);
									}
									
									CallOp call_op;
									call_op.result = result_var;
									call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
									call_op.return_type = return_type;
									call_op.return_size_in_bits = return_size;
									call_op.return_type_index = (return_type == Type::Struct) ? current_function_return_type_index_ : 0;
									call_op.is_member_function = true;
									call_op.is_variadic = false;
									
									// For member function calls, first argument is 'this' pointer
									if (std::holds_alternative<StringHandle>(source_value)) {
										// It's a variable - take its address
										TempVar this_ptr = var_counter.next();
										AddressOfOp addr_op;
										addr_op.result = this_ptr;
										addr_op.operand.type = expr_type;
										addr_op.operand.size_in_bits = expr_size;
										addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
										addr_op.operand.value = std::get<StringHandle>(source_value);
										ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
										
										// Add 'this' as first argument
										TypedValue this_arg;
										this_arg.type = expr_type;
										this_arg.size_in_bits = 64;  // Pointer size
										this_arg.value = this_ptr;
										this_arg.type_index = expr_type_index;
										call_op.args.push_back(std::move(this_arg));
									} else if (std::holds_alternative<TempVar>(source_value)) {
										// It's already a temporary
										// ASSUMPTION: For struct types, TempVars at this point
										// represent the address of the object (not the object value itself).
										TypedValue this_arg;
										this_arg.type = expr_type;
										this_arg.size_in_bits = 64;  // Pointer size for 'this'
										this_arg.value = std::get<TempVar>(source_value);
										this_arg.type_index = expr_type_index;
										call_op.args.push_back(std::move(this_arg));
									}
									
									ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), node.return_token()));
									
									// Replace operands with the result of the conversion
									operands.clear();
									operands.emplace_back(return_type);
									operands.emplace_back(return_size);
									operands.emplace_back(result_var);
								}
							} else {
								// No conversion operator found - fall back to generateTypeConversion
								operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
							}
						} else {
							// No valid type_index - fall back to generateTypeConversion
							operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
						}
					} else {
						// Not a struct type - use standard type conversion
						operands = generateTypeConversion(operands, expr_type, return_type, node.return_token());
					}
				}
			}
			
			// Call any enclosing __finally funclets before returning
			emitSehFinallyCallsBeforeReturn(node.return_token());

			// Create ReturnOp with the return value
			ReturnOp ret_op;

			// Check if operands has at least 3 elements before accessing
			if (operands.size() < 3) {
				FLASH_LOG(Codegen, Error, "Return statement: expression evaluation failed or returned insufficient operands");
				return;
			}
			
			// Extract IrValue from operand[2] - it could be various types
			if (std::holds_alternative<unsigned long long>(operands[2])) {
				ret_op.return_value = std::get<unsigned long long>(operands[2]);
			} else if (std::holds_alternative<TempVar>(operands[2])) {
				TempVar return_temp = std::get<TempVar>(operands[2]);
				ret_op.return_value = return_temp;
				
				// C++17 mandatory copy elision: Check if this is a prvalue (e.g., constructor call result)
				// being returned - prvalues used to initialize objects of the same type must have copies elided
				if (isTempVarRVOEligible(return_temp)) {
					FLASH_LOG_FORMAT(Codegen, Debug,
						"RVO opportunity detected: returning prvalue {} (constructor call result)",
						return_temp.name());
					// Note: Actual copy elision would require hidden return parameter support
					// For now, we just log the opportunity
				}
				
				// Mark the temp as a return value for potential NRVO analysis
				markTempVarAsReturnValue(return_temp);
			} else if (std::holds_alternative<StringHandle>(operands[2])) {
				ret_op.return_value = std::get<StringHandle>(operands[2]);
			} else if (std::holds_alternative<double>(operands[2])) {
				ret_op.return_value = std::get<double>(operands[2]);
			}
			// Use the function's return type, not the expression type
			// This is important when returning references - the function's return type is what matters
			ret_op.return_type = current_function_return_type_;
			ret_op.return_size = current_function_return_size_;
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
		}
		else {
			// Call any enclosing __finally funclets before returning
			emitSehFinallyCallsBeforeReturn(node.return_token());
			// For void returns, we don't need any operands
			ReturnOp ret_op;  // No return value for void
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(ret_op), node.return_token()));
		}
	}
