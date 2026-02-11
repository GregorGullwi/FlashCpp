	void visitBlockNode(const BlockNode& node) {
		// Check if this block contains only VariableDeclarationNodes
		// If so, it's likely from comma-separated declarations and shouldn't create a new scope
		bool only_var_decls = true;
		size_t var_decl_count = 0;
		node.get_statements().visit([&](const ASTNode& statement) {
			if (statement.is<VariableDeclarationNode>()) {
				var_decl_count++;
			} else {
				only_var_decls = false;
			}
		});

		// For blocks that only contain two or more variable declarations, don't enter a new scope
		// This handles comma-separated declarations like: int a = 1, b = 2;
		// which the parser represents as a BlockNode containing multiple VariableDeclarationNodes
		// Single variable declarations in blocks (e.g., { int x = 5; }) should create a scope
		bool enter_scope = !(only_var_decls && var_decl_count > 1);

		if (enter_scope) {
			// Enter a new scope
			symbol_table.enter_scope(ScopeType::Block);
			enterScope();
			ir_.addInstruction(IrOpcode::ScopeBegin, {}, Token());
		}

		// Visit all statements in the block
		node.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});

		if (enter_scope) {
			// Exit scope and call destructors
			exitScope();
			ir_.addInstruction(IrOpcode::ScopeEnd, {}, Token());
			symbol_table.exit_scope();
		}
	}

	void visitIfStatementNode(const IfStatementNode& node) {
		// Handle C++17 if constexpr - evaluate condition at compile time
		if (node.is_constexpr()) {
			// Evaluate the condition at compile time
			ConstExpr::EvaluationContext ctx(gSymbolTable);
			auto result = ConstExpr::Evaluator::evaluate(node.get_condition(), ctx);
			
			if (!result.success()) {
				FLASH_LOG(Codegen, Error, "if constexpr condition is not a constant expression: ", 
				          result.error_message);
				return;
			}

			// Only compile the taken branch
			if (result.as_bool()) {
				// Compile then branch - use visit() for proper scope handling
				auto then_stmt = node.get_then_statement();
				visit(then_stmt);
			} else if (node.has_else()) {
				// Compile else branch - use visit() for proper scope handling
				auto else_stmt = node.get_else_statement();
				if (else_stmt.has_value()) {
					visit(*else_stmt);
				}
			}
			// Note: Non-taken branch is completely discarded (not compiled)
			return;
		}

		// Regular if statement (runtime conditional)
		// Generate unique labels for this if statement
		static size_t if_counter = 0;
		size_t current_if = if_counter++;
	
	// Use a single StringBuilder and commit each label before starting the next
	// to avoid buffer overwrites in the shared allocator
	StringBuilder label_sb;
	label_sb.append("if_then_").append(current_if);
	std::string_view then_label = label_sb.commit();
	
	label_sb.append("if_else_").append(current_if);
	std::string_view else_label = label_sb.commit();
	
	label_sb.append("if_end_").append(current_if);
	std::string_view end_label = label_sb.commit();

		// Handle C++20 if-with-initializer
		if (node.has_init()) {
			auto init_stmt = node.get_init_statement();
			if (init_stmt.has_value()) {
				visit(*init_stmt);
			}
		}

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch
		CondBranchOp cond_branch;
		cond_branch.label_true = StringTable::getOrInternStringHandle(then_label);
		cond_branch.label_false = StringTable::getOrInternStringHandle(node.has_else() ? else_label : end_label);
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Then block
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(then_label)}, Token()));

		// Visit then statement - always use visit() to properly handle block scopes
		auto then_stmt = node.get_then_statement();
		visit(then_stmt);

		// Branch to end after then block (skip else)
		if (node.has_else()) {
			BranchOp branch_to_end;
			branch_to_end.target_label = StringTable::getOrInternStringHandle(end_label);
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_end), Token()));
		}

		// Else block (if present)
		if (node.has_else()) {
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(else_label)}, Token()));

			auto else_stmt = node.get_else_statement();
			if (else_stmt.has_value()) {
				// Always use visit() to properly handle block scopes
				visit(*else_stmt);
			}
		}

		// End label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(end_label)}, Token()));
	}

	void visitForStatementNode(const ForStatementNode& node) {
		// Enter a new scope for the for loop (C++ standard: for-init-statement creates a scope)
		symbol_table.enter_scope(ScopeType::Block);
		enterScope();
		
		// Generate unique labels for this for loop
		static size_t for_counter = 0;
		size_t current_for = for_counter++;
		
		// Use a single StringBuilder and commit each label before starting the next
		auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("for_start_").append(current_for));
		auto loop_body_label = StringTable::createStringHandle(StringBuilder().append("for_body_").append(current_for));
		auto loop_increment_label = StringTable::createStringHandle(StringBuilder().append("for_increment_").append(current_for));
		auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("for_end_").append(current_for));

		// Execute init statement (if present)
		if (node.has_init()) {
			auto init_stmt = node.get_init_statement();
			if (init_stmt.has_value()) {
				visit(*init_stmt);
			}
		}

		// Mark loop begin for break/continue support
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_increment_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: evaluate condition
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start_label}, Token()));

		// Evaluate condition (if present, otherwise infinite loop)
		if (node.has_condition()) {
			auto condition_operands = visitExpressionNode(node.get_condition()->as<ExpressionNode>());

			// Generate conditional branch: if true goto body, else goto end
			CondBranchOp cond_branch;
			cond_branch.label_true = loop_body_label;
			cond_branch.label_false = loop_end_label;
			cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
			ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));
		}

		// Loop body label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body_label}, Token()));

		// Visit loop body
		// Always call visit() to let visitBlockNode handle scope creation if needed
		auto body_stmt = node.get_body_statement();
		visit(body_stmt);

		// Loop increment label (for continue statements)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_increment_label}, Token()));

		// Execute update/increment expression (if present)
		if (node.has_update()) {
			visitExpressionNode(node.get_update_expression()->as<ExpressionNode>());
		}

		// Branch back to loop start
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start_label}, Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
		
		// Exit the for loop scope
		exitScope();
		symbol_table.exit_scope();
	}

	void visitWhileStatementNode(const WhileStatementNode& node) {
		// Generate unique labels for this while loop
		static size_t while_counter = 0;
		size_t current_while = while_counter++;
		
		// Use a single StringBuilder and commit each label before starting the next
		auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("while_start_").append(current_while));
		auto loop_body_label = StringTable::createStringHandle(StringBuilder().append("while_body_").append(current_while));
		auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("while_end_").append(current_while));
		
		// Mark loop begin for break/continue support
		// For while loops, continue jumps to loop_start (re-evaluate condition)
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_start_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: evaluate condition
		LabelOp start_lbl;
		start_lbl.label_name = loop_start_label;
		ir_.addInstruction(IrInstruction(IrOpcode::Label, std::move(start_lbl), Token()));

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch: if true goto body, else goto end
		CondBranchOp cond_branch;
		cond_branch.label_true = loop_body_label;
		cond_branch.label_false = loop_end_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Loop body label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body_label}, Token()));

		// Visit loop body
		// Always call visit() to let visitBlockNode handle scope creation if needed
		auto body_stmt = node.get_body_statement();
		visit(body_stmt);

		// Branch back to loop start (re-evaluate condition)
		BranchOp branch_to_start;
		branch_to_start.target_label = loop_start_label;
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_start), Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitDoWhileStatementNode(const DoWhileStatementNode& node) {
		// Generate unique labels for this do-while loop
		static size_t do_while_counter = 0;
		size_t current_do_while = do_while_counter++;
		
		// Use a single StringBuilder and commit each label before starting the next
		auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("do_while_start_").append(current_do_while));
		auto loop_condition_label = StringTable::createStringHandle(StringBuilder().append("do_while_condition_").append(current_do_while));
		auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("do_while_end_").append(current_do_while));
		
		// Mark loop begin for break/continue support
		// For do-while loops, continue jumps to condition check (not body start)
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_condition_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: execute body first (do-while always executes at least once)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start_label}, Token()));

		// Visit loop body
		// Always call visit() to let visitBlockNode handle scope creation if needed
		auto body_stmt = node.get_body_statement();
		visit(body_stmt);

		// Condition check label (for continue statements)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_condition_label}, Token()));

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch: if true goto start, else goto end
		CondBranchOp cond_branch;
		cond_branch.label_true = loop_start_label;
		cond_branch.label_false = loop_end_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitSwitchStatementNode(const SwitchStatementNode& node) {
		// Generate unique labels for this switch statement
		static size_t switch_counter = 0;
		StringHandle default_label = StringTable::getOrInternStringHandle(StringBuilder().append("switch_default_").append(switch_counter));
		StringHandle switch_end_label = StringTable::getOrInternStringHandle(StringBuilder().append("switch_end_").append(switch_counter));
		switch_counter++;
		
		// Evaluate the switch condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Get the condition type and value
		Type condition_type = std::get<Type>(condition_operands[0]);
		int condition_size = std::get<int>(condition_operands[1]);

		// Mark switch begin for break support (switch acts like a loop for break)
		// Continue is not allowed in switch, but break is
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = switch_end_label;
		loop_begin.loop_end_label = switch_end_label;
		loop_begin.loop_increment_label = switch_end_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Process the switch body to collect case labels
		auto body = node.get_body();
		if (!body.is<BlockNode>()) {
			assert(false && "Switch body must be a BlockNode");
			return;
		}

		const BlockNode& block = body.as<BlockNode>();
		std::vector<std::pair<std::string_view, ASTNode>> case_labels;  // label name, case value
		bool has_default = false;		// First pass: generate labels and collect case values
		size_t case_index = 0;
		block.get_statements().visit([&](const ASTNode& stmt) {
			if (stmt.is<CaseLabelNode>()) {
				StringBuilder case_sb;
				case_sb.append("switch_case_").append(switch_counter - 1).append("_").append(case_index);
				std::string_view case_label = case_sb.commit();
				case_labels.push_back({case_label, stmt.as<CaseLabelNode>().get_case_value()});
				case_index++;
			} else if (stmt.is<DefaultLabelNode>()) {
				has_default = true;
			}
		});

		// Generate comparison chain for each case
		size_t check_index = 0;
		for (const auto& [case_label, case_value_node] : case_labels) {
			// Evaluate case value (must be constant)
			auto case_value_operands = visitExpressionNode(case_value_node.as<ExpressionNode>());

			// Compare condition with case value using Equal opcode
			TempVar cmp_result = var_counter.next();
			
			// Create typed BinaryOp for the Equal comparison
			BinaryOp bin_op{
				.lhs = TypedValue{.type = condition_type, .size_in_bits = condition_size, .value = toIrValue(condition_operands[2])},
				.rhs = TypedValue{.type = std::get<Type>(case_value_operands[0]), .size_in_bits = std::get<int>(case_value_operands[1]), .value = toIrValue(case_value_operands[2])},
				.result = cmp_result,
			};
			ir_.addInstruction(IrInstruction(IrOpcode::Equal, std::move(bin_op), Token()));

			// Branch to case label if equal, otherwise check next case
			StringBuilder next_check_sb;
			next_check_sb.append("switch_check_").append(switch_counter - 1).append("_").append(check_index + 1);
			std::string_view next_check_label = next_check_sb.commit();

			// For switch statements, we need to jump to case label when condition is true
			// and fall through to next check when false. Since both labels are forward references,
			// we emit: test condition; jz next_check; jmp case_label
			CondBranchOp cond_branch;
			cond_branch.label_true = StringTable::getOrInternStringHandle(case_label);       // Fall through to unconditional branch when TRUE
			cond_branch.label_false = StringTable::getOrInternStringHandle(next_check_label); // Jump to next check when FALSE
			cond_branch.condition = TypedValue{.type = Type::Bool, .size_in_bits = 1, .value = cmp_result};
			ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

			// Unconditional branch to case label (when condition is true, we fall through here)
			BranchOp branch_to_case;
			branch_to_case.target_label = StringTable::getOrInternStringHandle(case_label);
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_case), Token()));

			// Next check label
			LabelOp next_lbl;
			next_lbl.label_name = StringTable::getOrInternStringHandle(next_check_label);
			ir_.addInstruction(IrInstruction(IrOpcode::Label, std::move(next_lbl), Token()));
			check_index++;
		}

		// If no case matched, jump to default or end
		if (has_default) {
			BranchOp branch_to_default;
			branch_to_default.target_label = default_label;
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_default), Token()));
		} else {
			BranchOp branch_to_end;
			branch_to_end.target_label = switch_end_label;
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, std::move(branch_to_end), Token()));
		}

		// Second pass: generate code for each case/default
		case_index = 0;
		block.get_statements().visit([&](const ASTNode& stmt) {
			if (stmt.is<CaseLabelNode>()) {
			const CaseLabelNode& case_node = stmt.as<CaseLabelNode>();
			StringBuilder case_sb;
			case_sb.append("switch_case_").append(switch_counter - 1).append("_").append(case_index);
			std::string_view case_label = case_sb.commit();

			// Case label
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(case_label)}, Token()));				// Execute case statements
				if (case_node.has_statement()) {
					auto case_stmt = case_node.get_statement();
					if (case_stmt->is<BlockNode>()) {
						case_stmt->as<BlockNode>().get_statements().visit([&](ASTNode statement) {
							visit(statement);
						});
					} else {
						visit(*case_stmt);
					}
				}
				// Note: Fall-through is automatic - no break means execution continues to next case

				case_index++;
			} else if (stmt.is<DefaultLabelNode>()) {
			const DefaultLabelNode& default_node = stmt.as<DefaultLabelNode>();

			// Default label
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = default_label}, Token()));				// Execute default statements
				if (default_node.has_statement()) {
					auto default_stmt = default_node.get_statement();
					if (default_stmt->is<BlockNode>()) {
						default_stmt->as<BlockNode>().get_statements().visit([&](ASTNode statement) {
							visit(statement);
						});
					} else {
						visit(*default_stmt);
					}
				}
			}
		});

		// Switch end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = switch_end_label}, Token()));

		// Mark switch end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitRangedForStatementNode(const RangedForStatementNode& node) {
		// Desugar ranged for loop into traditional for loop
		// For arrays: for (int x : arr) { body } becomes:
		//   for (int __i = 0; __i < array_size; ++__i) { int x = arr[__i]; body }
		// For types with begin()/end(): for (int x : vec) { body } becomes:
		//   for (auto __begin = vec.begin(), __end = vec.end(); __begin != __end; ++__begin) { int x = *__begin; body }
		// C++20: for (init; decl : range) { body } first executes init, then the above

		// C++20: Handle optional init-statement if present
		if (node.has_init_statement()) {
			visit(*node.get_init_statement());
		}

		// Generate unique labels and counter for this ranged for loop
		static size_t ranged_for_counter = 0;
		auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("ranged_for_start_").append(ranged_for_counter));
		auto loop_body_label = StringTable::createStringHandle(StringBuilder().append("ranged_for_body_").append(ranged_for_counter));
		auto loop_increment_label = StringTable::createStringHandle(StringBuilder().append("ranged_for_increment_").append(ranged_for_counter));
		auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("ranged_for_end_").append(ranged_for_counter));
		
		ranged_for_counter++;
		
		// Get the loop variable declaration and range expression
		auto loop_var_decl = node.get_loop_variable_decl();
		auto range_expr = node.get_range_expression();

		// C++11+ standard: The range expression is bound to a reference for lifetime extension
		// This ensures temporary objects live for the entire loop duration
		// For now, we only support simple identifiers (not temporaries), so lifetime is already correct
		
		// Check what kind of range expression we have
		if (!range_expr.is<ExpressionNode>()) {
			FLASH_LOG(Codegen, Error, "Range expression must be an expression");
			return;
		}

		auto& expr_variant = range_expr.as<ExpressionNode>();
		if (!std::holds_alternative<IdentifierNode>(expr_variant)) {
			FLASH_LOG(Codegen, Error, "Currently only identifiers are supported as range expressions");
			return;
		}

		const IdentifierNode& range_ident = std::get<IdentifierNode>(expr_variant);
		std::string_view range_name = range_ident.name();

		// Look up the range object in the symbol table
		const std::optional<ASTNode> range_symbol = symbol_table.lookup(range_name);
		if (!range_symbol.has_value()) {
			FLASH_LOG(Codegen, Error, "Range object '", range_name, "' not found in symbol table");
			return;
		}

		// Extract the DeclarationNode from either DeclarationNode or VariableDeclarationNode
		const DeclarationNode* range_decl_ptr = nullptr;
		if (range_symbol->is<DeclarationNode>()) {
			range_decl_ptr = &range_symbol->as<DeclarationNode>();
		} else if (range_symbol->is<VariableDeclarationNode>()) {
			range_decl_ptr = &range_symbol->as<VariableDeclarationNode>().declaration();
		} else {
			FLASH_LOG(Codegen, Error, "Range object '", range_name, "' is not a variable declaration");
			return;
		}

		const DeclarationNode& range_decl = *range_decl_ptr;
		const TypeSpecifierNode& range_type = range_decl.type_node().as<TypeSpecifierNode>();

		// C++ standard: pointers are NOT valid range expressions (no size information)
		// Only arrays and types with begin()/end() are allowed
		if (range_type.pointer_depth() > 0 && !range_decl.is_array()) {
			FLASH_LOG(Codegen, Error, "Cannot use pointer in range-based for loop; use array or type with begin()/end()");
			return;
		}

		// Check if it's an array
		if (range_decl.is_array()) {
			// Array-based range-for loop
			visitRangedForArray(node, range_name, range_decl, loop_start_label, loop_body_label, 
			                    loop_increment_label, loop_end_label, ranged_for_counter - 1);
		}
		// Check if it's a struct with begin()/end() methods
		else if (range_type.type() == Type::Struct) {
			visitRangedForBeginEnd(node, range_name, range_type, loop_start_label, loop_body_label,
			                       loop_increment_label, loop_end_label, ranged_for_counter - 1);
		}
		else {
			FLASH_LOG(Codegen, Error, "Range expression must be an array or a type with begin()/end() methods");
			return;
		}
	}

	void visitRangedForArray(const RangedForStatementNode& node, std::string_view array_name,
	                         const DeclarationNode& array_decl, StringHandle loop_start_label,
	                         StringHandle loop_body_label, StringHandle loop_increment_label,
	                         StringHandle loop_end_label, size_t counter) {
		auto loop_var_decl = node.get_loop_variable_decl();

		// Unified pointer-based approach: use begin/end pointers for arrays too
		// This is more efficient (no indexing multiplication) and matches what optimizing compilers do
		// For array: auto __begin = &array[0]; auto __end = &array[size]; for (; __begin != __end; ++__begin)

		// Get array size
		auto array_size_node = array_decl.array_size();
		if (!array_size_node.has_value()) {
			FLASH_LOG(Codegen, Error, "Array must have a known size for range-based for loop");
			return;
		}

		// Create begin/end pointer variable names
		StringBuilder sb_begin;
		sb_begin.append("__range_begin_");
		sb_begin.append(counter);
		std::string_view begin_var_name = sb_begin.commit();

		StringBuilder sb_end;
		sb_end.append("__range_end_");
		sb_end.append(counter);
		std::string_view end_var_name = sb_end.commit();

		Token begin_token(Token::Type::Identifier, begin_var_name, 0, 0, 0);
		Token end_token(Token::Type::Identifier, end_var_name, 0, 0, 0);

		// Get the array element type to create pointer type
		const TypeSpecifierNode& array_type = array_decl.type_node().as<TypeSpecifierNode>();
		
		// Calculate the actual element size for pointer arithmetic
		int element_size_bits;
		if (array_type.pointer_depth() > 0) {
			// Array of pointers - element size is pointer size (64 bits)
			element_size_bits = 64;
		} else if (array_type.type() == Type::Struct) {
			// Array of structs - lookup size from type info
			TypeIndex type_index = array_type.type_index();
			if (type_index > 0 && type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_index];
				const StructTypeInfo* struct_info = type_info.getStructInfo();
				if (struct_info) {
					element_size_bits = static_cast<int>(struct_info->total_size * 8);
				} else {
					element_size_bits = static_cast<int>(array_type.size_in_bits());
				}
			} else {
				element_size_bits = static_cast<int>(array_type.size_in_bits());
			}
		} else {
			// Regular array of primitives - use type size
			element_size_bits = static_cast<int>(array_type.size_in_bits());
			if (element_size_bits == 0) {
				element_size_bits = get_type_size_bits(array_type.type());
			}
		}
		
		// Create pointer type for begin/end (element_type*)
		// The size_in_bits should be the element size for correct pointer arithmetic
		auto begin_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			array_type.type(), array_type.type_index(), element_size_bits, Token()
		);
		begin_type_node.as<TypeSpecifierNode>().add_pointer_level();
		auto begin_decl_node = ASTNode::emplace_node<DeclarationNode>(begin_type_node, begin_token);

		auto end_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			array_type.type(), array_type.type_index(), element_size_bits, Token()
		);
		end_type_node.as<TypeSpecifierNode>().add_pointer_level();
		auto end_decl_node = ASTNode::emplace_node<DeclarationNode>(end_type_node, end_token);

		// Create begin = &array[0]
		auto array_expr_begin = ASTNode::emplace_node<ExpressionNode>(
			IdentifierNode(Token(Token::Type::Identifier, array_name, 0, 0, 0))
		);
		auto zero_literal = ASTNode::emplace_node<ExpressionNode>(
			NumericLiteralNode(Token(Token::Type::Literal, "0"sv, 0, 0, 0),
				static_cast<unsigned long long>(0), Type::Int, TypeQualifier::None, 32)
		);
		auto first_element = ASTNode::emplace_node<ExpressionNode>(
			ArraySubscriptNode(array_expr_begin, zero_literal, Token(Token::Type::Punctuator, "["sv, 0, 0, 0))
		);
		auto begin_init = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "&"sv, 0, 0, 0), first_element, true)
		);
		auto begin_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(begin_decl_node, begin_init);
		visit(begin_var_decl_node);

		// Create end = &array[size] (one past the last element)
		auto array_expr_end = ASTNode::emplace_node<ExpressionNode>(
			IdentifierNode(Token(Token::Type::Identifier, array_name, 0, 0, 0))
		);
		auto past_end_element = ASTNode::emplace_node<ExpressionNode>(
			ArraySubscriptNode(array_expr_end, array_size_node.value(), Token(Token::Type::Punctuator, "["sv, 0, 0, 0))
		);
		auto end_init = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "&"sv, 0, 0, 0), past_end_element, true)
		);
		auto end_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(end_decl_node, end_init);
		visit(end_var_decl_node);

		// Mark loop begin for break/continue support
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_increment_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: evaluate condition (__begin != __end)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start_label}, Token()));

		// Create condition: __begin != __end
		auto begin_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto end_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(end_token));
		auto condition_expr = ASTNode::emplace_node<ExpressionNode>(
			BinaryOperatorNode(Token(Token::Type::Operator, "!="sv, 0, 0, 0), begin_ident_expr, end_ident_expr)
		);
		auto condition_operands = visitExpressionNode(condition_expr.as<ExpressionNode>());

		// Generate conditional branch
		CondBranchOp cond_branch;
		cond_branch.label_true = loop_body_label;
		cond_branch.label_false = loop_end_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Loop body label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body_label}, Token()));

		// Declare and initialize the loop variable
		// For references (T& x or const T& x), we need the pointer value directly
		// For non-references (T x), we need the dereferenced value (*__begin)
		
		// Check if the loop variable is a reference
		if (!loop_var_decl.is<VariableDeclarationNode>()) {
			FLASH_LOG(Codegen, Error, "loop_var_decl is not a VariableDeclarationNode!");
			return;
		}
		const VariableDeclarationNode& original_var_decl = loop_var_decl.as<VariableDeclarationNode>();
		ASTNode loop_decl_node = original_var_decl.declaration_node();
		
		// C++20 standard: range-for desugars to `decl = *__begin;` for BOTH
		// value and reference loop variables. The iterator is always dereferenced.
		// For `int& c : arr`, this becomes `int& c = *__begin;`
		// For `int c : arr`,  this becomes `int c = *__begin;`
		auto begin_deref_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		ASTNode init_expr = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "*"sv, 0, 0, 0), begin_deref_expr, true)
		);
		
		auto loop_var_with_init = ASTNode::emplace_node<VariableDeclarationNode>(loop_decl_node, init_expr);

		// Generate IR for loop variable declaration
		// Note: visitVariableDeclarationNode will add it to the symbol table
		visit(loop_var_with_init);

		// Visit loop body - use visit() to properly handle block scopes
		auto body_stmt = node.get_body_statement();
		visit(body_stmt);

		// Loop increment label (for continue statements)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_increment_label}, Token()));

		// Increment pointer: ++__begin
		auto increment_begin = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto increment_expr = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "++"sv, 0, 0, 0), increment_begin, true)
		);
		visitExpressionNode(increment_expr.as<ExpressionNode>());

		// Branch back to loop start
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start_label}, Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitRangedForBeginEnd(const RangedForStatementNode& node, std::string_view range_name,
	                            const TypeSpecifierNode& range_type, StringHandle loop_start_label,
	                            StringHandle loop_body_label, StringHandle loop_increment_label,
	                            StringHandle loop_end_label, size_t counter) {
		auto loop_var_decl = node.get_loop_variable_decl();

		// Get the struct type info
		if (range_type.type_index() >= gTypeInfo.size()) {
			FLASH_LOG(Codegen, Error, "Invalid type index for range expression");
			return;
		}

		const TypeInfo& type_info = gTypeInfo[range_type.type_index()];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (!struct_info) {
			FLASH_LOG(Codegen, Error, "Range expression is not a struct type");
			return;
		}

		// Check for begin() and end() methods
		const StructMemberFunction* begin_func = struct_info->findMemberFunction("begin"sv);
		const StructMemberFunction* end_func = struct_info->findMemberFunction("end"sv);

		if (!begin_func || !end_func) {
			FLASH_LOG(Codegen, Error, "Range-based for loop requires type to have both begin() and end() methods");
			return;
		}

		// Create iterator variables: auto __begin = range.begin(), __end = range.end()
		StringBuilder sb_begin;
		sb_begin.append("__range_begin_");
		sb_begin.append(counter);
		std::string_view begin_var_name = sb_begin.commit();

		StringBuilder sb_end;
		sb_end.append("__range_end_");
		sb_end.append(counter);
		std::string_view end_var_name = sb_end.commit();

		// Get return type from begin() - should be a pointer type
		const FunctionDeclarationNode& begin_func_decl = begin_func->function_decl.as<FunctionDeclarationNode>();
		const TypeSpecifierNode& begin_return_type = begin_func_decl.decl_node().type_node().as<TypeSpecifierNode>();

		// Standard C++20 range-for with begin()/end() desugars to:
		//   auto __begin = range.begin();
		//   auto __end = range.end();
		//   for (; __begin != __end; ++__begin) { decl = *__begin; body; }
		
		Token begin_token(Token::Type::Identifier, begin_var_name, 0, 0, 0);
		Token end_token(Token::Type::Identifier, end_var_name, 0, 0, 0);

		// Create type nodes for the iterator variables (they're pointers typically)
		auto begin_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			begin_return_type.type(), begin_return_type.type_index(), begin_return_type.size_in_bits(), Token()
		);
		begin_type_node.as<TypeSpecifierNode>().copy_indirection_from(begin_return_type);
		auto begin_decl_node = ASTNode::emplace_node<DeclarationNode>(begin_type_node, begin_token);

		auto end_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
			begin_return_type.type(), begin_return_type.type_index(), begin_return_type.size_in_bits(), Token()
		);
		end_type_node.as<TypeSpecifierNode>().copy_indirection_from(begin_return_type);
		auto end_decl_node = ASTNode::emplace_node<DeclarationNode>(end_type_node, end_token);

		// Create member function calls: range.begin() and range.end()
		auto range_expr_for_begin = ASTNode::emplace_node<ExpressionNode>(
			IdentifierNode(Token(Token::Type::Identifier, range_name, 0, 0, 0))
		);
		
		ChunkedVector<ASTNode> empty_args;
		auto begin_call_expr = ASTNode::emplace_node<ExpressionNode>(
			MemberFunctionCallNode(range_expr_for_begin, 
			                       const_cast<FunctionDeclarationNode&>(begin_func_decl),
			                       std::move(empty_args), Token())
		);
		
		auto begin_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(begin_decl_node, begin_call_expr);
		visit(begin_var_decl_node);

		// Similarly for end()
		const FunctionDeclarationNode& end_func_decl = end_func->function_decl.as<FunctionDeclarationNode>();
		auto range_expr_for_end = ASTNode::emplace_node<ExpressionNode>(
			IdentifierNode(Token(Token::Type::Identifier, range_name, 0, 0, 0))
		);
		
		ChunkedVector<ASTNode> empty_args2;
		auto end_call_expr = ASTNode::emplace_node<ExpressionNode>(
			MemberFunctionCallNode(range_expr_for_end,
			                       const_cast<FunctionDeclarationNode&>(end_func_decl),
			                       std::move(empty_args2), Token())
		);
		
		auto end_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(end_decl_node, end_call_expr);
		visit(end_var_decl_node);

		// Mark loop begin for break/continue support
		LoopBeginOp loop_begin;
		loop_begin.loop_start_label = loop_start_label;
		loop_begin.loop_end_label = loop_end_label;
		loop_begin.loop_increment_label = loop_increment_label;
		ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

		// Loop start: evaluate condition (__begin != __end)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start_label}, Token()));

		// Create condition: __begin != __end
		auto begin_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto end_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(end_token));
		auto condition_expr = ASTNode::emplace_node<ExpressionNode>(
			BinaryOperatorNode(Token(Token::Type::Operator, "!="sv, 0, 0, 0), begin_ident_expr, end_ident_expr)
		);
		auto condition_operands = visitExpressionNode(condition_expr.as<ExpressionNode>());

		// Generate conditional branch
		CondBranchOp cond_branch;
		cond_branch.label_true = loop_body_label;
		cond_branch.label_false = loop_end_label;
		cond_branch.condition = toTypedValue(std::span<const IrOperand>(condition_operands.data(), condition_operands.size()));
		ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

		// Loop body label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body_label}, Token()));

		// Declare and initialize the loop variable
		// For references (T& x or const T& x), we need the iterator value directly
		// For non-references (T x), we need the dereferenced value (*__begin)
		
		// Create the loop variable declaration with initialization
		if (!loop_var_decl.is<VariableDeclarationNode>()) {
			assert(false && "loop_var_decl must be a VariableDeclarationNode");
			return;
		}
		const VariableDeclarationNode& original_var_decl = loop_var_decl.as<VariableDeclarationNode>();
		ASTNode loop_decl_node = original_var_decl.declaration_node();
		const DeclarationNode& loop_decl = loop_decl_node.as<DeclarationNode>();
		const TypeSpecifierNode& loop_type = loop_decl.type_node().as<TypeSpecifierNode>();
		
		// C++20 standard: range-for desugars to `decl = *__begin;` for BOTH
		// value and reference loop variables. The iterator is always dereferenced.
		// For struct iterators, reinterpret as pointer to element type, then dereference.
		ASTNode init_expr;
		{
			auto deref_begin_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
			auto loop_ptr_type = ASTNode::emplace_node<TypeSpecifierNode>(
				loop_type.type(), loop_type.type_index(), static_cast<int>(loop_type.size_in_bits()), Token()
			);
			// Copy existing pointer depth (e.g., for `int*& p : arr`, loop_type is int* with depth=1)
			loop_ptr_type.as<TypeSpecifierNode>().add_pointer_levels(static_cast<int>(loop_type.pointer_depth()));
			loop_ptr_type.as<TypeSpecifierNode>().add_pointer_level();
			auto cast_expr = ASTNode::emplace_node<ExpressionNode>(
				ReinterpretCastNode(loop_ptr_type, deref_begin_ident_expr, Token(Token::Type::Keyword, "reinterpret_cast"sv, 0, 0, 0))
			);
			init_expr = ASTNode::emplace_node<ExpressionNode>(
				UnaryOperatorNode(Token(Token::Type::Operator, "*"sv, 0, 0, 0), cast_expr, true)
			);
		}
		
		auto loop_var_with_init = ASTNode::emplace_node<VariableDeclarationNode>(loop_decl_node, init_expr);

		// Generate IR for loop variable declaration
		visit(loop_var_with_init);

		// Visit loop body - use visit() to properly handle block scopes
		auto body_stmt = node.get_body_statement();
		visit(body_stmt);

		// Loop increment label (for continue statements)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_increment_label}, Token()));

		// Increment iterator: ++__begin
		auto increment_begin = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto increment_expr = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "++"sv, 0, 0, 0), increment_begin, true)
		);
		visitExpressionNode(increment_expr.as<ExpressionNode>());

		// Branch back to loop start
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start_label}, Token()));

		// Loop end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitBreakStatementNode(const BreakStatementNode& node) {
		// Generate Break IR instruction (no operands - uses loop context stack in IRConverter)
		ir_.addInstruction(IrOpcode::Break, {}, node.break_token());
	}

	void visitContinueStatementNode(const ContinueStatementNode& node) {
		// Generate Continue IR instruction (no operands - uses loop context stack in IRConverter)
		ir_.addInstruction(IrOpcode::Continue, {}, node.continue_token());
	}

	void visitGotoStatementNode(const GotoStatementNode& node) {
		// Generate Branch IR instruction (unconditional jump) with the target label name
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(node.label_name())}, node.goto_token()));
	}

	void visitLabelStatementNode(const LabelStatementNode& node) {
		// Generate Label IR instruction with the label name
		std::string label_name(node.label_name());
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(label_name)}, node.label_token()));
	}

	void visitTryStatementNode(const TryStatementNode& node) {
		// Generate try-catch-finally structure
		// For now, we'll generate a simplified version that doesn't actually implement exception handling
		// but allows the code to compile and run

		// Generate unique labels for exception handling using StringBuilder
		static size_t try_counter = 0;
		size_t current_try_id = try_counter++;
		
		// Create and commit each label separately to avoid StringBuilder overlap
		StringBuilder handlers_sb;
		handlers_sb.append("__try_handlers_").append(current_try_id);
		std::string_view handlers_label = handlers_sb.commit();
		
		StringBuilder end_sb;
		end_sb.append("__try_end_").append(current_try_id);
		std::string_view end_label = end_sb.commit();

		// Emit TryBegin marker
		ir_.addInstruction(IrInstruction(IrOpcode::TryBegin, BranchOp{.target_label = StringTable::getOrInternStringHandle(handlers_label)}, node.try_token()));

		// Visit try block
		visit(node.try_block());

		// Emit TryEnd marker
		ir_.addInstruction(IrOpcode::TryEnd, {}, node.try_token());

		// Jump to end after successful try block execution
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(end_label)}, node.try_token()));

		// Emit label for exception handlers
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(handlers_label)}, node.try_token()));

		// Visit catch clauses
		for (size_t catch_index = 0; catch_index < node.catch_clauses().size(); ++catch_index) {
			const auto& catch_clause_node = node.catch_clauses()[catch_index];
			const auto& catch_clause = catch_clause_node.as<CatchClauseNode>();
			
			// Generate unique label for this catch handler end using StringBuilder
			StringBuilder catch_end_sb;
			catch_end_sb.append("__catch_end_").append(current_try_id).append("_").append(catch_index);
			std::string_view catch_end_label = catch_end_sb.commit();

			// If this is a typed catch (not catch(...))
			if (!catch_clause.is_catch_all()) {
				const auto& exception_decl = *catch_clause.exception_declaration();
				const auto& decl = exception_decl.as<DeclarationNode>();
				const auto& type_node = decl.type_node().as<TypeSpecifierNode>();

				// Get type information
				TypeIndex type_index = type_node.type_index();
				
				// Allocate a temporary for the caught exception
				TempVar exception_temp = var_counter.next();
				
				// Emit CatchBegin marker with exception type and qualifiers
				CatchBeginOp catch_op;
				catch_op.exception_temp = exception_temp;
				catch_op.type_index = type_index;
				catch_op.exception_type = type_node.type();  // Store the Type enum for built-in types
				catch_op.catch_end_label = catch_end_label;
				catch_op.is_const = type_node.is_const();
				catch_op.is_reference = type_node.is_lvalue_reference();
				catch_op.is_rvalue_reference = type_node.is_rvalue_reference();
				catch_op.is_catch_all = false;  // This is a typed catch, not catch(...)
				ir_.addInstruction(IrInstruction(IrOpcode::CatchBegin, std::move(catch_op), catch_clause.catch_token()));

				// Add the exception variable to the symbol table for the catch block scope
				symbol_table.enter_scope(ScopeType::Block);
				
				// Register the exception parameter in the symbol table
				std::string_view exception_var_name = decl.identifier_token().value();
				if (!exception_var_name.empty()) {
					// Create a variable declaration for the exception parameter
					VariableDeclOp decl_op;
					decl_op.type = type_node.type();
					decl_op.size_in_bits = static_cast<int>(type_node.size_in_bits());
					decl_op.var_name = StringTable::getOrInternStringHandle(exception_var_name);
					
					// Create a TypedValue for the initializer
					TypedValue init_value;
					init_value.type = type_node.type();
					init_value.size_in_bits = static_cast<int>(type_node.size_in_bits());
					init_value.value = exception_temp;
					if (type_node.is_rvalue_reference()) {
						init_value.ref_qualifier = ReferenceQualifier::RValueReference;
					} else if (type_node.is_reference()) {
						init_value.ref_qualifier = ReferenceQualifier::LValueReference;
					}
					decl_op.initializer = init_value;
					
					decl_op.is_reference = type_node.is_reference();
					decl_op.is_rvalue_reference = type_node.is_rvalue_reference();
					decl_op.is_array = false;
					decl_op.custom_alignment = 0;
					
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), decl.identifier_token()));
					
					// Add to symbol table
					symbol_table.insert(exception_var_name, exception_decl);
				}
			} else {
				// catch(...) - catches all exceptions
				CatchBeginOp catch_op;
				catch_op.exception_temp = TempVar(0);
				catch_op.type_index = TypeIndex(0);
				catch_op.exception_type = Type::Void;  // No specific type for catch(...)
				catch_op.catch_end_label = catch_end_label;
				catch_op.is_const = false;
				catch_op.is_reference = false;
				catch_op.is_rvalue_reference = false;
				catch_op.is_catch_all = true;  // This IS catch(...)
				ir_.addInstruction(IrOpcode::CatchBegin, std::move(catch_op), catch_clause.catch_token());
				symbol_table.enter_scope(ScopeType::Block);
			}

			// Visit catch block body
			visit(catch_clause.body());

			// Emit CatchEnd marker
			ir_.addInstruction(IrOpcode::CatchEnd, {}, catch_clause.catch_token());

			// Exit catch block scope
			symbol_table.exit_scope();

			// Jump to end after catch block
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(end_label)}, catch_clause.catch_token()));

			// Emit catch end label
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(catch_end_label)}, catch_clause.catch_token()));
		}

		// Emit end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(end_label)}, node.try_token()));
	}

	void visitThrowStatementNode(const ThrowStatementNode& node) {
		if (node.is_rethrow()) {
			// throw; (rethrow current exception)
			ir_.addInstruction(IrOpcode::Rethrow, {}, node.throw_token());
		} else {
			// throw expression;
			const auto& expr = *node.expression();
			
			// Generate code for the expression to throw
			auto expr_operands = visitExpressionNode(expr.as<ExpressionNode>());
			
			// Extract type information from the operands
			// operands format: [type, size, value_or_temp_var] - always 3 elements
			if (expr_operands.size() < 3) {
				FLASH_LOG(Codegen, Error, "Invalid expression operands for throw statement");
				return;
			}
			
			Type expr_type = std::get<Type>(expr_operands[0]);
			size_t type_size = std::get<int>(expr_operands[1]);
			
			// Extract TypeIndex from expression operands (now at position 3 since all operands have 4 elements)
			TypeIndex exception_type_index = 0;
			if (expr_operands.size() >= 4 && std::holds_alternative<unsigned long long>(expr_operands[3])) {
				exception_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(expr_operands[3]));
			}
			
			// Create ThrowOp with typed data
			ThrowOp throw_op;
			throw_op.type_index = exception_type_index;
			throw_op.exception_type = expr_type;  // Store the actual Type enum
			throw_op.size_in_bytes = type_size / 8;  // Convert bits to bytes
			throw_op.is_rvalue = true;  // Default to rvalue for now
			
			// Handle the value - it can be a TempVar, immediate int, or immediate float
			// All these types are compatible with IrValue variant
			if (std::holds_alternative<TempVar>(expr_operands[2])) {
				throw_op.exception_value = std::get<TempVar>(expr_operands[2]);
			} else if (std::holds_alternative<unsigned long long>(expr_operands[2])) {
				throw_op.exception_value = std::get<unsigned long long>(expr_operands[2]);
			} else if (std::holds_alternative<double>(expr_operands[2])) {
				throw_op.exception_value = std::get<double>(expr_operands[2]);
			} else {
				// Unknown operand type - log warning and default to zero value
				FLASH_LOG(Codegen, Warning, "Unknown operand type in throw expression, defaulting to zero");
				throw_op.exception_value = static_cast<unsigned long long>(0);
			}
			
			ir_.addInstruction(IrInstruction(IrOpcode::Throw, std::move(throw_op), node.throw_token()));
		}
	}

	void visitVariableDeclarationNode(const ASTNode& ast_node) {
		const VariableDeclarationNode& node = ast_node.as<VariableDeclarationNode>();
		const auto& decl = node.declaration();
		const auto& type_node = decl.type_node().as<TypeSpecifierNode>();

		// Check if this is a global variable (declared at global scope)
		bool is_global = (symbol_table.get_current_scope_type() == ScopeType::Global);

		// Check if this is a static local variable
		bool is_static_local = (node.storage_class() == StorageClass::Static && !is_global);

		if (is_global || is_static_local) {
			// Handle global variable or static local variable
			// For static locals, mangle the name to include the function name
			// Use StringBuilder to create a persistent string_view
			// (string_view in GlobalVariableDeclOp would dangle if we used local std::string)
			StringBuilder sb;
			if (is_static_local) {
				// Mangle name as: function_name.variable_name
				sb.append(current_function_name_).append(".").append(decl.identifier_token().value());
			} else {
				// For global variables, include namespace path for proper mangling
				if (!current_namespace_stack_.empty()) {
					// Check if we're in an anonymous namespace
					bool in_anonymous_ns = false;
					for (const auto& ns : current_namespace_stack_) {
						if (ns.empty()) {
							in_anonymous_ns = true;
							break;
						}
					}
					
					// For variables in anonymous namespaces with Itanium mangling,
					// we need to generate a unique mangled name
					if (in_anonymous_ns && NameMangling::g_mangling_style == NameMangling::ManglingStyle::Itanium) {
						// Generate proper Itanium mangling for anonymous namespace variable
						sb.append("_ZN");  // Start nested name
						for (const auto& ns : current_namespace_stack_) {
							if (ns.empty()) {
								// Anonymous namespace: use _GLOBAL__N_1
								sb.append("12_GLOBAL__N_1");
							} else {
								sb.append(std::to_string(ns.size())).append(ns);
							}
						}
						// Add variable name
						std::string_view var_id = decl.identifier_token().value();
						sb.append(std::to_string(var_id.size())).append(var_id);
						sb.append("E");  // End nested name
					} else {
						// For MSVC or named namespaces, use namespace::variable format
						for (const auto& ns : current_namespace_stack_) {
							sb.append(ns).append("::");
						}
						sb.append(decl.identifier_token().value());
					}
				} else {
					sb.append(decl.identifier_token().value());
				}
			}
			std::string_view var_name_view = sb.commit();
			// Phase 3: Intern the string using StringTable
			StringHandle var_name = StringTable::getOrInternStringHandle(var_name_view);

			// Store mapping from simple name to mangled name for later lookups
			// This is needed for anonymous namespace variables
			// Phase 4: Using StringHandle for both key and value
			StringHandle simple_name_handle = decl.identifier_token().handle();
			if (var_name_view != decl.identifier_token().value()) {
				global_variable_names_[simple_name_handle] = var_name;
			}

			// Create GlobalVariableDeclOp
			GlobalVariableDeclOp op;
			op.type = type_node.type();
			op.size_in_bits = static_cast<int>(type_node.size_in_bits());
			op.var_name = var_name;  // Phase 3: Now using StringHandle
			op.element_count = 1;  // Default for scalars
			
			// Helper to append a value as raw bytes in little-endian format
			auto appendValueAsBytes = [](std::vector<char>& data, unsigned long long value, size_t byte_count) {
				for (size_t i = 0; i < byte_count; ++i) {
					data.push_back(static_cast<char>((value >> (i * 8)) & 0xFF));
				}
			};
			
			// Helper to evaluate a constexpr and get the raw value
			auto evalToValue = [&](const ASTNode& expr, Type target_type) -> unsigned long long {
				ConstExpr::EvaluationContext ctx(gSymbolTable);
				auto eval_result = ConstExpr::Evaluator::evaluate(expr, ctx);
				
				if (!eval_result.success()) {
					FLASH_LOG(Codegen, Warning, "Non-constant initializer in global variable '", 
					          decl.identifier_token().value(), "' at line ", decl.identifier_token().line());
					return 0;
				}
				
				if (target_type == Type::Float) {
					float f = static_cast<float>(eval_result.as_double());
					uint32_t f_bits;
					std::memcpy(&f_bits, &f, sizeof(float));
					return f_bits;
				} else if (target_type == Type::Double || target_type == Type::LongDouble) {
					double d = eval_result.as_double();
					unsigned long long bits;
					std::memcpy(&bits, &d, sizeof(double));
					return bits;
				} else if (std::holds_alternative<double>(eval_result.value)) {
					return static_cast<unsigned long long>(eval_result.as_int());
				} else if (std::holds_alternative<unsigned long long>(eval_result.value)) {
					return std::get<unsigned long long>(eval_result.value);
				} else if (std::holds_alternative<long long>(eval_result.value)) {
					return static_cast<unsigned long long>(std::get<long long>(eval_result.value));
				} else if (std::holds_alternative<bool>(eval_result.value)) {
					return std::get<bool>(eval_result.value) ? 1 : 0;
				}
				return 0;
			};
			
			// Check if this is an array and get element count (product of all dimensions for multidimensional)
			if (decl.is_array() || type_node.is_array()) {
				const auto& dims = decl.array_dimensions();
				if (!dims.empty()) {
					// Calculate total element count as product of all dimensions
					op.element_count = 1;
					for (const auto& dim_expr : dims) {
						ConstExpr::EvaluationContext ctx(gSymbolTable);
						auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
						if (eval_result.success() && eval_result.as_int() > 0) {
							op.element_count *= static_cast<size_t>(eval_result.as_int());
						}
					}
				} else if (type_node.array_size().has_value()) {
					op.element_count = *type_node.array_size();
				}
			}

			// Check if initialized
			size_t element_size = op.size_in_bits / 8;
			if (node.initializer()) {
				const ASTNode& init_node = *node.initializer();
				
				// Handle array initialization with InitializerListNode
				if (init_node.is<InitializerListNode>()) {
					const InitializerListNode& init_list = init_node.as<InitializerListNode>();
					const auto& initializers = init_list.initializers();
					
					op.is_initialized = true;
					op.element_count = initializers.size();
					
					// Build raw bytes for each element
					for (const auto& elem_init : initializers) {
						unsigned long long value = evalToValue(elem_init, type_node.type());
						appendValueAsBytes(op.init_data, value, element_size);
					}
				} else if (init_node.is<ExpressionNode>()) {
					// Single value initialization
					unsigned long long value = evalToValue(init_node, type_node.type());
					op.is_initialized = true;
					appendValueAsBytes(op.init_data, value, element_size);
				} else {
					op.is_initialized = false;
				}
			} else {
				// No explicit initializer provided
				// Check if this is a struct with default member initializers
				if (type_node.type_index() != 0) {
					// This is a user-defined type (struct/class)
					const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
					const StructTypeInfo* struct_info = type_info.getStructInfo();
					if (struct_info && !struct_info->members.empty()) {
						// Check if any members have default initializers
						bool has_default_inits = false;
						for (const auto& member : struct_info->members) {
							if (member.default_initializer.has_value()) {
								has_default_inits = true;
								break;
							}
						}
						
						if (has_default_inits) {
							// Build initial data from default member initializers
							op.is_initialized = true;
							op.init_data.resize(struct_info->total_size, 0);  // Start with zeros
							
							for (const auto& member : struct_info->members) {
								if (member.default_initializer.has_value()) {
									// Evaluate the default initializer
									unsigned long long value = evalToValue(*member.default_initializer, member.type);
									
									// Write the value at the member's offset
									size_t member_size = member.size;
									for (size_t i = 0; i < member_size && (member.offset + i) < op.init_data.size(); ++i) {
										op.init_data[member.offset + i] = static_cast<char>((value >> (i * 8)) & 0xFF);
									}
								}
							}
						} else {
							op.is_initialized = false;
						}
					} else {
						op.is_initialized = false;
					}
				} else {
					op.is_initialized = false;
				}
			}

			ir_.addInstruction(IrInstruction(IrOpcode::GlobalVariableDecl, std::move(op), decl.identifier_token()));
			// (The parser already added it to the symbol table)
			if (is_static_local) {
				StaticLocalInfo info;
				info.mangled_name = var_name;  // Phase 4: Using StringHandle directly
				info.type = type_node.type();
				info.size_in_bits = static_cast<int>(type_node.size_in_bits());
				// Phase 4: Using StringHandle for key
				StringHandle key = decl.identifier_token().handle();
				static_local_names_[key] = info;
			}

			return;
		}

		// Handle constexpr variables with function call initializers
		// For constexpr, we try to evaluate at compile-time
		if (node.is_constexpr() && node.initializer().has_value()) {
			const ASTNode& init_node = *node.initializer();
			
			// Check if initializer is a function call (including callable object invocation)
			// Lambda calls come through as MemberFunctionCallNode (operator() calls)
			bool is_function_call = false;
			if (init_node.is<ExpressionNode>()) {
				const ExpressionNode& expr = init_node.as<ExpressionNode>();
				is_function_call = std::holds_alternative<FunctionCallNode>(expr) ||
				                   std::holds_alternative<MemberFunctionCallNode>(expr);
			}
			
			if (is_function_call) {
				// Try to evaluate the function call at compile time
				ConstExpr::EvaluationContext ctx(symbol_table);
				auto eval_result = ConstExpr::Evaluator::evaluate(init_node, ctx);
				
				if (eval_result.success()) {
					// Insert into symbol table first
					if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
						assert(false && "Expected identifier to be unique");
					}
					
					// Generate variable declaration with compile-time value
					VariableDeclOp decl_op;
					decl_op.type = type_node.type();
					decl_op.size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
					decl_op.var_name = decl.identifier_token().handle();
					decl_op.custom_alignment = static_cast<unsigned long long>(decl.custom_alignment());
					decl_op.is_reference = type_node.is_reference();
					decl_op.is_rvalue_reference = type_node.is_rvalue_reference();
					decl_op.is_array = false;
					
					// Set the compile-time evaluated initializer
					if (std::holds_alternative<long long>(eval_result.value)) {
						decl_op.initializer = TypedValue{type_node.type(), decl_op.size_in_bits, 
							static_cast<unsigned long long>(std::get<long long>(eval_result.value))};
					} else if (std::holds_alternative<unsigned long long>(eval_result.value)) {
						decl_op.initializer = TypedValue{type_node.type(), decl_op.size_in_bits, 
							std::get<unsigned long long>(eval_result.value)};
					} else if (std::holds_alternative<double>(eval_result.value)) {
						double d = std::get<double>(eval_result.value);
						if (type_node.type() == Type::Float) {
							float f = static_cast<float>(d);
							uint32_t bits;
							std::memcpy(&bits, &f, sizeof(float));
							decl_op.initializer = TypedValue{Type::Float, 32, static_cast<unsigned long long>(bits)};
						} else {
							unsigned long long bits;
							std::memcpy(&bits, &d, sizeof(double));
							decl_op.initializer = TypedValue{Type::Double, 64, bits};
						}
					}
					
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), node.declaration().identifier_token()));
					return;  // Done - constexpr variable initialized at compile time
				}
				// If evaluation failed, fall through to runtime evaluation
				// This is allowed - the variable just won't be usable in other constexpr contexts
			}
		}

		// Handle local variable
		// Create variable declaration operands
		// Format: [type, size_in_bits, var_name, custom_alignment, is_ref, is_rvalue_ref, is_array, ...]
		std::vector<IrOperand> operands;
		operands.emplace_back(type_node.type());
		// For pointers, allocate 64 bits (pointer size on x64), not the pointed-to type size
		int size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
		operands.emplace_back(size_in_bits);
		operands.emplace_back(decl.identifier_token().handle());
		operands.emplace_back(static_cast<unsigned long long>(decl.custom_alignment()));
		operands.emplace_back(type_node.is_reference());
		operands.emplace_back(type_node.is_rvalue_reference());
		operands.emplace_back(decl.is_array());  // Add is_array flag

		// For arrays, calculate total element count (product of all dimensions for multidimensional arrays)
		size_t array_count = 0;
		if (decl.is_array()) {
			const auto& dims = decl.array_dimensions();
			if (!dims.empty()) {
				// Calculate total element count as product of all dimensions
				array_count = 1;
				for (const auto& dim_expr : dims) {
					ConstExpr::EvaluationContext ctx(symbol_table);
					auto eval_result = ConstExpr::Evaluator::evaluate(dim_expr, ctx);
					
					if (eval_result.success()) {
						long long dim_size = eval_result.as_int();
						if (dim_size > 0) {
							array_count *= static_cast<size_t>(dim_size);
						} else {
							array_count = 0;
							break;
						}
					} else {
						array_count = 0;
						break;
					}
				}
				
				// Add element type, size, and count as operands
				operands.emplace_back(type_node.type());  // element type
				operands.emplace_back(size_in_bits);      // element size
				operands.emplace_back(static_cast<unsigned long long>(array_count));
			} else if (decl.is_unsized_array() && node.initializer().has_value()) {
				// Unsized array - get size from initializer list
				const ASTNode& init_node = *node.initializer();
				if (init_node.is<InitializerListNode>()) {
					const InitializerListNode& init_list = init_node.as<InitializerListNode>();
					array_count = init_list.initializers().size();
					// Add the inferred size as an operand
					operands.emplace_back(type_node.type());  // element type
					operands.emplace_back(size_in_bits);      // element size
					operands.emplace_back(static_cast<unsigned long long>(array_count));
				}
			}
		}

		// Add initializer if present (for non-arrays)
		if (node.initializer() && !decl.is_array()) {
			const ASTNode& init_node = *node.initializer();

				// Check if this is a brace initializer (InitializerListNode)
			if (init_node.is<InitializerListNode>()) {
				const InitializerListNode& init_list = init_node.as<InitializerListNode>();
				
				// For scalar types with direct initialization like int v(10), 
				// the InitializerListNode will have a single element. Handle this case.
				if (type_node.type() != Type::Struct && init_list.initializers().size() == 1) {
					// Direct initialization of scalar type: int v(10) or int v{10}
					// Extract the single initializer and treat it as a regular expression initializer
					const ASTNode& single_init = init_list.initializers()[0];
					
					// Visit the initializer expression to get its IR
					auto init_operands = visitExpressionNode(single_init.as<ExpressionNode>());
					
					// Append the initializer operands
					operands.insert(operands.end(), init_operands.begin(), init_operands.end());
					
					// Add to symbol table
					if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
						assert(false && "Expected identifier to be unique");
					}

					// Generate VariableDecl with initializer
					VariableDeclOp decl_op;
					decl_op.type = type_node.type();
					decl_op.size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
					decl_op.var_name = decl.identifier_token().handle();
					decl_op.custom_alignment = static_cast<unsigned long long>(decl.custom_alignment());
					decl_op.is_reference = type_node.is_reference();
					decl_op.is_rvalue_reference = type_node.is_rvalue_reference();
					decl_op.is_array = decl.is_array();
					if (operands.size() >= 10) {
						TypedValue tv = toTypedValue(std::span<const IrOperand>(&operands[7], 3));
						decl_op.initializer = std::move(tv);
					}
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), node.declaration().identifier_token()));
					return;  // Done with scalar direct initialization
				} else {
					// Handle brace initialization for structs or multi-element initializers
					
					// Add to symbol table first
					if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
						assert(false && "Expected identifier to be unique");
					}

					// Add the variable declaration without initializer
					VariableDeclOp decl_op;
					decl_op.type = type_node.type();
					decl_op.size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
					decl_op.var_name = decl.identifier_token().handle();
					decl_op.custom_alignment = static_cast<unsigned long long>(decl.custom_alignment());
					decl_op.is_reference = type_node.is_reference();
					decl_op.is_rvalue_reference = type_node.is_rvalue_reference();
					decl_op.is_array = decl.is_array();
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), node.declaration().identifier_token()));

					// Check if this struct has a constructor
					if (type_node.type() == Type::Struct) {
						TypeIndex type_index = type_node.type_index();
						if (type_index < gTypeInfo.size()) {
							const TypeInfo& type_info = gTypeInfo[type_index];
							if (type_info.struct_info_) {
								const StructTypeInfo& struct_info = *type_info.struct_info_;

								// Check if this is an abstract class (only for non-pointer types)
								if (struct_info.is_abstract && type_node.pointer_levels().empty()) {
									FLASH_LOG(Codegen, Error, "Cannot instantiate abstract class '", type_info.name(), "'");
									assert(false && "Cannot instantiate abstract class");
								}

								const auto& initializers = init_list.initializers();

								// Check if this is a designated initializer list or aggregate initialization
								// Designated initializers always use direct member initialization
								bool use_direct_member_init = init_list.has_any_designated();
								
								// Check if there's a constructor that matches the number of initializers
								// For aggregate initialization Point{1, 2}, we need a constructor with 2 parameters
								// If no matching constructor exists, use direct member initialization
								// Also consider constructors with default arguments
								bool has_matching_constructor = false;
								const ConstructorDeclarationNode* matching_ctor = nullptr;
								size_t num_initializers = initializers.size();

								// Special case: if empty initializer list and struct needs a trivial default constructor
								// This handles template specializations where the constructor is generated later
								if (!use_direct_member_init && num_initializers == 0 &&
								    !struct_info.hasAnyConstructor() && struct_info.needs_default_constructor &&
								    !struct_info.isDefaultConstructorDeleted()) {
									has_matching_constructor = true;
									matching_ctor = nullptr;
								}

								if (!has_matching_constructor && !use_direct_member_init && struct_info.hasAnyConstructor()) {
								
								// FIRST: Try to find copy constructor if we have exactly one initializer of the same struct type
								// This ensures copy constructors are preferred over converting constructors
								if (num_initializers == 1) {
									const ASTNode& init_expr = initializers[0];
									if (init_expr.is<ExpressionNode>()) {
										const auto& expr = init_expr.as<ExpressionNode>();
										if (std::holds_alternative<IdentifierNode>(expr)) {
											const auto& ident = std::get<IdentifierNode>(expr);
											std::optional<ASTNode> init_symbol = symbol_table.lookup(ident.name());
											if (init_symbol.has_value()) {
												if (const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol)) {
													const TypeSpecifierNode& init_type = init_decl->type_node().as<TypeSpecifierNode>();
													// Check if initializer is of the same struct type
													if (init_type.type() == Type::Struct && 
														init_type.type_index() == type_index) {
														// Try to find copy constructor
														const StructMemberFunction* copy_ctor = struct_info.findCopyConstructor();
														if (copy_ctor && copy_ctor->function_decl.is<ConstructorDeclarationNode>()) {
															has_matching_constructor = true;
															matching_ctor = &copy_ctor->function_decl.as<ConstructorDeclarationNode>();
															FLASH_LOG(Codegen, Debug, "Matched copy constructor for ", struct_info.name);
														}
													}
												}
											}
										}
									}
								}
								
								// SECOND: If no copy constructor matched, look for other constructors
								if (!has_matching_constructor) {
								for (const auto& func : struct_info.member_functions) {
									if (func.is_constructor) {
										// Get parameter count from the function declaration
										if (func.function_decl.is<FunctionDeclarationNode>()) {
											const auto& func_decl = func.function_decl.as<FunctionDeclarationNode>();
											size_t param_count = func_decl.parameter_nodes().size();
											if (param_count == num_initializers) {
												has_matching_constructor = true;
												break;
											}
										} else if (func.function_decl.is<ConstructorDeclarationNode>()) {
											const auto& ctor_decl = func.function_decl.as<ConstructorDeclarationNode>();
											const auto& params = ctor_decl.parameter_nodes();
											size_t param_count = params.size();
											
											// Skip copy constructor and move constructor for brace initialization
											// Copy/move constructors should only be used when the initializer is of the same struct type
											if (param_count == 1 && params.size() == 1 && params[0].is<DeclarationNode>()) {
												const auto& param_decl = params[0].as<DeclarationNode>();
												const auto& param_type = param_decl.type_node().as<TypeSpecifierNode>();
												
												// Skip if this is a copy constructor (reference to same struct type)
												if (param_type.is_reference() && param_type.type() == Type::Struct) {
													// Check if the initializer is actually of this struct type
													bool init_is_struct_of_same_type = false;
													if (num_initializers == 1) {
														const ASTNode& init_expr = initializers[0];
														if (init_expr.is<ExpressionNode>()) {
															const auto& expr = init_expr.as<ExpressionNode>();
															// Get the type of the initializer expression
															if (std::holds_alternative<IdentifierNode>(expr)) {
																const auto& ident = std::get<IdentifierNode>(expr);
																std::optional<ASTNode> init_symbol = symbol_table.lookup(ident.name());
																if (init_symbol.has_value()) {
																	if (const DeclarationNode* init_decl = get_decl_from_symbol(*init_symbol)) {
																		const TypeSpecifierNode& init_type = init_decl->type_node().as<TypeSpecifierNode>();
																		if (init_type.type() == Type::Struct && 
																			init_type.type_index() == param_type.type_index()) {
																			init_is_struct_of_same_type = true;
																		}
																	}
																}
															}
														}
													}
													if (!init_is_struct_of_same_type) {
														// Skip copy constructor - initializer is not of the same struct type
														continue;
													}
												}
												
												// Skip if this is a move constructor (rvalue reference to same struct type)
												if (param_type.is_rvalue_reference() && param_type.type() == Type::Struct) {
													// Check if the initializer is actually of this struct type
													bool init_is_struct_of_same_type = false;
													if (num_initializers == 1) {
														const ASTNode& init_expr = initializers[0];
														if (init_expr.is<ExpressionNode>()) {
															const auto& expr = init_expr.as<ExpressionNode>();
															// For move constructor, we only use it for rvalue expressions of same type
															// For now, skip it for brace initialization unless it's explicitly a move
															if (std::holds_alternative<IdentifierNode>(expr)) {
																// Simple identifier - not an rvalue, don't match move constructor
																continue;
															}
														}
													}
													if (!init_is_struct_of_same_type) {
														// Skip move constructor for non-matching types
														continue;
													}
												}
											}
											
											// Exact match
											if (param_count == num_initializers) {
												has_matching_constructor = true;
												matching_ctor = &ctor_decl;
												break;
											}
											
											// Check if constructor has default arguments that cover the gap
											if (param_count > num_initializers) {
												// Check if parameters from num_initializers onwards all have defaults
												bool all_have_defaults = true;
												for (size_t i = num_initializers; i < param_count; ++i) {
													if (params[i].is<DeclarationNode>()) {
														if (!params[i].as<DeclarationNode>().has_default_value()) {
															all_have_defaults = false;
															break;
														}
													} else {
														all_have_defaults = false;
														break;
													}
												}
												if (all_have_defaults) {
													has_matching_constructor = true;
													matching_ctor = &ctor_decl;
													break;
												}
											}
										}
									}
								}
								}
							}

							if (has_matching_constructor) {
								// Generate constructor call with parameters from initializer list
								ConstructorCallOp ctor_op;
								ctor_op.struct_name = type_info.name();
								ctor_op.object = decl.identifier_token().handle();

								// Get constructor parameter types for reference handling
								const auto& ctor_params = matching_ctor ? matching_ctor->parameter_nodes() : std::vector<ASTNode>{};
								
								// Add each initializer as a constructor parameter
								size_t arg_index = 0;
								for (const ASTNode& init_expr : initializers) {
									if (init_expr.is<ExpressionNode>()) {
										// Get the parameter type for this argument (if it exists)
										const TypeSpecifierNode* param_type = nullptr;
										if (arg_index < ctor_params.size() && ctor_params[arg_index].is<DeclarationNode>()) {
											param_type = &ctor_params[arg_index].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
										}
										
										auto init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
										// init_operands = [type, size, value]
										if (init_operands.size() >= 3) {
											TypedValue tv;
											
											// Check if parameter expects a reference and argument is an identifier
											bool is_ident = std::holds_alternative<IdentifierNode>(init_expr.as<ExpressionNode>());
											bool param_is_ref = param_type && (param_type->is_reference() || param_type->is_rvalue_reference());
											
											if (param_is_ref && is_ident) {
												const auto& identifier = std::get<IdentifierNode>(init_expr.as<ExpressionNode>());
												std::optional<ASTNode> arg_symbol = symbol_table.lookup(identifier.name());
												
												const DeclarationNode* arg_decl = nullptr;
												if (arg_symbol.has_value() && arg_symbol->is<DeclarationNode>()) {
													arg_decl = &arg_symbol->as<DeclarationNode>();
												} else if (arg_symbol.has_value() && arg_symbol->is<VariableDeclarationNode>()) {
													arg_decl = &arg_symbol->as<VariableDeclarationNode>().declaration();
												}
												
												if (arg_decl) {
													const auto& arg_type = arg_decl->type_node().as<TypeSpecifierNode>();
													
													if (arg_type.is_reference() || arg_type.is_rvalue_reference()) {
														// Argument is already a reference - just pass it through
														tv = toTypedValue(init_operands);
													} else {
														// Argument is a value - take its address
														TempVar addr_var = var_counter.next();
														AddressOfOp addr_op;
														addr_op.result = addr_var;
														addr_op.operand.type = arg_type.type();
														addr_op.operand.size_in_bits = static_cast<int>(arg_type.size_in_bits());
														addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
														addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
														ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
														
														// Create TypedValue with the address
														tv.type = arg_type.type();
														tv.size_in_bits = 64;  // Pointer size
														tv.value = addr_var;
														tv.ref_qualifier = ReferenceQualifier::LValueReference;  // Mark as reference parameter
														tv.type_index = arg_type.type_index();  // Preserve type_index for struct references
													}
												} else {
													// Not a simple identifier or not found - use as-is
													tv = toTypedValue(init_operands);
												}
											} else {
												// Not a reference parameter or not an identifier - use as-is
												tv = toTypedValue(init_operands);
											}
											
											ctor_op.arguments.push_back(std::move(tv));
										} else {
											assert(false && "Invalid initializer operands - expected [type, size, value]");
										}
									} else {
										assert(false && "Initializer must be an ExpressionNode");
									}
									arg_index++;
								}
								
								// Fill in default arguments for missing parameters
								if (matching_ctor) {
									const auto& params = matching_ctor->parameter_nodes();
									size_t num_explicit_args = ctor_op.arguments.size();
									for (size_t i = num_explicit_args; i < params.size(); ++i) {
										if (params[i].is<DeclarationNode>()) {
											const auto& param_decl = params[i].as<DeclarationNode>();
											if (param_decl.has_default_value()) {
												const ASTNode& default_node = param_decl.default_value();
												if (default_node.is<ExpressionNode>()) {
													auto default_operands = visitExpressionNode(default_node.as<ExpressionNode>());
													if (default_operands.size() >= 3) {
														TypedValue default_arg = toTypedValue(default_operands);
														ctor_op.arguments.push_back(std::move(default_arg));
													}
												}
											}
										}
									}
								}
								
								ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
							} else {
								// No constructor - use direct member initialization
								// But first check if default constructor is deleted
								if (num_initializers == 0 && struct_info.isDefaultConstructorDeleted()) {
									std::string_view error_msg = StringBuilder().append("Cannot default-initialize struct ")
										.append(StringTable::getStringView(struct_info.name))
										.append(" - default constructor is deleted").commit();
									throw std::runtime_error(std::string(error_msg));
								}

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
										if (positional_index < struct_info.members.size()) {
											StringHandle member_name = struct_info.members[positional_index].getName();
											member_values[member_name] = &initializers[i];
											positional_index++;
										}
									}
								}

								// Generate member stores for each struct member
								for (const StructMember& member : struct_info.members) {
									// Determine the initial value
									IrValue member_value;
									// Check if this member has an initializer
									StringHandle member_name_handle = member.getName();
									if (member_values.count(member_name_handle)) {
										const ASTNode& init_expr = *member_values[member_name_handle];
										
										// Check if this is a nested braced initializer (InitializerListNode)
										if (init_expr.is<InitializerListNode>()) {
											// Nested braced initializer - handle it recursively using the helper function
											const InitializerListNode& nested_init_list = init_expr.as<InitializerListNode>();
											
											// Get the type info for the nested member
											TypeIndex nested_member_type_index = member.type_index;
											if (nested_member_type_index < gTypeInfo.size()) {
												const TypeInfo& nested_member_type_info = gTypeInfo[nested_member_type_index];
												
												// If this is a struct type, use the recursive helper
												if (nested_member_type_info.struct_info_ && !nested_member_type_info.struct_info_->members.empty()) {
													generateNestedMemberStores(
													    *nested_member_type_info.struct_info_,
													    nested_init_list,
													    decl.identifier_token().handle(),
													    static_cast<int>(member.offset),
													    decl.identifier_token()
													);
													continue;  // Skip the outer member store
												}
											}
										}

										std::vector<IrOperand> init_operands;
										if (init_expr.is<ExpressionNode>()) {
											init_operands = visitExpressionNode(init_expr.as<ExpressionNode>());
										} else {
											assert(false && "Initializer must be an ExpressionNode or InitializerListNode");
										}

										if (init_operands.size() >= 3) {
											// Extract value from init_operands[2]
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
											assert(false && "Invalid initializer operands");
										}
									} else {
										// Use default member initializer if available, otherwise zero-initialize
										if (member.default_initializer.has_value()) {
											ConstExpr::EvaluationContext ctx(gSymbolTable);
											auto eval_result = ConstExpr::Evaluator::evaluate(*member.default_initializer, ctx);
											if (eval_result.success()) {
												if (std::holds_alternative<unsigned long long>(eval_result.value)) {
													member_value = std::get<unsigned long long>(eval_result.value);
												} else if (std::holds_alternative<long long>(eval_result.value)) {
													member_value = static_cast<unsigned long long>(std::get<long long>(eval_result.value));
												} else if (std::holds_alternative<bool>(eval_result.value)) {
													member_value = std::get<bool>(eval_result.value) ? 1ULL : 0ULL;
												} else if (std::holds_alternative<double>(eval_result.value)) {
													member_value = std::get<double>(eval_result.value);
												} else {
													member_value = 0ULL;
												}
											} else {
												member_value = 0ULL;
											}
										} else {
											member_value = 0ULL;
										}
									}

									MemberStoreOp member_store;
									member_store.value.type = member.type;
									member_store.value.size_in_bits = static_cast<int>(member.size * 8);
									member_store.value.value = member_value;
									member_store.object = decl.identifier_token().handle();
									member_store.member_name = member.getName();
									member_store.offset = static_cast<int>(member.offset);
									member_store.is_reference = member.is_reference;
									member_store.is_rvalue_reference = member.is_rvalue_reference;
									member_store.struct_type_info = nullptr;

									ir_.addInstruction(IrInstruction(IrOpcode::MemberStore, std::move(member_store), decl.identifier_token()));
								}
							}

							// Register for destructor if needed
							if (struct_info.hasDestructor()) {
								registerVariableWithDestructor(
									std::string(decl.identifier_token().value()),
									std::string(StringTable::getStringView(type_info.name()))
								);
							}
						}
					}
					}  // end if (type_node.type() == Type::Struct)
				}  // end else (struct initialization)
				return; // Early return - we've already added the variable declaration
			} else if (init_node.is<LambdaExpressionNode>()) {
				// Lambda expression initializer (direct)
				const auto& lambda = init_node.as<LambdaExpressionNode>();
				// Pass the target variable name so captures are stored in the right variable
				std::string_view var_name = decl.identifier_token().value();
				generateLambdaExpressionIr(lambda, var_name);
				
				// Check if target type is a function pointer - if so, store __invoke address
				if (type_node.is_function_pointer() && lambda.captures().empty()) {
					TempVar func_addr_var = generateLambdaInvokeFunctionAddress(lambda);
					operands.emplace_back(Type::FunctionPointer);
					operands.emplace_back(64);
					operands.emplace_back(func_addr_var);
				}
				// Lambda expression already emitted VariableDecl, so return early
				if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
					assert(false && "Expected identifier to be unique");
				}
				return;
			} else if (init_node.is<ExpressionNode>() && 
			           std::holds_alternative<LambdaExpressionNode>(init_node.as<ExpressionNode>())) {
				// Lambda expression wrapped in ExpressionNode
				const auto& lambda = std::get<LambdaExpressionNode>(init_node.as<ExpressionNode>());
				// Pass the target variable name so captures are stored in the right variable
				std::string_view var_name = decl.identifier_token().value();
				generateLambdaExpressionIr(lambda, var_name);
				
				// Check if target type is a function pointer - if so, store __invoke address
				if (type_node.is_function_pointer() && lambda.captures().empty()) {
					TempVar func_addr_var = generateLambdaInvokeFunctionAddress(lambda);
					operands.emplace_back(Type::FunctionPointer);
					operands.emplace_back(64);
					operands.emplace_back(func_addr_var);
				}
				// Lambda expression already emitted VariableDecl, so return early
				if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
					assert(false && "Expected identifier to be unique");
				}
				return;
			} else {
				// Regular expression initializer
				// For struct types with copy constructors, check if it's an rvalue (function return)
				// before deciding whether to use constructor call or direct initialization
				// However, if the struct doesn't have a constructor, we need to evaluate the expression
				// IMPORTANT: Pointer types (Base* pb = &b) should process initializer normally
				bool is_struct_with_constructor = false;
				if (type_node.type() == Type::Struct && type_node.pointer_depth() == 0 && type_node.type_index() < gTypeInfo.size()) {
					const TypeInfo& type_info = gTypeInfo[type_node.type_index()];
					if (type_info.struct_info_ && type_info.struct_info_->hasConstructor()) {
						is_struct_with_constructor = true;
					}
				}
				
				// References don't use copy constructors - they bind to the address of the initializer
				bool is_copy_init_for_struct = (type_node.type() == Type::Struct &&
				                                 type_node.pointer_depth() == 0 &&
				                                 !type_node.is_reference() &&
				                                 !type_node.is_rvalue_reference() &&
				                                 node.initializer() &&
				                                 init_node.is<ExpressionNode>() &&
				                                 !init_node.is<InitializerListNode>() &&
				                                 is_struct_with_constructor);


				if (!is_copy_init_for_struct) {
					// For reference types, use LValueAddress context to get the address of the initializer
					ExpressionContext ref_context = (type_node.is_reference() || type_node.is_rvalue_reference())
						? ExpressionContext::LValueAddress
						: ExpressionContext::Load;
					auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>(), ref_context);
					
					// Check if we need implicit conversion via conversion operator
					// This handles cases like: int i = myStruct; where myStruct has operator int()
					if (init_operands.size() >= 3) {
						Type init_type = std::get<Type>(init_operands[0]);
						int init_size = std::get<int>(init_operands[1]);
						TypeIndex init_type_index = 0;  // Will be set below if type_index is available
						
						// Extract type_index if available (4th element in init_operands)
						if (init_operands.size() >= 4 && std::holds_alternative<unsigned long long>(init_operands[3])) {
							init_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(init_operands[3]));
						}
						
						// Check if source and target types differ and source is a struct
						bool need_conversion = (init_type != type_node.type()) || 
						                       (init_type == Type::Struct && init_type_index != type_node.type_index());
						
						if (need_conversion && init_type == Type::Struct && init_type_index < gTypeInfo.size()) {
							const TypeInfo& source_type_info = gTypeInfo[init_type_index];
							const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
							
							// Look for a conversion operator to the target type
							const StructMemberFunction* conv_op = findConversionOperator(
								source_struct_info, type_node.type(), type_node.type_index());
							
							if (conv_op) {
								FLASH_LOG(Codegen, Debug, "Found conversion operator from ", 
									StringTable::getStringView(source_type_info.name()), 
									" to target type");
								
								// Generate call to the conversion operator
								// The conversion operator is a const member function taking no parameters
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
								}, init_operands[2]);
								
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
									call_op.return_type = type_node.type();
									call_op.return_size_in_bits = type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits());
									call_op.return_type_index = type_node.type_index();
									call_op.is_member_function = true;
									call_op.is_variadic = false;
									
									// For member function calls, first argument is 'this' pointer
									// We need to pass the address of the source object
									if (std::holds_alternative<StringHandle>(source_value)) {
										// It's a variable - take its address
										TempVar this_ptr = var_counter.next();
										AddressOfOp addr_op;
										addr_op.result = this_ptr;
										addr_op.operand.type = init_type;
										addr_op.operand.size_in_bits = init_size;
										addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
										addr_op.operand.value = std::get<StringHandle>(source_value);
										ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
										
										// Add 'this' as first argument
										TypedValue this_arg;
										this_arg.type = init_type;
										this_arg.size_in_bits = 64;  // Pointer size
										this_arg.value = this_ptr;
										this_arg.type_index = init_type_index;
										call_op.args.push_back(std::move(this_arg));
									} else if (std::holds_alternative<TempVar>(source_value)) {
										// It's already a temporary - it might be an address or value
										// For conversion operators, we need the address
										// ASSUMPTION: For struct types, TempVars at this point in variable initialization
										// represent the address of the object (not the object value itself).
										// This is because visitExpressionNode returns addresses for struct identifiers.
										TypedValue this_arg;
										this_arg.type = init_type;
										this_arg.size_in_bits = 64;  // Pointer size for 'this'
										this_arg.value = std::get<TempVar>(source_value);
										this_arg.type_index = init_type_index;
										call_op.args.push_back(std::move(this_arg));
									}
									
									ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), decl.identifier_token()));
									
									// Replace init_operands with the result of the conversion
									init_operands.clear();
									init_operands.emplace_back(type_node.type());
									init_operands.emplace_back(type_node.pointer_depth() > 0 ? 64 : static_cast<int>(type_node.size_in_bits()));
									init_operands.emplace_back(result_var);
								}
							}
						}
					}
					
					operands.insert(operands.end(), init_operands.begin(), init_operands.end());
				} else {
					// For struct with constructor, evaluate the initializer to check if it's an rvalue
					auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
					// Check if this is an rvalue (TempVar) - function return value
					bool is_rvalue = (init_operands.size() >= 3 && std::holds_alternative<TempVar>(init_operands[2]));
					if (is_rvalue) {
						// For rvalues, use direct initialization (no constructor call)
						operands.insert(operands.end(), init_operands.begin(), init_operands.end());
					}
					// For lvalues, skip adding to operands - will use constructor call below
				}
			}
		}

		if (!symbol_table.insert(decl.identifier_token().value(), ast_node)) {
			assert(false && "Expected identifier to be unique");
		}

		VariableDeclOp decl_op;
		decl_op.type = type_node.type();
		// References and pointers are both 64-bit (pointer size on x64)
		decl_op.size_in_bits = (type_node.pointer_depth() > 0 || type_node.is_reference()) ? 64 : static_cast<int>(type_node.size_in_bits());
		decl_op.var_name = decl.identifier_token().handle();
		decl_op.custom_alignment = static_cast<unsigned long long>(decl.custom_alignment());
		decl_op.is_reference = type_node.is_reference();
		decl_op.is_rvalue_reference = type_node.is_rvalue_reference();
		decl_op.is_array = decl.is_array();
		if (decl.is_array() && operands.size() >= 10) {
			decl_op.array_element_type = std::get<Type>(operands[7]);
			decl_op.array_element_size = std::get<int>(operands[8]);
			if (std::holds_alternative<unsigned long long>(operands[9])) {
				decl_op.array_count = std::get<unsigned long long>(operands[9]);
			}
		}
		if (node.initializer() && !decl.is_array() && operands.size() >= 10) {
			TypedValue tv = toTypedValue(std::span<const IrOperand>(&operands[7], 3));
			decl_op.initializer = std::move(tv);
		}
		
		// Track whether the variable was already initialized with an rvalue (function return value)
		// Check if the VariableDecl has an initializer set BEFORE we move decl_op
		bool has_rvalue_initializer = decl_op.initializer.has_value();
		
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(decl_op), node.declaration().identifier_token()));

		// Handle array initialization with initializer list
		if (decl.is_array() && node.initializer().has_value()) {
			const ASTNode& init_node = *node.initializer();
			if (init_node.is<InitializerListNode>()) {
				const InitializerListNode& init_list = init_node.as<InitializerListNode>();
				const auto& initializers = init_list.initializers();
				
				// Generate store for each element
				for (size_t i = 0; i < initializers.size(); i++) {
					// Evaluate the initializer expression
					auto init_operands = visitExpressionNode(initializers[i].as<ExpressionNode>());
					
					// Generate array element store: arr[i] = value
					ArrayStoreOp store_op;
					store_op.element_type = type_node.type();
					store_op.element_size_in_bits = size_in_bits;
					store_op.array = decl.identifier_token().handle();
					store_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					store_op.value = toTypedValue(init_operands);
					store_op.member_offset = 0;
					store_op.is_pointer_to_array = false;  // Local arrays are actual arrays, not pointers
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(store_op), 
						node.declaration().identifier_token()));
				}
			}
		}

		// If this is a struct type with a constructor, generate a constructor call
		// IMPORTANT: Only for non-pointer struct types. Pointers are just addresses, no constructor needed.
		// IMPORTANT: References also don't need constructor calls - they just bind to existing objects
		if (type_node.type() == Type::Struct && type_node.pointer_depth() == 0 && !type_node.is_reference() && !type_node.is_rvalue_reference()) {
			TypeIndex type_index = type_node.type_index();
			if (type_index < gTypeInfo.size()) {
				const TypeInfo& type_info = gTypeInfo[type_index];
				
				// Skip incomplete template instantiations (with "_unknown" in name)
				std::string_view type_name = StringTable::getStringView(type_info.name());
				if (type_name.find("_unknown") != std::string_view::npos) {
					FLASH_LOG(Codegen, Debug, "Skipping constructor call for '", type_name, "' with _unknown in name (incomplete instantiation)");
					// Don't generate constructor calls for incomplete template instantiations
					// Just treat them as plain data (no initialization)
					// The variable declaration was already emitted above
					return;
				}
				
				if (type_info.struct_info_) {
					// Check if this is an abstract class (only for non-pointer types)
					if (type_info.struct_info_->is_abstract && type_node.pointer_levels().empty()) {
						FLASH_LOG(Codegen, Error, "Cannot instantiate abstract class '", type_info.name(), "'");
						assert(false && "Cannot instantiate abstract class");
					}

					if (type_info.struct_info_->hasConstructor()) {
						FLASH_LOG(Codegen, Debug, "Struct ", type_info.name(), " has constructor");
						// Check if we have a copy/move initializer like "Tiny t2 = t;"
						// Skip if the variable was already initialized with an rvalue (function return)
						bool has_copy_init = false;
						bool has_direct_ctor_call = false;
						const ConstructorCallNode* direct_ctor = nullptr;
						
						FLASH_LOG(Codegen, Debug, "has_rvalue_initializer=", has_rvalue_initializer, " node.initializer()=", (bool)node.initializer());
						if (node.initializer() && !has_rvalue_initializer) {
							const ASTNode& init_node = *node.initializer();
							if (init_node.is<ExpressionNode>()) {
								const auto& expr = init_node.as<ExpressionNode>();
								FLASH_LOG(Codegen, Debug, "Checking initializer for ", decl.identifier_token().value());
								// Check if this is a direct constructor call (e.g., S s(x))
								if (std::holds_alternative<ConstructorCallNode>(expr)) {
									has_direct_ctor_call = true;
									direct_ctor = &std::get<ConstructorCallNode>(expr);
									FLASH_LOG(Codegen, Debug, "Found ConstructorCallNode initializer");
								} else if (!init_node.is<InitializerListNode>()) {
									// For copy initialization like "AllSizes b = a;", we need to
									// generate a copy constructor call.
									has_copy_init = true;
								}
							}
						}

						if (has_direct_ctor_call && direct_ctor) {
							// Direct constructor call like S s(x) - process its arguments directly
							FLASH_LOG(Codegen, Debug, "Processing direct constructor call for ", type_info.name());
							// Find the matching constructor to get parameter types for reference handling
							const ConstructorDeclarationNode* matching_ctor = nullptr;
							size_t num_args = 0;
							direct_ctor->arguments().visit([&](ASTNode) { num_args++; });
							
							if (type_info.struct_info_) {
								// Special case: If we have exactly one argument of the same struct type, try copy constructor first
								// This ensures copy constructors are preferred over converting constructors
								// But only when the argument is actually of the same struct type
								if (num_args == 1) {
									// Check if the argument is an identifier of the same struct type
									ASTNode first_arg;
									direct_ctor->arguments().visit([&](ASTNode arg) { 
										if (!first_arg.has_value()) first_arg = arg;
									});
									
									bool arg_is_same_struct_type = false;
									if (first_arg.has_value() && first_arg.is<ExpressionNode>()) {
										const auto& expr = first_arg.as<ExpressionNode>();
										if (std::holds_alternative<IdentifierNode>(expr)) {
											const auto& ident = std::get<IdentifierNode>(expr);
											std::optional<ASTNode> arg_symbol = symbol_table.lookup(ident.name());
											if (arg_symbol.has_value()) {
												if (const DeclarationNode* arg_decl = get_decl_from_symbol(*arg_symbol)) {
													const TypeSpecifierNode& arg_type = arg_decl->type_node().as<TypeSpecifierNode>();
													// Check if argument is of the same struct type
													if (arg_type.type() == Type::Struct && 
														arg_type.type_index() == type_node.type_index()) {
														arg_is_same_struct_type = true;
													}
												}
											}
										}
									}
									
									// Only select copy constructor if argument is of the same struct type
									if (arg_is_same_struct_type) {
										const StructMemberFunction* copy_ctor_func = type_info.struct_info_->findCopyConstructor();
										if (copy_ctor_func && copy_ctor_func->function_decl.is<ConstructorDeclarationNode>()) {
											matching_ctor = &copy_ctor_func->function_decl.as<ConstructorDeclarationNode>();
											FLASH_LOG(Codegen, Debug, "Matched copy constructor for ", type_info.name());
										}
									}
								}
								
								// If we didn't find a copy constructor, use general matching
								if (!matching_ctor) {
									for (const auto& func : type_info.struct_info_->member_functions) {
										if (func.is_constructor && func.function_decl.is<ConstructorDeclarationNode>()) {
											const auto& ctor_node = func.function_decl.as<ConstructorDeclarationNode>();
											const auto& params = ctor_node.parameter_nodes();
											
											// Match constructor with same number of parameters or with default parameters
											if (params.size() == num_args) {
												matching_ctor = &ctor_node;
												break;
											} else if (params.size() > num_args) {
												// Check if remaining params have defaults
												bool all_have_defaults = true;
												for (size_t i = num_args; i < params.size(); ++i) {
													if (!params[i].is<DeclarationNode>() || 
													    !params[i].as<DeclarationNode>().has_default_value()) {
														all_have_defaults = false;
														break;
													}
												}
												if (all_have_defaults) {
													matching_ctor = &ctor_node;
													break;
												}
											}
										}
									}
								}
							}
							
							// Create constructor call with the declared variable as the object
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name();
							ctor_op.object = decl.identifier_token().handle();
							
							// Get constructor parameter types for reference handling
							const auto& ctor_params = matching_ctor ? matching_ctor->parameter_nodes() : std::vector<ASTNode>{};
							
							// Process constructor arguments with reference parameter handling
							size_t arg_index = 0;
							direct_ctor->arguments().visit([&](ASTNode argument) {
								// Get the parameter type for this argument (if it exists)
								const TypeSpecifierNode* param_type = nullptr;
								if (arg_index < ctor_params.size() && ctor_params[arg_index].is<DeclarationNode>()) {
									param_type = &ctor_params[arg_index].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
								}
								
								auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
								// argumentIrOperands = [type, size, value]
								if (argumentIrOperands.size() >= 3) {
									TypedValue tv;
									
									// Check if parameter expects a reference and argument is an identifier
									if (param_type && (param_type->is_reference() || param_type->is_rvalue_reference()) &&
									    std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
										const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
										std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
										
										// Handle both DeclarationNode and VariableDeclarationNode
										const DeclarationNode* arg_decl = nullptr;
										if (symbol.has_value() && symbol->is<DeclarationNode>()) {
											arg_decl = &symbol->as<DeclarationNode>();
										} else if (symbol.has_value() && symbol->is<VariableDeclarationNode>()) {
											arg_decl = &symbol->as<VariableDeclarationNode>().declaration();
										}
										
										if (arg_decl) {
											const auto& arg_type = arg_decl->type_node().as<TypeSpecifierNode>();
											
											if (arg_type.is_reference() || arg_type.is_rvalue_reference()) {
												// Argument is already a reference - just pass it through
												tv = toTypedValue(argumentIrOperands);
											} else {
												// Argument is a value - take its address
												TempVar addr_var = var_counter.next();
												AddressOfOp addr_op;
												addr_op.result = addr_var;
												addr_op.operand.type = arg_type.type();
												addr_op.operand.size_in_bits = static_cast<int>(arg_type.size_in_bits());
												addr_op.operand.pointer_depth = 0;  // TODO: Verify pointer depth
												addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
												ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
												
												// Create TypedValue with the address
												tv.type = arg_type.type();
												tv.size_in_bits = 64;  // Pointer size
												tv.value = addr_var;
												tv.ref_qualifier = ReferenceQualifier::LValueReference;  // Mark as reference parameter
												tv.type_index = arg_type.type_index();  // Preserve type_index for struct references
											}
										} else {
											// Not a simple identifier or not found - use as-is
											tv = toTypedValue(argumentIrOperands);
										}
									} else {
										// Not a reference parameter or not an identifier - use as-is
										tv = toTypedValue(argumentIrOperands);
									}
									
									// If we have parameter type information, use it to set pointer depth and CV qualifiers
									if (param_type) {
										tv.pointer_depth = static_cast<int>(param_type->pointer_depth());
										// For pointer types, also extract CV qualifiers from pointer levels
										if (param_type->is_pointer() && !param_type->pointer_levels().empty()) {
											// Use CV qualifier from the first pointer level (T* const -> const)
											// For now, we'll use the main CV qualifier
											if (!tv.is_reference()) {
												tv.cv_qualifier = param_type->cv_qualifier();
											}
										}
										// For reference types, use the CV qualifier
										if (param_type->is_reference() || param_type->is_rvalue_reference()) {
											tv.cv_qualifier = param_type->cv_qualifier();
										}
										// Also update type_index if it's a struct type
										if (param_type->type() == Type::Struct && param_type->type_index() != 0) {
											tv.type_index = param_type->type_index();
										}
									}
									
									ctor_op.arguments.push_back(std::move(tv));
								}
								arg_index++;
							});
							
							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));

							// Register for destructor if needed
							if (type_info.struct_info_ && type_info.struct_info_->hasDestructor()) {
								registerVariableWithDestructor(
									std::string(decl.identifier_token().value()),
									std::string(StringTable::getStringView(type_info.name()))
								);
							}
						} else if (has_copy_init) {
							// Generate copy constructor call
							ConstructorCallOp ctor_op;
							ctor_op.struct_name = type_info.name();
							ctor_op.object = decl.identifier_token().handle();

							// Add initializer as copy constructor parameter
							// Copy constructors take const lvalue reference parameters
							const ASTNode& init_node = *node.initializer();
							auto init_operands = visitExpressionNode(init_node.as<ExpressionNode>());
							// init_operands = [type, size, value]
							if (init_operands.size() >= 3) {
								TypedValue init_arg;
								
								// Check if initializer is an identifier (variable)
								if (std::holds_alternative<IdentifierNode>(init_node.as<ExpressionNode>())) {
									const auto& identifier = std::get<IdentifierNode>(init_node.as<ExpressionNode>());
									std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
									
									// Handle both DeclarationNode and VariableDeclarationNode
									const DeclarationNode* init_decl = nullptr;
									if (symbol.has_value() && symbol->is<DeclarationNode>()) {
										init_decl = &symbol->as<DeclarationNode>();
									} else if (symbol.has_value() && symbol->is<VariableDeclarationNode>()) {
										init_decl = &symbol->as<VariableDeclarationNode>().declaration();
									}
									
									if (init_decl) {
										const auto& init_type = init_decl->type_node().as<TypeSpecifierNode>();
										
										if (init_type.is_reference() || init_type.is_rvalue_reference()) {
											// Initializer is already a reference - just pass it through
											init_arg = toTypedValue(init_operands);
										} else {
											// Initializer is a value - take its address for copy constructor
											TempVar addr_var = var_counter.next();
											AddressOfOp addr_op;
											addr_op.result = addr_var;
											addr_op.operand.type = init_type.type();
											addr_op.operand.size_in_bits = static_cast<int>(init_type.size_in_bits());
											addr_op.operand.pointer_depth = 0;
											addr_op.operand.value = StringTable::getOrInternStringHandle(identifier.name());
											ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, std::move(addr_op), Token()));
											
											// Create TypedValue with the address
											init_arg.type = init_type.type();
											init_arg.size_in_bits = 64;  // Pointer size
											init_arg.value = addr_var;
											init_arg.ref_qualifier = ReferenceQualifier::LValueReference;  // Mark as reference parameter
											init_arg.type_index = init_type.type_index();  // Preserve type_index for struct references
										}
									} else {
										// Symbol not found - use as-is
										init_arg = toTypedValue(init_operands);
									}
								} else {
									// Not an identifier (e.g., temporary, expression result) - use as-is
									init_arg = toTypedValue(init_operands);
								}
								
								ctor_op.arguments.push_back(std::move(init_arg));
							}

							ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));

							// Register for destructor if needed
			if (type_info.struct_info_ && type_info.struct_info_->hasDestructor()) {
								registerVariableWithDestructor(
									std::string(decl.identifier_token().value()),
									std::string(StringTable::getStringView(type_info.name()))
								);
							}
						} else if (!has_rvalue_initializer) {
							// No initializer - check if we need to call default constructor
							// Call default constructor if:
							// 1. It's user-defined (not implicit), OR
							// 2. The struct has default member initializers (implicit ctor needs to init them), OR
							// 3. The struct has a vtable (implicit ctor needs to init the vptr), OR
							// 4. The struct has base classes with constructors (implicit ctor needs to call base ctors)
							const StructMemberFunction* default_ctor = type_info.struct_info_->findDefaultConstructor();
							bool is_implicit_default_ctor = false;
							if (default_ctor && default_ctor->function_decl.is<ConstructorDeclarationNode>()) {
								const auto& ctor_node = default_ctor->function_decl.as<ConstructorDeclarationNode>();
								is_implicit_default_ctor = ctor_node.is_implicit();
							}

							// Check if any base class has constructors that need to be called
							bool has_base_with_constructors = false;
							for (const auto& base : type_info.struct_info_->base_classes) {
								if (base.type_index < gTypeInfo.size()) {
									const TypeInfo& base_type_info = gTypeInfo[base.type_index];
									const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
									if (base_struct_info && base_struct_info->hasAnyConstructor()) {
										has_base_with_constructors = true;
										break;
									}
								}
							}

							bool needs_default_ctor_call = !is_implicit_default_ctor ||
							                               type_info.struct_info_->hasDefaultMemberInitializers() ||
							                               type_info.struct_info_->has_vtable ||
							                               has_base_with_constructors;

							if (needs_default_ctor_call) {
								// Check if this is an array - need to call constructor for each element
								if (decl.is_array()) {
									// For arrays, we need to call the constructor once for each element
									// Get array size
									size_t ctor_array_count = 1;
									auto size_expr = decl.array_size();
									if (size_expr.has_value()) {
										// Evaluate the array size expression using ConstExprEvaluator
										ConstExpr::EvaluationContext array_ctx(symbol_table);
										auto eval_result = ConstExpr::Evaluator::evaluate(*size_expr, array_ctx);
										if (eval_result.success()) {
											ctor_array_count = static_cast<size_t>(eval_result.as_int());
										}
									}
									
									// Generate constructor call for each array element
									for (size_t i = 0; i < ctor_array_count; i++) {
										ConstructorCallOp ctor_op;
										ctor_op.struct_name = type_info.name();
										// For arrays, we need to specify the element to construct
										ctor_op.object = decl.identifier_token().handle();
										ctor_op.array_index = i;  // Mark this as an array element constructor call
										
										// If the constructor has parameters with default values, generate the default arguments
										if (default_ctor && default_ctor->function_decl.is<ConstructorDeclarationNode>()) {
											const auto& ctor_node = default_ctor->function_decl.as<ConstructorDeclarationNode>();
											const auto& params = ctor_node.parameter_nodes();
											
											for (const auto& param : params) {
												if (param.is<DeclarationNode>()) {
													const auto& param_decl = param.as<DeclarationNode>();
													if (param_decl.has_default_value()) {
														// Generate IR for the default value expression
														const ASTNode& default_node = param_decl.default_value();
														if (default_node.is<ExpressionNode>()) {
															auto default_operands = visitExpressionNode(default_node.as<ExpressionNode>());
															if (default_operands.size() >= 3) {
																TypedValue default_arg = toTypedValue(default_operands);
																ctor_op.arguments.push_back(std::move(default_arg));
															}
														}
													}
												}
											}
										}
										
										ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
									}
								} else {
									// Single object (non-array) - generate single constructor call
									ConstructorCallOp ctor_op;
									ctor_op.struct_name = type_info.name();
									ctor_op.object = decl.identifier_token().handle();
									
									// If the constructor has parameters with default values, generate the default arguments
									if (default_ctor && default_ctor->function_decl.is<ConstructorDeclarationNode>()) {
										const auto& ctor_node = default_ctor->function_decl.as<ConstructorDeclarationNode>();
										const auto& params = ctor_node.parameter_nodes();
										
										for (const auto& param : params) {
											if (param.is<DeclarationNode>()) {
												const auto& param_decl = param.as<DeclarationNode>();
												if (param_decl.has_default_value()) {
													// Generate IR for the default value expression
													const ASTNode& default_node = param_decl.default_value();
													if (default_node.is<ExpressionNode>()) {
														auto default_operands = visitExpressionNode(default_node.as<ExpressionNode>());
														if (default_operands.size() >= 3) {
															TypedValue default_arg = toTypedValue(default_operands);
															ctor_op.arguments.push_back(std::move(default_arg));
														}
													}
												}
											}
										}
									}
									
									ir_.addInstruction(IrInstruction(IrOpcode::ConstructorCall, std::move(ctor_op), decl.identifier_token()));
								}
							}
						}
					}
				}
				
				// If this struct has a destructor, register it for automatic cleanup
				if (type_info.struct_info_ && type_info.struct_info_->hasDestructor()) {
					registerVariableWithDestructor(
						std::string(decl.identifier_token().value()),
						std::string(StringTable::getStringView(type_info.name()))
					);
				}
			}
		}
	}

	void visitStructuredBindingNode(const ASTNode& ast_node) {
		const StructuredBindingNode& node = ast_node.as<StructuredBindingNode>();
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Processing structured binding with ", 
		          node.identifiers().size(), " identifiers");
		
		// Step 1: Evaluate the initializer expression and get its type
		const ASTNode& initializer = node.initializer();
		if (!initializer.is<ExpressionNode>()) {
			FLASH_LOG(Codegen, Error, "Structured binding initializer is not an expression");
			return;
		}
		
		auto init_operands = visitExpressionNode(initializer.as<ExpressionNode>());
		if (init_operands.size() < 3) {
			FLASH_LOG(Codegen, Error, "Structured binding initializer produced invalid operands");
			return;
		}
		
		// Extract initializer type information
		Type init_type = std::get<Type>(init_operands[0]);
		int init_size = std::get<int>(init_operands[1]);
		TypeIndex init_type_index = 0;
		
		// Get type_index if available (4th element)
		if (init_operands.size() >= 4 && std::holds_alternative<unsigned long long>(init_operands[3])) {
			init_type_index = static_cast<TypeIndex>(std::get<unsigned long long>(init_operands[3]));
		}
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Initializer type=", (int)init_type, 
		          " type_index=", init_type_index, " ref_qualifier=", (int)node.ref_qualifier());
		
		// Check if this is a reference binding (auto& or auto&&)
		bool is_reference_binding = node.is_lvalue_reference() || node.is_rvalue_reference();
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: is_reference_binding=", is_reference_binding,
		          " is_lvalue_ref=", node.is_lvalue_reference(), " is_rvalue_ref=", node.is_rvalue_reference());
		
		// Step 2: Determine if initializer is an array by checking the symbol table
		bool is_array = false;
		size_t array_size = 0;
		Type array_element_type = init_type;
		int array_element_size = init_size;
		
		// Check if initializer is an identifier (which could be an array variable)
		if (initializer.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = initializer.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr_node)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
				std::optional<ASTNode> symbol = symbol_table.lookup(id_node.name());
				
				if (symbol.has_value()) {
					// Check if it's a DeclarationNode with array information
					if (symbol->is<DeclarationNode>()) {
						const DeclarationNode& decl = symbol->as<DeclarationNode>();
						if (decl.is_array() && decl.array_size().has_value()) {
							// Evaluate array size
							ConstExpr::EvaluationContext ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*decl.array_size(), ctx);
							if (eval_result.success()) {
								is_array = true;
								array_size = static_cast<size_t>(eval_result.as_int());
								// Get element type and size from the type specifier
								const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
								array_element_type = type_spec.type();
								array_element_size = static_cast<int>(type_spec.size_in_bits());
								FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Detected array with size ", array_size, 
								          " element_type=", (int)array_element_type, " element_size=", array_element_size);
							}
						}
					}
					// Check if it's a VariableDeclarationNode with array information
					else if (symbol->is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var_decl = symbol->as<VariableDeclarationNode>();
						const DeclarationNode& decl = var_decl.declaration();
						if (decl.is_array() && decl.array_size().has_value()) {
							// Evaluate array size
							ConstExpr::EvaluationContext ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*decl.array_size(), ctx);
							if (eval_result.success()) {
								is_array = true;
								array_size = static_cast<size_t>(eval_result.as_int());
								// Get element type and size from the type specifier
								const TypeSpecifierNode& type_spec = decl.type_node().as<TypeSpecifierNode>();
								array_element_type = type_spec.type();
								array_element_size = static_cast<int>(type_spec.size_in_bits());
								FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Detected array with size ", array_size,
								          " element_type=", (int)array_element_type, " element_size=", array_element_size);
							}
						}
					}
				}
			}
		}
		
		// Step 3: Create a hidden temporary variable to hold the initializer
		// Generate unique name for the hidden variable
		TempVar hidden_var = var_counter.next();
		StringBuilder sb;
		sb.append("__structured_binding_e_").append(static_cast<uint64_t>(hidden_var.var_number));
		std::string_view hidden_var_name = sb.commit();
		StringHandle hidden_var_handle = StringTable::createStringHandle(hidden_var_name);
		
		// Declare the hidden variable
		VariableDeclOp hidden_decl_op;
		hidden_decl_op.var_name = hidden_var_handle;
		
		// For arrays, we need to set up the array info properly
		if (is_array) {
			hidden_decl_op.type = array_element_type;
			hidden_decl_op.size_in_bits = array_element_size;
			hidden_decl_op.is_array = true;
			hidden_decl_op.array_element_type = array_element_type;
			hidden_decl_op.array_element_size = array_element_size;
			hidden_decl_op.array_count = array_size;
			// Don't set initializer here for arrays - we'll copy element by element
		} else if (is_reference_binding) {
			// For reference bindings (auto& [a,b] = x), the hidden variable is a reference
			// to the original object, not a copy
			hidden_decl_op.type = init_type;
			hidden_decl_op.size_in_bits = 64;  // Reference is always 64-bit pointer
			hidden_decl_op.is_reference = true;
			hidden_decl_op.is_rvalue_reference = node.is_rvalue_reference();
			
			// Generate addressof for the initializer to get reference
			if (initializer.is<ExpressionNode>()) {
				const ExpressionNode& expr_node = initializer.as<ExpressionNode>();
				if (std::holds_alternative<IdentifierNode>(expr_node)) {
					const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
					
					// Generate AddressOf for the identifier
					TempVar addr_temp = var_counter.next();
					AddressOfOp addr_op;
					addr_op.result = addr_temp;
					addr_op.operand.type = init_type;
					addr_op.operand.size_in_bits = init_size;
					addr_op.operand.pointer_depth = 0;
					addr_op.operand.value = StringTable::getOrInternStringHandle(id_node.name());
					ir_.addInstruction(IrInstruction(IrOpcode::AddressOf, addr_op, Token()));
					
					hidden_decl_op.initializer = TypedValue{init_type, 64, addr_temp};
				} else {
					// For other expressions, just use the value and hope for the best
					hidden_decl_op.initializer = toTypedValue(init_operands);
				}
			} else {
				hidden_decl_op.initializer = toTypedValue(init_operands);
			}
		} else {
			hidden_decl_op.type = init_type;
			hidden_decl_op.size_in_bits = init_size;
			hidden_decl_op.initializer = toTypedValue(init_operands);
		}
		
		ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(hidden_decl_op), Token()));
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Created hidden variable ", hidden_var_name);
		
		// For arrays, copy elements from the source array to the hidden variable
		if (is_array && initializer.is<ExpressionNode>()) {
			const ExpressionNode& expr_node = initializer.as<ExpressionNode>();
			if (std::holds_alternative<IdentifierNode>(expr_node)) {
				const IdentifierNode& id_node = std::get<IdentifierNode>(expr_node);
				StringHandle source_array = StringTable::getOrInternStringHandle(id_node.name());
				
				// Copy each element
				for (size_t i = 0; i < array_size; ++i) {
					// Load element from source array
					TempVar element_temp = var_counter.next();
					ArrayAccessOp access_op;
					access_op.result = element_temp;
					access_op.array = source_array;
					access_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					access_op.element_type = array_element_type;
					access_op.element_size_in_bits = array_element_size;
					access_op.is_pointer_to_array = false;
					access_op.member_offset = 0;
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(access_op), Token()));
					
					// Store element to hidden array
					ArrayStoreOp store_op;
					store_op.element_type = array_element_type;
					store_op.element_size_in_bits = array_element_size;
					store_op.array = hidden_var_handle;
					store_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					store_op.value = TypedValue{array_element_type, array_element_size, element_temp};
					store_op.member_offset = 0;
					store_op.is_pointer_to_array = false;
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayStore, std::move(store_op), Token()));
				}
			}
		}
		
		// Step 4: Determine decomposition strategy
		if (is_array) {
			// Array decomposition
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Using array decomposition strategy");
			
			// Validate identifier count matches array size
			if (node.identifiers().size() != array_size) {
				FLASH_LOG(Codegen, Error, "Structured binding: number of identifiers (", node.identifiers().size(), 
				          ") does not match array size (", array_size, ")");
				return;
			}
			
			// Create bindings for each array element
			for (size_t i = 0; i < array_size; ++i) {
				StringHandle binding_id = node.identifiers()[i];
				std::string_view binding_name = StringTable::getStringView(binding_id);
				
				FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Creating binding '", binding_name, 
				          "' to array element [", i, "]");
				
				// Create a TypeSpecifierNode for this binding's type
				TypeSpecifierNode binding_type(array_element_type, TypeQualifier::None, 
				                                static_cast<unsigned char>(array_element_size), Token());
				
				// If this is a reference binding (auto& or auto&&), mark the type as a reference
				if (is_reference_binding) {
					if (node.is_lvalue_reference()) {
						binding_type.set_reference(false);  // false = lvalue reference
					} else if (node.is_rvalue_reference()) {
						// For auto&&, bindings to array elements become lvalue references
						binding_type.set_reference(false);  // false = lvalue reference
					}
				}
				
				// Create a synthetic declaration node for the binding
				Token binding_token(Token::Type::Identifier, binding_name, 0, 0, 0);
				ASTNode binding_decl_node = ASTNode::emplace_node<DeclarationNode>(
					ASTNode::emplace_node<TypeSpecifierNode>(binding_type),
					binding_token
				);
				
				// Add to symbol table
				symbol_table.insert(binding_name, binding_decl_node);
				
				// Generate IR for the binding
				if (is_reference_binding) {
					// For reference bindings, create a reference variable that points to the element
					// Compute address of array element: &(hidden_var[i])
					TempVar element_addr = var_counter.next();
					ArrayElementAddressOp addr_op;
					addr_op.result = element_addr;
					addr_op.array = hidden_var_handle;
					addr_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					addr_op.element_type = array_element_type;
					addr_op.element_size_in_bits = array_element_size;
					addr_op.is_pointer_to_array = false;
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayElementAddress, std::move(addr_op), binding_token));
					
					// Declare the binding as a reference variable initialized with the address
					VariableDeclOp binding_var_decl;
					binding_var_decl.var_name = binding_id;
					binding_var_decl.type = array_element_type;
					binding_var_decl.size_in_bits = 64;  // References are pointers (64-bit addresses)
					binding_var_decl.is_reference = true;  // Mark as reference
					binding_var_decl.is_rvalue_reference = node.is_rvalue_reference();
					binding_var_decl.initializer = TypedValue{array_element_type, 64, element_addr};
					
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
				} else {
					// For value bindings, load the element value (existing behavior)
					TempVar element_val = var_counter.next();
					ArrayAccessOp load_op;
					load_op.result = element_val;
					load_op.array = hidden_var_handle;
					load_op.index = TypedValue{Type::Int, 32, static_cast<unsigned long long>(i)};
					load_op.element_type = array_element_type;
					load_op.element_size_in_bits = array_element_size;
					load_op.is_pointer_to_array = false;  // Local array
					load_op.member_offset = 0;
					
					ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(load_op), binding_token));
					
					// Now, declare the binding variable with the element value as initializer
					VariableDeclOp binding_var_decl;
					binding_var_decl.var_name = binding_id;
					binding_var_decl.type = array_element_type;
					binding_var_decl.size_in_bits = array_element_size;
					binding_var_decl.initializer = TypedValue{array_element_type, array_element_size, element_val};
					
					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
				}
				
				FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Added binding '", binding_name, "' to symbol table");
			}
			
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Successfully created ", array_size, " array bindings");
			return;  // Early return for array case
			
		} else if (init_type != Type::Struct) {
			FLASH_LOG(Codegen, Error, "Structured bindings currently only support struct and array types, got type=", (int)init_type);
			return;
		}
		
		// Step 5: Check for tuple-like decomposition (C++17 protocol)
		// If std::tuple_size<E> is specialized for the type, use tuple-like decomposition
		// Otherwise, fall back to aggregate (struct) decomposition
		if (init_type_index >= gTypeInfo.size()) {
			FLASH_LOG(Codegen, Error, "Invalid type index for structured binding: ", init_type_index);
			return;
		}
		
		const TypeInfo& type_info = gTypeInfo[init_type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		
		if (!struct_info) {
			FLASH_LOG(Codegen, Error, "Type is not a struct for structured binding");
			return;
		}
		
		// Step 5a: Check for tuple-like decomposition protocol (C++17)
		// If std::tuple_size<E> is specialized for the type E, use tuple-like decomposition
		// The protocol requires: std::tuple_size<E>, std::tuple_element<N, E>, and get<N>(e)
		std::string_view type_name_view = StringTable::getStringView(type_info.name());
		
		// Build the expected tuple_size specialization name: "tuple_size_TypeName" or "std::tuple_size_TypeName"
		StringBuilder tuple_size_name_builder;
		tuple_size_name_builder.append("tuple_size_").append(type_name_view);
		std::string_view tuple_size_name = tuple_size_name_builder.commit();
		StringHandle tuple_size_handle = StringTable::getOrInternStringHandle(tuple_size_name);
		
		// Also try with std:: prefix  
		StringBuilder std_tuple_size_name_builder;
		std_tuple_size_name_builder.append("std::tuple_size_").append(type_name_view);
		std::string_view std_tuple_size_name = std_tuple_size_name_builder.commit();
		StringHandle std_tuple_size_handle = StringTable::getOrInternStringHandle(std_tuple_size_name);
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Checking for tuple_size<", type_name_view, 
		          "> as '", tuple_size_name, "' or '", std_tuple_size_name, "'");
		
		// Look up the tuple_size specialization
		auto tuple_size_it = gTypesByName.find(tuple_size_handle);
		if (tuple_size_it == gTypesByName.end()) {
			tuple_size_it = gTypesByName.find(std_tuple_size_handle);
		}
		
		// If tuple_size is specialized for this type, use tuple-like decomposition
		if (tuple_size_it != gTypesByName.end()) {
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Found tuple_size specialization, using tuple-like decomposition");
			
			const TypeInfo* tuple_size_type_info = tuple_size_it->second;
			const StructTypeInfo* tuple_size_struct = tuple_size_type_info->getStructInfo();
			
			// Get the 'value' static member from tuple_size
			size_t tuple_size_value = 0;
			bool found_value = false;
			
			if (tuple_size_struct) {
				for (const auto& static_member : tuple_size_struct->static_members) {
					// Check for 'value' member (can be constexpr or const)
					if (StringTable::getStringView(static_member.name) == "value") {
						// Evaluate the static value
						if (static_member.initializer.has_value()) {
							ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*static_member.initializer, eval_ctx);
							if (eval_result.success()) {
								tuple_size_value = static_cast<size_t>(eval_result.as_int());
								found_value = true;
								FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: tuple_size::value = ", tuple_size_value);
							}
						}
						break;
					}
				}
			}
			
			if (!found_value) {
				FLASH_LOG(Codegen, Warning, "visitStructuredBindingNode: Could not get tuple_size::value, falling back to aggregate decomposition");
			} else {
				// Validate that the number of identifiers matches tuple_size::value
				if (node.identifiers().size() != tuple_size_value) {
					FLASH_LOG(Codegen, Error, "Structured binding: number of identifiers (", node.identifiers().size(), 
					          ") does not match tuple_size::value (", tuple_size_value, ")");
					return;
				}
				
				FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: tuple_size detected with ", tuple_size_value, " elements");
				
				// Try to find get<N>() functions for tuple-like decomposition
				bool all_get_found = true;
				std::vector<std::pair<StringHandle, Type>> binding_info;  // (mangled_name, return_type) for each get<N>
				
				// First, look up std::tuple_element<N, E>::type and get<N>() for each binding
				for (size_t i = 0; i < tuple_size_value && all_get_found; ++i) {
					// Build the tuple_element specialization name
					StringBuilder tuple_element_name_builder;
					tuple_element_name_builder.append("tuple_element_").append(static_cast<uint64_t>(i)).append("_").append(type_name_view);
					std::string_view tuple_element_name = tuple_element_name_builder.commit();
					
					// Also try with std:: prefix
					StringBuilder std_tuple_element_name_builder;
					std_tuple_element_name_builder.append("std::tuple_element_").append(static_cast<uint64_t>(i)).append("_").append(type_name_view);
					std::string_view std_tuple_element_name = std_tuple_element_name_builder.commit();
					
					// Look up the type alias
					StringHandle type_alias_handle = StringTable::getOrInternStringHandle(
						StringBuilder().append(tuple_element_name).append("::type").commit());
					StringHandle std_type_alias_handle = StringTable::getOrInternStringHandle(
						StringBuilder().append(std_tuple_element_name).append("::type").commit());
					
					auto type_alias_it = gTypesByName.find(type_alias_handle);
					if (type_alias_it == gTypesByName.end()) {
						type_alias_it = gTypesByName.find(std_type_alias_handle);
					}
					
					Type element_type = Type::Int;  // Default
					int element_size = 32;
					TypeIndex element_type_index = 0;
					
					if (type_alias_it != gTypesByName.end()) {
						const TypeInfo* type_alias_info = type_alias_it->second;
						element_type = type_alias_info->type_;
						element_type_index = type_alias_info->type_index_;
						element_size = type_alias_info->type_size_;
						if (element_size == 0) {
							element_size = get_type_size_bits(element_type);
						}
						FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: tuple_element<", i, ">::type = ", (int)element_type, ", size=", element_size);
					}
					
					// Now look for the get<N>() function
					// First, try template registry with exact index
					TemplateTypeArg index_arg;
					index_arg.base_type = Type::UnsignedLong;
					index_arg.is_value = true;
					index_arg.value = static_cast<int64_t>(i);
					std::vector<TemplateTypeArg> get_template_args = { index_arg };
					
					auto get_spec = gTemplateRegistry.lookupExactSpecialization("get", get_template_args);
					
					if (get_spec.has_value() && get_spec->is<FunctionDeclarationNode>()) {
						const FunctionDeclarationNode& get_func = get_spec->as<FunctionDeclarationNode>();
						
						// Generate mangled name with template argument
						const DeclarationNode& decl_node = get_func.decl_node();
						const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
						
						std::vector<TypeSpecifierNode> param_types;
						for (const auto& param : get_func.parameter_nodes()) {
							param_types.push_back(param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
						}
						
						std::vector<int64_t> template_args = { static_cast<int64_t>(i) };
						auto mangled = NameMangling::generateMangledNameWithTemplateArgs(
							"get", return_type, param_types, template_args, 
							get_func.is_variadic(), "", current_namespace_stack_);
						
						StringHandle mangled_handle = StringTable::getOrInternStringHandle(mangled.view());
						binding_info.push_back({mangled_handle, element_type});
						
						FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Found get<", i, "> with mangled name: ", mangled.view());
					} else {
						// Try symbol table lookup for explicit specializations
						extern SymbolTable gSymbolTable;
						auto get_overloads = gSymbolTable.lookup_all("get");
						
						bool found_this_get = false;
						size_t func_index = 0;
						
						for (const auto& overload : get_overloads) {
							if (!overload.is<FunctionDeclarationNode>()) continue;
							
							const FunctionDeclarationNode& get_func = overload.as<FunctionDeclarationNode>();
							const DeclarationNode& decl_node = get_func.decl_node();
							const TypeSpecifierNode& return_type = decl_node.type_node().as<TypeSpecifierNode>();
							
							// Check if this overload's return type matches our element type
							// or if it's the i-th function declaration (by order)
							bool type_matches = (return_type.type() == element_type);
							if (element_type == Type::Struct) {
								type_matches = type_matches && (return_type.type_index() == element_type_index);
							}
							
							if (type_matches || func_index == i) {
								// Generate mangled name with template argument for this specialization
								std::vector<TypeSpecifierNode> param_types;
								for (const auto& param : get_func.parameter_nodes()) {
									param_types.push_back(param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>());
								}
								
								std::vector<int64_t> template_args = { static_cast<int64_t>(i) };
								auto mangled = NameMangling::generateMangledNameWithTemplateArgs(
									"get", return_type, param_types, template_args,
									get_func.is_variadic(), "", current_namespace_stack_);
								
								StringHandle mangled_handle = StringTable::getOrInternStringHandle(mangled.view());
								binding_info.push_back({mangled_handle, element_type});
								
								FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Found get<", i, "> (symbol table) with mangled name: ", mangled.view());
								found_this_get = true;
								break;
							}
							func_index++;
						}
						
						if (!found_this_get) {
							FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: get<", i, "> not found, falling back to aggregate");
							all_get_found = false;
						}
					}
				}
				
				// If we found all get<N>() functions, generate the tuple-like decomposition
				if (all_get_found && binding_info.size() == tuple_size_value) {
					FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: All get<> functions found, using tuple-like protocol");
					
					// Generate calls to get<N>(e) for each binding
					for (size_t i = 0; i < tuple_size_value; ++i) {
						StringHandle binding_id = node.identifiers()[i];
						std::string_view binding_name = StringTable::getStringView(binding_id);
						
						auto [get_mangled_name, element_type] = binding_info[i];
						
						// Look up element size from tuple_element type alias
						int element_size = get_type_size_bits(element_type);
						TypeIndex element_type_index = 0;
						
						// Generate call to get<N>(hidden_var)
						TempVar result_temp = var_counter.next();
						
						CallOp call_op;
						call_op.result = result_temp;
						call_op.return_type = element_type;
						call_op.return_size_in_bits = element_size;
						call_op.return_type_index = element_type_index;
						call_op.function_name = get_mangled_name;
						call_op.is_member_function = false;
						
						// Pass the hidden variable as argument
						TypedValue arg;
						arg.type = init_type;
						arg.size_in_bits = init_size;
						arg.value = hidden_var_handle;
						arg.type_index = init_type_index;
						arg.ref_qualifier = ReferenceQualifier::LValueReference;  // Pass by const reference
						call_op.args.push_back(arg);
						
						Token binding_token(Token::Type::Identifier, binding_name, 0, 0, 0);
						ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(call_op), binding_token));
						
						// Create the binding variable
						VariableDeclOp binding_var_decl;
						binding_var_decl.var_name = binding_id;
						binding_var_decl.type = element_type;
						binding_var_decl.size_in_bits = element_size;
						TypedValue init_val3;
						init_val3.type = element_type;
						init_val3.size_in_bits = element_size;
						init_val3.value = result_temp;
						init_val3.type_index = element_type_index;
						binding_var_decl.initializer = init_val3;
						
						ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
						
						// Create synthetic declaration for symbol table
						TypeSpecifierNode binding_type(element_type, TypeQualifier::None,
						                               static_cast<unsigned char>(element_size > 255 ? 255 : element_size), Token());
						binding_type.set_type_index(element_type_index);
						
						ASTNode binding_decl_node = ASTNode::emplace_node<DeclarationNode>(
							ASTNode::emplace_node<TypeSpecifierNode>(binding_type),
							binding_token
						);
						symbol_table.insert(binding_name, binding_decl_node);
						
						FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Created tuple binding '", binding_name, 
						          "' via get<", i, ">");
					}
					
					FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Successfully created ", tuple_size_value, " bindings using tuple-like protocol");
					return;  // Done - don't fall through to aggregate decomposition
				}
				
				// Fall through to aggregate decomposition
				FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Falling through to aggregate decomposition");
			}
		}
		
		// Step 6: Aggregate (struct) decomposition
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Using aggregate decomposition");
		
		// Step 6a: Validate that we have the correct number of identifiers
		// Count non-static public members (all members in FlashCpp are non-static by default)
		size_t public_member_count = 0;
		for (const auto& member : struct_info->members) {
			if (member.access == AccessSpecifier::Public) {
				public_member_count++;
			}
		}
		
		if (node.identifiers().size() != public_member_count) {
			FLASH_LOG(Codegen, Error, "Structured binding: number of identifiers (", node.identifiers().size(), 
			          ") does not match number of public members (", public_member_count, ")");
			return;
		}
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Decomposing struct with ", 
		          public_member_count, " public members");
		
		// Step 7: Create bindings for each identifier
		// For each binding, we create a variable that's initialized with a member access expression
		size_t binding_idx = 0;
		for (const auto& member : struct_info->members) {
			if (member.access != AccessSpecifier::Public) {
				continue;  // Skip non-public members
			}
			
			if (binding_idx >= node.identifiers().size()) {
				break;  // Safety check
			}
			
			StringHandle binding_id = node.identifiers()[binding_idx];
			std::string_view binding_name = StringTable::getStringView(binding_id);
			
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Creating binding '", binding_name, 
			          "' to member '", StringTable::getStringView(member.name), "'");
			
			// Create a TypeSpecifierNode for this binding's type
			// The binding has the same type as the struct member
			// For size_in_bits, clamp to 255 since TypeSpecifierNode uses unsigned char
			// For struct types, type_index is what matters, not size_in_bits
			size_t member_size_bits_full = member.size * 8;
			unsigned char member_size_bits = (member_size_bits_full > 255) ? 255 : static_cast<unsigned char>(member_size_bits_full);
			TypeSpecifierNode binding_type(member.type, TypeQualifier::None, member_size_bits, Token());
			binding_type.set_type_index(member.type_index);
			
			// If this is a reference binding (auto& or auto&&), mark the type as a reference
			if (is_reference_binding) {
				if (node.is_lvalue_reference()) {
					binding_type.set_reference(false);  // false = lvalue reference
				} else if (node.is_rvalue_reference()) {
					// For auto&&, the binding type depends on value category
					// Since we're binding to members of the hidden variable, they're lvalues
					// So auto&& bindings to struct members become lvalue references
					binding_type.set_reference(false);  // false = lvalue reference
				}
			}
			
			// Create a synthetic declaration node for the binding
			// This allows the binding to be looked up in the symbol table
			Token binding_token(Token::Type::Identifier, binding_name, 0, 0, 0);
			ASTNode binding_decl_node = ASTNode::emplace_node<DeclarationNode>(
				ASTNode::emplace_node<TypeSpecifierNode>(binding_type),
				binding_token
			);
			
			// Add to symbol table
			symbol_table.insert(binding_name, binding_decl_node);
			
			// Generate IR for the binding
			if (is_reference_binding) {
				// For reference bindings, create a reference variable that points to the member
				// We need to get the address of the member, not load its value
				
				// Compute address of member: &(hidden_var.member)
				TempVar member_addr = var_counter.next();
				ComputeAddressOp addr_op;
				addr_op.result = member_addr;
				addr_op.base = hidden_var_handle;
				addr_op.total_member_offset = static_cast<int>(member.offset);
				addr_op.result_type = member.type;
				addr_op.result_size_bits = 64;  // Address is 64-bit pointer
				
				ir_.addInstruction(IrInstruction(IrOpcode::ComputeAddress, std::move(addr_op), binding_token));
				
				// Declare the binding as a reference variable initialized with the address
				VariableDeclOp binding_var_decl;
				binding_var_decl.var_name = binding_id;
				binding_var_decl.type = member.type;
				binding_var_decl.size_in_bits = 64;  // References are pointers (64-bit addresses)
				binding_var_decl.is_reference = true;  // Mark as reference
				binding_var_decl.is_rvalue_reference = node.is_rvalue_reference();
				TypedValue init_val;
				init_val.type = member.type;
				init_val.size_in_bits = 64;
				init_val.value = member_addr;
				init_val.type_index = member.type_index;
				binding_var_decl.initializer = init_val;
				
				ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
			} else {
				// For value bindings, load the member value (existing behavior)
				// First, generate a member access to load the value
				TempVar member_val = var_counter.next();
				MemberLoadOp load_op;
				load_op.result.type = member.type;
				load_op.result.size_in_bits = member_size_bits;
				load_op.result.value = member_val;
				load_op.result.type_index = member.type_index;
				load_op.object = hidden_var_handle;
				load_op.member_name = member.name;
				load_op.offset = static_cast<int>(member.offset);
				load_op.struct_type_info = &type_info;
				load_op.is_reference = member.is_reference;
				load_op.is_rvalue_reference = member.is_rvalue_reference;
				load_op.is_pointer_to_member = false;
				
				ir_.addInstruction(IrInstruction(IrOpcode::MemberAccess, std::move(load_op), binding_token));
				
				// Now, declare the binding variable with the member value as initializer
				VariableDeclOp binding_var_decl;
				binding_var_decl.var_name = binding_id;
				binding_var_decl.type = member.type;
				binding_var_decl.size_in_bits = member_size_bits;
				TypedValue init_val2;
				init_val2.type = member.type;
				init_val2.size_in_bits = static_cast<int>(member_size_bits);
				init_val2.value = member_val;
				init_val2.type_index = member.type_index;
				binding_var_decl.initializer = init_val2;
				
				ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(binding_var_decl), binding_token));
			}
			
			FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Added binding '", binding_name, "' to symbol table");
			
			binding_idx++;
		}
		
		FLASH_LOG(Codegen, Debug, "visitStructuredBindingNode: Successfully created ", binding_idx, " bindings");
	}

