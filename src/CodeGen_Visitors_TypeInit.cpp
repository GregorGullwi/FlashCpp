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

private:
	// Helper: resolve self-referential struct types in template instantiations.
	// When a template member function references its own class (e.g., const W& in W<T>::operator+=),
	// the type_index may point to the unfinalized template base. This resolves it to the
	// enclosing instantiated struct's type_index by mutating `type` in-place.
	// Important: only resolves when the unfinalized type's name matches the base name of the
	// enclosing struct — avoids incorrectly resolving outer class references in nested classes.
	static void resolveSelfReferentialType(TypeSpecifierNode& type, TypeIndex enclosing_type_index) {
		if (type.type() == Type::Struct && type.type_index() > 0 && type.type_index() < gTypeInfo.size()) {
			auto& ti = gTypeInfo[type.type_index()];
			if (!ti.struct_info_ || ti.struct_info_->total_size == 0) {
				if (enclosing_type_index < gTypeInfo.size()) {
					// Verify this is actually a self-reference by checking that the unfinalized
					// type's name matches the base name of the enclosing struct.
					// For template instantiations: W (unfinalized) matches W$hash (enclosing)
					// For nested classes: Outer (unfinalized) does NOT match Outer::Inner (enclosing)
					auto unfinalized_name = StringTable::getStringView(ti.name());
					auto enclosing_name = StringTable::getStringView(gTypeInfo[enclosing_type_index].name());
					
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

	// Helper: generate a member function call for user-defined operator++/-- overloads on structs.
	// Returns the IR operands {result_type, result_size, ret_var, result_type_index} on success,
	// or std::nullopt if no overload was found.
	std::optional<std::vector<IrOperand>> generateUnaryIncDecOverloadCall(
		std::string_view op_name,  // "++" or "--"
		Type operandType,
		const std::vector<IrOperand>& operandIrOperands,
		bool is_prefix
	) {
		if (operandType != Type::Struct || operandIrOperands.size() < 4)
			return std::nullopt;

		TypeIndex operand_type_index = 0;
		if (std::holds_alternative<unsigned long long>(operandIrOperands[3])) {
			operand_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(operandIrOperands[3]));
		}
		if (operand_type_index == 0)
			return std::nullopt;

		// For ++/--, we need to distinguish prefix (0 params) from postfix (1 param: dummy int).
		// findUnaryOperatorOverload returns the first match; scan all member functions to pick
		// the overload whose parameter count matches the call form.
		size_t expected_param_count = is_prefix ? 0 : 1;
		const StructMemberFunction* matched_func = nullptr;
		const StructMemberFunction* fallback_func = nullptr;
		if (operand_type_index < gTypeInfo.size()) {
			const StructTypeInfo* struct_info = gTypeInfo[operand_type_index].getStructInfo();
			if (struct_info) {
				for (const auto& mf : struct_info->member_functions) {
					if (mf.is_operator_overload && mf.operator_symbol == op_name) {
						const auto& fd = mf.function_decl.as<FunctionDeclarationNode>();
						if (fd.parameter_nodes().size() == expected_param_count) {
							matched_func = &mf;
							break;
						}
						if (!fallback_func) fallback_func = &mf;
					}
				}
			}
		}
		// Fallback: if no exact arity match, use any operator++ / operator-- overload.
		// This handles the common case where only one form (prefix or postfix) is defined.
		if (!matched_func) matched_func = fallback_func;
		if (!matched_func)
			return std::nullopt;

		const StructMemberFunction& member_func = *matched_func;
		const FunctionDeclarationNode& func_decl = member_func.function_decl.as<FunctionDeclarationNode>();
		std::string_view struct_name = StringTable::getStringView(gTypeInfo[operand_type_index].name());
		TypeSpecifierNode return_type = func_decl.decl_node().type_node().as<TypeSpecifierNode>();
		resolveSelfReferentialType(return_type, operand_type_index);

		std::vector<TypeSpecifierNode> param_types;
		// Use the matched function's actual parameter count for mangling, not the call form.
		// When the fallback path is taken (e.g., only prefix defined but postfix called),
		// we must mangle to match the definition, not the call site.
		const auto& actual_params = func_decl.parameter_nodes();
		if (actual_params.size() == 1 && actual_params[0].is<DeclarationNode>()) {
			// Postfix overload has a dummy int parameter
			TypeSpecifierNode int_type(Type::Int, TypeQualifier::None, 32, Token());
			param_types.push_back(int_type);
		}
		std::vector<std::string_view> empty_namespace;
		auto op_func_name = StringBuilder().append("operator").append(op_name).commit();
		auto mangled_name = NameMangling::generateMangledName(
			op_func_name, return_type, param_types, false,
			struct_name, empty_namespace, Linkage::CPlusPlus
		);

		TempVar ret_var = var_counter.next();
		CallOp call_op;
		call_op.result = ret_var;
		call_op.function_name = StringTable::getOrInternStringHandle(mangled_name);
		call_op.return_type = return_type.type();
		call_op.return_size_in_bits = static_cast<int>(return_type.size_in_bits());
		if (call_op.return_size_in_bits == 0 && return_type.type_index() > 0 && return_type.type_index() < gTypeInfo.size() && gTypeInfo[return_type.type_index()].struct_info_) {
			call_op.return_size_in_bits = static_cast<int>(gTypeInfo[return_type.type_index()].struct_info_->total_size * 8);
		}
		call_op.return_type_index = return_type.type_index();
		call_op.is_member_function = true;

		// Take address of operand for 'this' pointer
		TempVar this_addr = var_counter.next();
		AddressOfOp addr_op;
		addr_op.result = this_addr;
		addr_op.operand = toTypedValue(operandIrOperands);
		addr_op.operand.pointer_depth = 0;
		ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));

		TypedValue this_arg;
		this_arg.type = operandType;
		this_arg.size_in_bits = 64;
		this_arg.value = this_addr;
		call_op.args.push_back(this_arg);

		// For postfix operators, pass dummy int argument (value 0)
		if (!is_prefix) {
			TypedValue dummy_arg;
			dummy_arg.type = Type::Int;
			dummy_arg.size_in_bits = 32;
			dummy_arg.value = 0ULL;
			call_op.args.push_back(dummy_arg);
		}

		int result_size = call_op.return_size_in_bits;
		TypeIndex result_type_index = call_op.return_type_index;
		Type result_type = call_op.return_type;
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), Token()));
		return std::vector<IrOperand>{ result_type, result_size, ret_var, static_cast<unsigned long long>(result_type_index) };
	}

	// Helper: generate built-in pointer or integer increment/decrement IR.
	// Handles pointer arithmetic (add/subtract element_size) and integer pre/post inc/dec.
	// is_increment: true for ++, false for --
	std::vector<IrOperand> generateBuiltinIncDec(
		bool is_increment,
		bool is_prefix,
		bool operandHandledAsIdentifier,
		const UnaryOperatorNode& unaryOperatorNode,
		const std::vector<IrOperand>& operandIrOperands,
		Type operandType,
		TempVar result_var
	) {
		// Check if this is a pointer increment/decrement (requires pointer arithmetic)
		bool is_pointer = false;
		int element_size = 1;
		if (operandHandledAsIdentifier && unaryOperatorNode.get_operand().is<ExpressionNode>()) {
			const ExpressionNode& operandExpr = unaryOperatorNode.get_operand().as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(operandExpr)) {
				const IdentifierNode& identifier = std::get<IdentifierNode>(operandExpr);
				auto symbol = symbol_table.lookup(identifier.name());
				if (symbol.has_value()) {
					const TypeSpecifierNode* type_node = nullptr;
					if (symbol->is<DeclarationNode>()) {
						type_node = &symbol->as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
					} else if (symbol->is<VariableDeclarationNode>()) {
						type_node = &symbol->as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
					}
					
					if (type_node && type_node->pointer_depth() > 0) {
						is_pointer = true;
						if (type_node->pointer_depth() > 1) {
							element_size = 8;  // Multi-level pointer: element is a pointer
						} else {
							element_size = getSizeInBytes(type_node->type(), type_node->type_index(), type_node->size_in_bits());
						}
					}
				}
			}
		}
		
		UnaryOp unary_op{
			.value = toTypedValue(operandIrOperands),
			.result = result_var
		};
		
		IrOpcode arith_opcode = is_increment ? IrOpcode::Add : IrOpcode::Subtract;
		
		if (is_pointer) {
			// For pointers, use a BinaryOp to add/subtract element_size
			// Extract the pointer operand value once (used in multiple BinaryOp/AssignmentOp below)
			IrValue ptr_operand = std::holds_alternative<StringHandle>(operandIrOperands[2])
				? IrValue(std::get<StringHandle>(operandIrOperands[2])) : IrValue{};
			
			if (is_prefix) {
				BinaryOp bin_op{
					.lhs = { Type::UnsignedLongLong, 64, ptr_operand },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(arith_opcode, std::move(bin_op), Token()));
				// Store back to the pointer variable
				if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
					AssignmentOp assign_op;
					assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
					assign_op.lhs = { Type::UnsignedLongLong, 64, ptr_operand };
					assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
				}
				return { operandType, 64, result_var, 0ULL };
			} else {
				// Postfix: save old value, modify, return old value
				TempVar old_value = var_counter.next();
				if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
					AssignmentOp save_op;
					save_op.result = old_value;
					save_op.lhs = { Type::UnsignedLongLong, 64, old_value };
					save_op.rhs = toTypedValue(operandIrOperands);
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(save_op), Token()));
				}
				BinaryOp bin_op{
					.lhs = { Type::UnsignedLongLong, 64, ptr_operand },
					.rhs = { Type::Int, 32, static_cast<unsigned long long>(element_size) },
					.result = result_var,
				};
				ir_.addInstruction(IrInstruction(arith_opcode, std::move(bin_op), Token()));
				// Store back to the pointer variable
				if (std::holds_alternative<StringHandle>(operandIrOperands[2])) {
					AssignmentOp assign_op;
					assign_op.result = std::get<StringHandle>(operandIrOperands[2]);
					assign_op.lhs = { Type::UnsignedLongLong, 64, ptr_operand };
					assign_op.rhs = { Type::UnsignedLongLong, 64, result_var };
					ir_.addInstruction(IrInstruction(IrOpcode::Assignment, std::move(assign_op), Token()));
				}
				return { operandType, 64, old_value, 0ULL };
			}
		} else {
			// Regular integer increment/decrement
			IrOpcode pre_opcode = is_increment ? IrOpcode::PreIncrement : IrOpcode::PreDecrement;
			IrOpcode post_opcode = is_increment ? IrOpcode::PostIncrement : IrOpcode::PostDecrement;
			ir_.addInstruction(IrInstruction(is_prefix ? pre_opcode : post_opcode, unary_op, Token()));
		}
		
		return { operandType, std::get<int>(operandIrOperands[1]), result_var, 0ULL };
	}

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
				member_store.is_reference = member.is_reference();
				member_store.is_rvalue_reference = member.is_rvalue_reference();
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
					member_store.is_reference = member.is_reference();
					member_store.is_rvalue_reference = member.is_rvalue_reference();
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
					member_store.is_reference = member.is_reference();
					member_store.is_rvalue_reference = member.is_rvalue_reference();
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
				member_store.is_reference = member.is_reference();
				member_store.is_rvalue_reference = member.is_rvalue_reference();
				member_store.struct_type_info = nullptr;
				ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), token));
			}
		}
	}

	// Helper function to convert a MemberFunctionCallNode to a regular FunctionCallNode
	// Used when a member function call syntax is used but the object is not a struct
	std::vector<IrOperand> convertMemberCallToFunctionCall(const MemberFunctionCallNode& memberFunctionCallNode) {
		const FunctionDeclarationNode& func_decl = memberFunctionCallNode.function_declaration();
		const DeclarationNode& decl_node = func_decl.decl_node();
		
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
								member_is_reference = result.member->is_reference();
								member_is_rvalue_reference = result.member->is_rvalue_reference();
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

