#include "Parser.h"
#include "IrGenerator.h"
#include "SemanticAnalysis.h"

void AstToIr::visitBlockNode(const BlockNode& node) {
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

void AstToIr::visitIfStatementNode(const IfStatementNode& node) {
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
	// The condition may be a declaration: if (Type var = expr) — C++98/11/14/17/20
	ExprResult condition_result;
	auto cond_node = node.get_condition();
	if (cond_node.is<VariableDeclarationNode>()) {
	// Declaration-as-condition: visit the declaration to generate alloc + init IR,
	// then use the variable's value as the boolean condition.
	// ExpressionNode is a std::variant, so IdentifierNode implicitly converts to it.
		const VariableDeclarationNode& var_decl = cond_node.as<VariableDeclarationNode>();
		visitVariableDeclarationNode(cond_node);
		ExpressionNode ident_expr = IdentifierNode(var_decl.declaration().identifier_token());
		condition_result = visitExpressionNode(ident_expr);
	} else {
		condition_result = visitExpressionNode(cond_node.as<ExpressionNode>());
	}
	// C++20 [stmt.select]: contextual bool conversion.
	condition_result = applyConditionBoolConversion(condition_result, cond_node, Token());
	// C++20 [class.temporary]/4: flush temporaries bound in the condition full-expression.
	emitAndClearFullExpressionTempDestructors();

	// Generate conditional branch
	CondBranchOp cond_branch;
	cond_branch.label_true = StringTable::getOrInternStringHandle(then_label);
	cond_branch.label_false = StringTable::getOrInternStringHandle(node.has_else() ? else_label : end_label);
	cond_branch.condition = toTypedValue(condition_result);
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

void AstToIr::visitForStatementNode(const ForStatementNode& node) {
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
	pushLoopSehDepth();
	LoopBeginOp loop_begin;
	loop_begin.loop_start_label = loop_start_label;
	loop_begin.loop_end_label = loop_end_label;
	loop_begin.loop_increment_label = loop_increment_label;
	ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

	// Loop start: evaluate condition
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_start_label}, Token()));

	// Evaluate condition (if present, otherwise infinite loop)
	if (node.has_condition()) {
		ExprResult condition_result = visitExpressionNode(node.get_condition()->as<ExpressionNode>());
	// C++20 [stmt.for]: contextual bool conversion.
		condition_result = applyConditionBoolConversion(condition_result, *node.get_condition(), Token());
	// C++20 [class.temporary]/4: flush temporaries bound in the condition full-expression.
		emitAndClearFullExpressionTempDestructors();

	// Generate conditional branch: if true goto body, else goto end
		CondBranchOp cond_branch;
		cond_branch.label_true = loop_body_label;
		cond_branch.label_false = loop_end_label;
		cond_branch.condition = toTypedValue(condition_result);
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
	// C++20 [class.temporary]/4: flush temporaries from the increment full-expression.
		emitAndClearFullExpressionTempDestructors();
	}

	// Branch back to loop start
	ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start_label}, Token()));

	// Loop end label
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

	// Mark loop end
	ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	popLoopSehDepth();

	// Exit the for loop scope
	exitScope();
	symbol_table.exit_scope();
}

void AstToIr::visitWhileStatementNode(const WhileStatementNode& node) {
	// Generate unique labels for this while loop
	static size_t while_counter = 0;
	size_t current_while = while_counter++;

	// Use a single StringBuilder and commit each label before starting the next
	auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("while_start_").append(current_while));
	auto loop_body_label = StringTable::createStringHandle(StringBuilder().append("while_body_").append(current_while));
	auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("while_end_").append(current_while));

	// Mark loop begin for break/continue support
	// For while loops, continue jumps to loop_start (re-evaluate condition)
	pushLoopSehDepth();
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
	ExprResult condition_result = visitExpressionNode(node.get_condition().as<ExpressionNode>());
	// C++20 [stmt.while]: contextual bool conversion.
	condition_result = applyConditionBoolConversion(condition_result, node.get_condition(), Token());
	// C++20 [class.temporary]/4: flush temporaries bound in the condition full-expression.
	emitAndClearFullExpressionTempDestructors();

	// Generate conditional branch: if true goto body, else goto end
	CondBranchOp cond_branch;
	cond_branch.label_true = loop_body_label;
	cond_branch.label_false = loop_end_label;
	cond_branch.condition = toTypedValue(condition_result);
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
	popLoopSehDepth();
}

void AstToIr::visitDoWhileStatementNode(const DoWhileStatementNode& node) {
	// Generate unique labels for this do-while loop
	static size_t do_while_counter = 0;
	size_t current_do_while = do_while_counter++;

	// Use a single StringBuilder and commit each label before starting the next
	auto loop_start_label = StringTable::createStringHandle(StringBuilder().append("do_while_start_").append(current_do_while));
	auto loop_condition_label = StringTable::createStringHandle(StringBuilder().append("do_while_condition_").append(current_do_while));
	auto loop_end_label = StringTable::createStringHandle(StringBuilder().append("do_while_end_").append(current_do_while));

	// Mark loop begin for break/continue support
	// For do-while loops, continue jumps to condition check (not body start)
	pushLoopSehDepth();
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
	ExprResult condition_result = visitExpressionNode(node.get_condition().as<ExpressionNode>());
	// C++20 [stmt.do]: contextual bool conversion.
	condition_result = applyConditionBoolConversion(condition_result, node.get_condition(), Token());
	// C++20 [class.temporary]/4: flush temporaries bound in the condition full-expression.
	emitAndClearFullExpressionTempDestructors();

	// Generate conditional branch: if true goto start, else goto end
	CondBranchOp cond_branch;
	cond_branch.label_true = loop_start_label;
	cond_branch.label_false = loop_end_label;
	cond_branch.condition = toTypedValue(condition_result);
	ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

	// Loop end label
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

	// Mark loop end
	ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	popLoopSehDepth();
}

void AstToIr::visitSwitchStatementNode(const SwitchStatementNode& node) {
	// Generate unique labels for this switch statement
	static size_t switch_counter = 0;
	StringHandle default_label = StringTable::getOrInternStringHandle(StringBuilder().append("switch_default_").append(switch_counter));
	StringHandle switch_end_label = StringTable::getOrInternStringHandle(StringBuilder().append("switch_end_").append(switch_counter));
	switch_counter++;

	// Evaluate the switch condition
	ExprResult condition_result = visitExpressionNode(node.get_condition().as<ExpressionNode>());
	// C++20 [class.temporary]/4: flush temporaries bound in the condition full-expression.
	emitAndClearFullExpressionTempDestructors();

	// Get the condition type and value
	TypeCategory condition_type = condition_result.typeEnum();
	int condition_size = condition_result.size_in_bits.value;

	// Mark switch begin for break support (switch acts like a loop for break)
	// Continue is not allowed in switch, but break is
	pushLoopSehDepth();
	LoopBeginOp loop_begin;
	loop_begin.loop_start_label = switch_end_label;
	loop_begin.loop_end_label = switch_end_label;
	loop_begin.loop_increment_label = switch_end_label;
	ir_.addInstruction(IrInstruction(IrOpcode::LoopBegin, std::move(loop_begin), Token()));

	// Process the switch body to collect case labels
	auto body = node.get_body();
	if (!body.is<BlockNode>()) {
		throw InternalError("Switch body must be a BlockNode");
		return;
	}

	const BlockNode& block = body.as<BlockNode>();
	std::vector<std::pair<std::string_view, ASTNode>> case_labels;  // label name, case value
	bool has_default = false;  // First pass: generate labels and collect case values
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
		ExprResult case_value_result = visitExpressionNode(case_value_node.as<ExpressionNode>());

	// Compare condition with case value using Equal opcode
		TempVar cmp_result = var_counter.next();

	// Create typed BinaryOp for the Equal comparison
		BinaryOp bin_op{
			.lhs = makeTypedValue(condition_type, SizeInBits{static_cast<int>(condition_size)}, toIrValue(condition_result.value)),
			.rhs = makeTypedValue(case_value_result.typeEnum(), case_value_result.size_in_bits, toIrValue(case_value_result.value)),
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
		cond_branch.label_true = StringTable::getOrInternStringHandle(case_label);	   // Fall through to unconditional branch when TRUE
		cond_branch.label_false = StringTable::getOrInternStringHandle(next_check_label); // Jump to next check when FALSE
		cond_branch.condition = makeTypedValue(TypeCategory::Bool, SizeInBits{1}, cmp_result);
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
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(case_label)}, Token()));	  // Execute case statements
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
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = default_label}, Token()));	   // Execute default statements
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
	popLoopSehDepth();
}

void AstToIr::visitRangedForStatementNode(const RangedForStatementNode& node) {
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

	// C++11+ standard: the range expression is materialized into a hidden
	// `__range` binding before begin/end are formed. Reuse the original
	// identifier when available; otherwise synthesize a hidden variable so
	// member-access and temporary-producing expressions are evaluated once.
	if (!range_expr.is<ExpressionNode>()) {
		FLASH_LOG(Codegen, Error, "Range expression must be an expression");
		return;
	}

	auto& expr_variant = range_expr.as<ExpressionNode>();
	std::string_view range_name;
	const DeclarationNode* range_decl_ptr = nullptr;
	ASTNode range_object_expr = range_expr;

	if (std::holds_alternative<IdentifierNode>(expr_variant)) {
		const IdentifierNode& range_ident = std::get<IdentifierNode>(expr_variant);
		range_name = range_ident.name();

	// Look up the range object in the symbol table
		const std::optional<ASTNode> range_symbol = symbol_table.lookup(range_name);
		if (!range_symbol.has_value()) {
			FLASH_LOG(Codegen, Error, "Range object '", range_name, "' not found in symbol table");
			return;
		}

	// Extract the DeclarationNode from either DeclarationNode or VariableDeclarationNode
		if (range_symbol->is<DeclarationNode>()) {
			range_decl_ptr = &range_symbol->as<DeclarationNode>();
		} else if (range_symbol->is<VariableDeclarationNode>()) {
			range_decl_ptr = &range_symbol->as<VariableDeclarationNode>().declaration();
		} else {
			FLASH_LOG(Codegen, Error, "Range object '", range_name, "' is not a variable declaration");
			return;
		}
	} else {
		std::optional<TypeSpecifierNode> inferred_range_type;
		if (std::holds_alternative<MemberAccessNode>(expr_variant)) {
			const StructTypeInfo* resolved_struct_info = nullptr;
			const StructMember* resolved_member = nullptr;
			if (resolveMemberAccessType(std::get<MemberAccessNode>(expr_variant), resolved_struct_info, resolved_member) &&
				resolved_member) {
				inferred_range_type.emplace(
					resolved_member->type_index.withCategory(
						isIrStructType(toIrType(resolved_member->memberType()))
							? TypeCategory::Struct
							: resolved_member->memberType()),
					static_cast<int>(resolved_member->size * 8),
					Token(),
					CVQualifier::None,
					ReferenceQualifier::None);
				if (resolved_member->pointer_depth > 0) {
					inferred_range_type->add_pointer_levels(resolved_member->pointer_depth);
				}
				if (resolved_member->reference_qualifier != ReferenceQualifier::None) {
					inferred_range_type->set_reference_qualifier(resolved_member->reference_qualifier);
				}
			}
		}
		if (!inferred_range_type.has_value()) {
			if (!parser_) {
				FLASH_LOG(Codegen, Error, "Parser is required to infer non-identifier range expression types");
				return;
			}
			inferred_range_type = parser_->get_expression_type(range_expr);
		}
		if (!inferred_range_type.has_value()) {
			FLASH_LOG(Codegen, Error, "Could not infer type of non-identifier range expression");
			return;
		}

		StringBuilder sb_range;
		sb_range.append("__range_");
		sb_range.append(ranged_for_counter - 1);
		range_name = sb_range.commit();

		Token range_token(Token::Type::Identifier, range_name, 0, 0, 0);
		ASTNode range_type_node = ASTNode::emplace_node<TypeSpecifierNode>(*inferred_range_type);
		ASTNode range_decl_node = ASTNode::emplace_node<DeclarationNode>(range_type_node, range_token);
		ASTNode range_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(range_decl_node, range_expr);
		visit(range_var_decl_node);
		range_object_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(range_token));
		range_decl_ptr = &range_decl_node.as<DeclarationNode>();
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
	else if (isIrStructType(toIrType(range_type.type()))) {
		visitRangedForBeginEnd(node, range_object_expr, range_type, loop_start_label, loop_body_label,
							   loop_increment_label, loop_end_label, ranged_for_counter - 1);
	} else {
		FLASH_LOG(Codegen, Error, "Range expression must be an array or a type with begin()/end() methods");
		return;
	}
}

void AstToIr::visitRangedForArray(const RangedForStatementNode& node, std::string_view array_name,
								  const DeclarationNode& array_decl, StringHandle loop_start_label,
								  StringHandle loop_body_label, StringHandle loop_increment_label,
								  StringHandle loop_end_label, size_t counter) {
	auto loop_var_decl = node.get_loop_variable_decl();

	// Unified pointer-based approach: use begin/end pointers for arrays too
	// This is more efficient (no indexing multiplication) and matches what optimizing compilers do
	// For array: auto __begin = &array[0]; auto __end = &array[size]; for (; __begin != __end; ++__begin)

	// Get array size
	auto array_size_node = array_decl.array_size();
	if (!array_size_node.has_value() && array_decl.is_unsized_array()) {
	// For unsized arrays (int arr[] = {...}), infer size from the initializer
		auto symbol = symbol_table.lookup(array_name);
		if (symbol.has_value() && symbol->is<VariableDeclarationNode>()) {
			const auto& var_decl = symbol->as<VariableDeclarationNode>();
			if (var_decl.initializer().has_value() && var_decl.initializer()->is<InitializerListNode>()) {
				size_t inferred_size = var_decl.initializer()->as<InitializerListNode>().initializers().size();
				StringBuilder sb;
				sb.append(inferred_size);
				StringHandle size_str = StringTable::createStringHandle(sb);
				Token size_token(Token::Type::Literal, StringTable::getStringView(size_str), 0, 0, 0);
				array_size_node = ASTNode::emplace_node<ExpressionNode>(
					NumericLiteralNode(size_token, static_cast<unsigned long long>(inferred_size), TypeCategory::Int, TypeQualifier::None, 32));
			}
		}
	}
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
	} else if (array_type.category() == TypeCategory::Struct) {
	// Array of structs - lookup size from type info
		TypeIndex type_index = array_type.type_index();
		if (const TypeInfo* type_info = tryGetTypeInfo(type_index)) {
			const StructTypeInfo* struct_info = type_info->getStructInfo();
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
		array_type.type_index().withCategory(array_type.type()), element_size_bits, Token(), CVQualifier::None, ReferenceQualifier::None);
	begin_type_node.as<TypeSpecifierNode>().add_pointer_level();
	auto begin_decl_node = ASTNode::emplace_node<DeclarationNode>(begin_type_node, begin_token);

	auto end_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
		array_type.type_index().withCategory(array_type.type()), element_size_bits, Token(), CVQualifier::None, ReferenceQualifier::None);
	end_type_node.as<TypeSpecifierNode>().add_pointer_level();
	auto end_decl_node = ASTNode::emplace_node<DeclarationNode>(end_type_node, end_token);

	// Create begin = &array[0]
	auto array_expr_begin = ASTNode::emplace_node<ExpressionNode>(
		IdentifierNode(Token(Token::Type::Identifier, array_name, 0, 0, 0)));
	auto zero_literal = ASTNode::emplace_node<ExpressionNode>(
		NumericLiteralNode(Token(Token::Type::Literal, "0"sv, 0, 0, 0),
						   static_cast<unsigned long long>(0), TypeCategory::Int, TypeQualifier::None, 32));
	auto first_element = ASTNode::emplace_node<ExpressionNode>(
		ArraySubscriptNode(array_expr_begin, zero_literal, Token(Token::Type::Punctuator, "["sv, 0, 0, 0)));
	auto begin_init = ASTNode::emplace_node<ExpressionNode>(
		UnaryOperatorNode(Token(Token::Type::Operator, "&"sv, 0, 0, 0), first_element, true));
	auto begin_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(begin_decl_node, begin_init);
	visit(begin_var_decl_node);

	// Create end = &array[size] (one past the last element)
	auto array_expr_end = ASTNode::emplace_node<ExpressionNode>(
		IdentifierNode(Token(Token::Type::Identifier, array_name, 0, 0, 0)));
	auto past_end_element = ASTNode::emplace_node<ExpressionNode>(
		ArraySubscriptNode(array_expr_end, array_size_node.value(), Token(Token::Type::Punctuator, "["sv, 0, 0, 0)));
	auto end_init = ASTNode::emplace_node<ExpressionNode>(
		UnaryOperatorNode(Token(Token::Type::Operator, "&"sv, 0, 0, 0), past_end_element, true));
	auto end_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(end_decl_node, end_init);
	visit(end_var_decl_node);

	// Mark loop begin for break/continue support
	pushLoopSehDepth();
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
		BinaryOperatorNode(Token(Token::Type::Operator, "!="sv, 0, 0, 0), begin_ident_expr, end_ident_expr));
	ExprResult condition_result = visitExpressionNode(condition_expr.as<ExpressionNode>());

	// Generate conditional branch
	CondBranchOp cond_branch;
	cond_branch.label_true = loop_body_label;
	cond_branch.label_false = loop_end_label;
	cond_branch.condition = toTypedValue(condition_result);
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
	if (sema_) {
		loop_decl_node = sema_->normalizeRangedForLoopDecl(original_var_decl, array_type);
	} else if (isPlaceholderAutoType(original_var_decl.declaration().type_node().as<TypeSpecifierNode>().type())) {
		throw InternalError("Range-for placeholder loop variable reached array lowering without semantic normalization");
	}

	// C++20 standard: range-for desugars to `decl = *__begin;` for BOTH
	// value and reference loop variables. The iterator is always dereferenced.
	// For `int& c : arr`, this becomes `int& c = *__begin;`
	// For `int c : arr`,  this becomes `int c = *__begin;`
	auto begin_deref_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
	ASTNode init_expr = ASTNode::emplace_node<ExpressionNode>(
		UnaryOperatorNode(Token(Token::Type::Operator, "*"sv, 0, 0, 0), begin_deref_expr, true));

	symbol_table.enter_scope(ScopeType::Block);
	enterScope();
	ir_.addInstruction(IrOpcode::ScopeBegin, {}, Token());

	auto loop_var_with_init = ASTNode::emplace_node<VariableDeclarationNode>(loop_decl_node, init_expr);

	// Generate IR for loop variable declaration
	visit(loop_var_with_init);

	// Visit loop body - use visit() to properly handle block scopes
	auto body_stmt = node.get_body_statement();
	visit(body_stmt);

	// Exit the loop variable scope so destructors fire each iteration,
	// before the increment and branch-back.
	exitScope();
	ir_.addInstruction(IrOpcode::ScopeEnd, {}, Token());
	symbol_table.exit_scope();

	// Loop increment label (for continue statements)
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_increment_label}, Token()));

	// Increment pointer: ++__begin
	auto increment_begin = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
	auto increment_expr = ASTNode::emplace_node<ExpressionNode>(
		UnaryOperatorNode(Token(Token::Type::Operator, "++"sv, 0, 0, 0), increment_begin, true));
	visitExpressionNode(increment_expr.as<ExpressionNode>());

	// Branch back to loop start
	ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start_label}, Token()));

	// Loop end label
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

	// Mark loop end
	ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	popLoopSehDepth();
}

void AstToIr::visitRangedForBeginEnd(const RangedForStatementNode& node, ASTNode range_object_expr,
									 const TypeSpecifierNode& range_type, StringHandle loop_start_label,
									 StringHandle loop_body_label, StringHandle loop_increment_label,
									 StringHandle loop_end_label, size_t counter) {
	auto loop_var_decl = node.get_loop_variable_decl();

	// Get the struct type info
	if (range_type.type_index().index() >= getTypeInfoCount()) {
		FLASH_LOG(Codegen, Error, "Invalid type index for range expression");
		return;
	}

	const TypeInfo& type_info = getTypeInfo(range_type.type_index());
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
		begin_return_type.type_index().withCategory(begin_return_type.type()), begin_return_type.size_in_bits(), Token(), CVQualifier::None, ReferenceQualifier::None);
	begin_type_node.as<TypeSpecifierNode>().copy_indirection_from(begin_return_type);
	auto begin_decl_node = ASTNode::emplace_node<DeclarationNode>(begin_type_node, begin_token);

	auto end_type_node = ASTNode::emplace_node<TypeSpecifierNode>(
		begin_return_type.type_index().withCategory(begin_return_type.type()), begin_return_type.size_in_bits(), Token(), CVQualifier::None, ReferenceQualifier::None);
	end_type_node.as<TypeSpecifierNode>().copy_indirection_from(begin_return_type);
	auto end_decl_node = ASTNode::emplace_node<DeclarationNode>(end_type_node, end_token);

	// Create member function calls: range.begin() and range.end()
	ChunkedVector<ASTNode> empty_args;
	auto begin_call_expr = ASTNode::emplace_node<ExpressionNode>(
		MemberFunctionCallNode(range_object_expr,
							   begin_func_decl,
							   std::move(empty_args), Token()));

	auto begin_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(begin_decl_node, begin_call_expr);
	visit(begin_var_decl_node);

	// Similarly for end()
	const FunctionDeclarationNode& end_func_decl = end_func->function_decl.as<FunctionDeclarationNode>();
	ChunkedVector<ASTNode> empty_args2;
	auto end_call_expr = ASTNode::emplace_node<ExpressionNode>(
		MemberFunctionCallNode(range_object_expr,
							   end_func_decl,
							   std::move(empty_args2), Token()));

	auto end_var_decl_node = ASTNode::emplace_node<VariableDeclarationNode>(end_decl_node, end_call_expr);
	visit(end_var_decl_node);

	// Mark loop begin for break/continue support
	pushLoopSehDepth();
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
		BinaryOperatorNode(Token(Token::Type::Operator, "!="sv, 0, 0, 0), begin_ident_expr, end_ident_expr));
	ExprResult condition_result = visitExpressionNode(condition_expr.as<ExpressionNode>());

	// Generate conditional branch
	CondBranchOp cond_branch;
	cond_branch.label_true = loop_body_label;
	cond_branch.label_false = loop_end_label;
	cond_branch.condition = toTypedValue(condition_result);
	ir_.addInstruction(IrInstruction(IrOpcode::ConditionalBranch, std::move(cond_branch), Token()));

	// Loop body label
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_body_label}, Token()));

	// Declare and initialize the loop variable
	// For references (T& x or const T& x), we need the iterator value directly
	// For non-references (T x), we need the dereferenced value (*__begin)

	// Create the loop variable declaration with initialization
	if (!loop_var_decl.is<VariableDeclarationNode>()) {
		throw InternalError("loop_var_decl must be a VariableDeclarationNode");
		return;
	}
	const VariableDeclarationNode& original_var_decl = loop_var_decl.as<VariableDeclarationNode>();
	// For pointer-based iterators (e.g. int*), stripping one pointer level
	// gives the element type directly. For struct iterators, use the declared
	// operator*() return type as the range element type.
	ASTNode loop_decl_node = original_var_decl.declaration_node();
	if (sema_) {
		loop_decl_node = sema_->normalizeRangedForLoopDecl(
			original_var_decl,
			range_type,
			begin_return_type,
			node.resolved_dereference_function());
	} else if (isPlaceholderAutoType(original_var_decl.declaration().type_node().as<TypeSpecifierNode>().type())) {
		throw InternalError("Range-for placeholder loop variable reached iterator lowering without semantic normalization");
	}
	const DeclarationNode& loop_decl = loop_decl_node.as<DeclarationNode>();
	const TypeSpecifierNode& loop_type = loop_decl.type_node().as<TypeSpecifierNode>();

	// C++20 standard: range-for desugars to `decl = *__begin;` for BOTH
	// value and reference loop variables. The iterator is always dereferenced.
	// For struct iterators, reinterpret as pointer to element type, then dereference.
	ASTNode init_expr;
	if (begin_return_type.pointer_depth() > 0) {
		auto deref_begin_ident_expr = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
		auto loop_ptr_type = ASTNode::emplace_node<TypeSpecifierNode>(
			loop_type.type_index().withCategory(loop_type.type()), static_cast<int>(loop_type.size_in_bits()), Token(), CVQualifier::None, ReferenceQualifier::None);
	// Copy existing pointer depth (e.g., for `int*& p : arr`, loop_type is int* with depth=1)
		loop_ptr_type.as<TypeSpecifierNode>().add_pointer_levels(static_cast<int>(loop_type.pointer_depth()));
		loop_ptr_type.as<TypeSpecifierNode>().add_pointer_level();
		auto cast_expr = ASTNode::emplace_node<ExpressionNode>(
			ReinterpretCastNode(loop_ptr_type, deref_begin_ident_expr, Token(Token::Type::Keyword, "reinterpret_cast"sv, 0, 0, 0)));
		init_expr = ASTNode::emplace_node<ExpressionNode>(
			UnaryOperatorNode(Token(Token::Type::Operator, "*"sv, 0, 0, 0), cast_expr, true));
	} else {
		const bool prefer_const_deref = range_type.is_const() || begin_func->is_const();
		const FunctionDeclarationNode* dereference_func = node.resolved_dereference_function();
		if (!dereference_func && sema_) {
			dereference_func = sema_->resolveRangedForIteratorDereference(begin_return_type, prefer_const_deref);
		}
		if (!dereference_func) {
			throw InternalError("Range-for struct iterator missing operator*() during lowering");
		}
		ChunkedVector<ASTNode> dereference_args;
		ASTNode dereference_call = ASTNode::emplace_node<ExpressionNode>(
			MemberFunctionCallNode(
				ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token)),
				*dereference_func,
				std::move(dereference_args),
				Token(Token::Type::Identifier, "operator*"sv, 0, 0, 0)));
		const TypeSpecifierNode& dereference_return_type =
			dereference_func->decl_node().type_node().as<TypeSpecifierNode>();
		if (loop_type.reference_qualifier() == ReferenceQualifier::None &&
			(dereference_return_type.is_reference() || dereference_return_type.is_rvalue_reference())) {
			init_expr = ASTNode::emplace_node<ExpressionNode>(
				UnaryOperatorNode(Token(Token::Type::Operator, "*"sv, 0, 0, 0), dereference_call, true));
		} else {
			init_expr = dereference_call;
		}
	}

	symbol_table.enter_scope(ScopeType::Block);
	enterScope();
	ir_.addInstruction(IrOpcode::ScopeBegin, {}, Token());

	auto loop_var_with_init = ASTNode::emplace_node<VariableDeclarationNode>(loop_decl_node, init_expr);

	// Generate IR for loop variable declaration
	visit(loop_var_with_init);

	// Visit loop body - use visit() to properly handle block scopes
	auto body_stmt = node.get_body_statement();
	visit(body_stmt);

	// Exit the loop variable scope so destructors fire each iteration,
	// before the increment and branch-back.
	exitScope();
	ir_.addInstruction(IrOpcode::ScopeEnd, {}, Token());
	symbol_table.exit_scope();

	// Loop increment label (for continue statements)
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_increment_label}, Token()));

	// Increment iterator: ++__begin
	auto increment_begin = ASTNode::emplace_node<ExpressionNode>(IdentifierNode(begin_token));
	auto increment_expr = ASTNode::emplace_node<ExpressionNode>(
		UnaryOperatorNode(Token(Token::Type::Operator, "++"sv, 0, 0, 0), increment_begin, true));
	visitExpressionNode(increment_expr.as<ExpressionNode>());

	// Branch back to loop start
	ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = loop_start_label}, Token()));

	// Loop end label
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = loop_end_label}, Token()));

	// Mark loop end
	ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	popLoopSehDepth();
}

void AstToIr::visitBreakStatementNode(const BreakStatementNode& node) {
	// If inside __try/__finally within a loop, call __finally before breaking
	emitSehFinallyCallsBeforeBreakContinue(node.break_token());
	// Emit destructor calls for all local variables in scopes between the current
	// position and the enclosing loop scope before jumping to the loop exit.
	if (!loop_depth_stack_.empty()) {
		emitDestructorsForNonLocalExit(loop_depth_stack_.back().scope_depth);
	}
	// Generate Break IR instruction (no operands - uses loop context stack in IRConverter)
	ir_.addInstruction(IrOpcode::Break, {}, node.break_token());
}

void AstToIr::visitContinueStatementNode(const ContinueStatementNode& node) {
	// If inside __try/__finally within a loop, call __finally before continuing
	emitSehFinallyCallsBeforeBreakContinue(node.continue_token());
	// Emit destructor calls for all local variables in scopes between the current
	// position and the enclosing loop scope before jumping to the loop increment.
	if (!loop_depth_stack_.empty()) {
		emitDestructorsForNonLocalExit(loop_depth_stack_.back().scope_depth);
	}
	// Generate Continue IR instruction (no operands - uses loop context stack in IRConverter)
	ir_.addInstruction(IrOpcode::Continue, {}, node.continue_token());
}

void AstToIr::visitGotoStatementNode(const GotoStatementNode& node) {
	StringHandle target_label = StringTable::getOrInternStringHandle(node.label_name());
	// Emit destructors for any local variables in scopes exited by the jump.
	// prescanLabels() populated label_scope_depth_map_ for all labels in this function
	// so both forward and backward gotos are handled.
	auto it = label_scope_depth_map_.find(target_label);
	if (it != label_scope_depth_map_.end()) {
		emitDestructorsForNonLocalExit(it->second);
	}
	// Generate Branch IR instruction (unconditional jump)
	ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = target_label}, node.goto_token()));
}

void AstToIr::visitLabelStatementNode(const LabelStatementNode& node) {
	// Generate Label IR instruction with the label name
	std::string label_name(node.label_name());
	ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(label_name)}, node.label_token()));
}
