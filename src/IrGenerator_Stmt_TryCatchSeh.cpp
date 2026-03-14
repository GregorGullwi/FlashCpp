#include "Parser.h"
#include "IrGenerator.h"

void AstToIr::visitTryStatementNode(const TryStatementNode& node) {
	active_try_statement_depth_ += 1;
	auto pop_try_depth = [this]() {
		active_try_statement_depth_ -= 1;
	};

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

		StringBuilder handlers_end_sb;
		handlers_end_sb.append("__try_handlers_end_").append(current_try_id);
		std::string_view handlers_end_label = handlers_end_sb.commit();

		// Emit TryBegin marker
		ir_.addInstruction(IrInstruction(IrOpcode::TryBegin, BranchOp{.target_label = StringTable::getOrInternStringHandle(handlers_label)}, node.try_token()));

		// Enable capture of try-block-scope vars for Phase 1 cleanup.
		// Nested try blocks must preserve the outer capture state so outer-scope
		// destructible locals remain available to the outer landing pad.
		bool saved_capture_try_cleanup = capture_try_cleanup_;
		size_t saved_capture_try_cleanup_depth = capture_try_cleanup_depth_;
		std::vector<ScopeVariableInfo> saved_captured_try_cleanup_vars = std::move(captured_try_cleanup_vars_);
		captured_try_cleanup_vars_.clear();
		capture_try_cleanup_ = true;
		capture_try_cleanup_depth_ = scope_stack_.size() + 1;

		// Visit try block
		visit(node.try_block());

		// Disable capture and collect the vars — only converted to StringHandle pairs
		// when there is actually a first catch handler that needs them.
		capture_try_cleanup_ = false;
		std::vector<ScopeVariableInfo> try_scope_cleanup_vars = std::move(captured_try_cleanup_vars_);
		capture_try_cleanup_ = saved_capture_try_cleanup;
		capture_try_cleanup_depth_ = saved_capture_try_cleanup_depth;
		captured_try_cleanup_vars_ = std::move(saved_captured_try_cleanup_vars);
		std::vector<std::pair<StringHandle, StringHandle>> try_cleanup_vars;
		if (!try_scope_cleanup_vars.empty()) {
			for (const auto& var : try_scope_cleanup_vars) {
				try_cleanup_vars.push_back({
					StringTable::getOrInternStringHandle(var.struct_name),
					StringTable::getOrInternStringHandle(var.variable_name)
				});
			}
		}

		// Emit TryEnd marker
		ir_.addInstruction(IrOpcode::TryEnd, {}, node.try_token());

		// Jump to parent continuation on successful try block execution
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(end_label)}, node.try_token()));

		// Parent continuation label must remain in the parent runtime range.
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(end_label)}, node.try_token()));

		// Skip over out-of-line catch handlers during normal execution.
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(handlers_end_label)}, node.try_token()));

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
				catch_op.continuation_label = end_label;
				catch_op.is_const = type_node.is_const();
				catch_op.ref_qualifier = ((type_node.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((type_node.is_lvalue_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
				catch_op.is_catch_all = false;  // This is a typed catch, not catch(...)
				// Phase 1: first handler gets the cleanup vars for try-block-local destructors
				if (catch_index == 0) {
					catch_op.cleanup_vars = try_cleanup_vars;
				}
				ir_.addInstruction(IrInstruction(IrOpcode::CatchBegin, std::move(catch_op), catch_clause.catch_token()));

				// Add the exception variable to the symbol table for the catch block scope
				symbol_table.enter_scope(ScopeType::Block);

				// Register the exception parameter in the symbol table
				std::string_view exception_var_name = decl.identifier_token().value();
				if (!exception_var_name.empty()) {
					// Create a variable declaration for the exception parameter
					VariableDeclOp decl_op;
					decl_op.type = type_node.type();
					decl_op.size_in_bits = SizeInBits{type_node.size_in_bits()};
					decl_op.var_name = StringTable::getOrInternStringHandle(exception_var_name);

					// Create a TypedValue for the initializer
					TypedValue init_value;
					init_value.type = type_node.type();
					init_value.size_in_bits = SizeInBits{type_node.size_in_bits()};
					init_value.type_index = type_index;
					init_value.value = exception_temp;
					if (type_node.is_rvalue_reference()) {
						init_value.ref_qualifier = ReferenceQualifier::RValueReference;
					} else if (type_node.is_reference()) {
						init_value.ref_qualifier = ReferenceQualifier::LValueReference;
					}
					decl_op.initializer = init_value;
					decl_op.use_copy_constructor = !type_node.is_reference() &&
					                               type_node.type() == Type::Struct &&
					                               type_index.is_valid();

					decl_op.ref_qualifier = ((type_node.is_rvalue_reference() ? CVReferenceQualifier::RValueReference : ((type_node.is_reference()) ? CVReferenceQualifier::LValueReference : CVReferenceQualifier::None)));
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
				catch_op.continuation_label = end_label;
				catch_op.is_const = false;
				catch_op.ref_qualifier = CVReferenceQualifier::None;
				catch_op.is_catch_all = true;  // This IS catch(...)
				// Phase 1: first handler gets the cleanup vars for try-block-local destructors
				if (catch_index == 0) {
					catch_op.cleanup_vars = try_cleanup_vars;
				}
				ir_.addInstruction(IrOpcode::CatchBegin, std::move(catch_op), catch_clause.catch_token());
				symbol_table.enter_scope(ScopeType::Block);
			}

			// Visit catch block body
			catch_scope_stack_.push_back({scope_stack_.size(), active_try_statement_depth_});
			visit(catch_clause.body());
			catch_scope_stack_.pop_back();

			// Emit CatchEnd marker
			ir_.addInstruction(IrOpcode::CatchEnd, CatchEndOp{.continuation_label = end_label}, catch_clause.catch_token());

			// Exit catch block scope
			symbol_table.exit_scope();

			// Jump to end after catch block
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(end_label)}, catch_clause.catch_token()));

			// Emit catch end label
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(catch_end_label)}, catch_clause.catch_token()));
		}

		// End of out-of-line catch handlers; resume normal flow after try/catch.
		// Before the handlers_end_label, emit an ElfCatchNoMatch marker if any typed
		// (non-catch-all) handlers were present.  In the ELF code generator this
		// inserts "load exc_ptr; JMP cleanup_lp" so the exception propagates correctly
		// when no handler matched.  On Windows this is a no-op.
		bool has_typed_handlers = false;
		for (size_t i = 0; i < node.catch_clauses().size(); ++i) {
			if (!node.catch_clauses()[i].as<CatchClauseNode>().is_catch_all()) {
				has_typed_handlers = true;
				break;
			}
		}
		if (has_typed_handlers) {
			function_has_typed_catch_ = true;  // signal emitPendingFunctionCleanupLP
			ir_.addInstruction(IrInstruction(IrOpcode::ElfCatchNoMatch, ElfCatchNoMatchOp{}, node.try_token()));
		}
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(handlers_end_label)}, node.try_token()));
		pop_try_depth();
	}

	void AstToIr::visitThrowStatementNode(const ThrowStatementNode& node) {
		if (node.is_rethrow()) {
			emitActiveCatchScopeDestructors();
			// throw; (rethrow current exception)
			ir_.addInstruction(IrOpcode::Rethrow, {}, node.throw_token());
		} else {
			// throw expression;
			const auto& expr = *node.expression();

			// Generate code for the expression to throw
			ExprResult expr_result = visitExpressionNode(expr.as<ExpressionNode>());

			Type expr_type = expr_result.type;
			size_t type_size = static_cast<size_t>(expr_result.size_in_bits.value);

			// Extract TypeIndex from ExprResult (.type_index carries legacy slot-4 metadata)
			TypeIndex exception_type_index = expr_result.type_index;

			IrValue exception_value;
			bool is_rvalue = !std::holds_alternative<StringHandle>(expr_result.value);
			bool value_is_materialized = false;
			if (std::holds_alternative<TempVar>(expr_result.value)) {
				is_rvalue = !isTempVarLValue(std::get<TempVar>(expr_result.value));
			}

			if (std::holds_alternative<TempVar>(expr_result.value)) {
				exception_value = std::get<TempVar>(expr_result.value);
			} else if (std::holds_alternative<StringHandle>(expr_result.value)) {
				exception_value = std::get<StringHandle>(expr_result.value);
			} else if (std::holds_alternative<unsigned long long>(expr_result.value)) {
				exception_value = std::get<unsigned long long>(expr_result.value);
			} else if (std::holds_alternative<double>(expr_result.value)) {
				exception_value = std::get<double>(expr_result.value);
			} else {
				// Unknown operand type - log warning and default to zero value
				FLASH_LOG(Codegen, Warning, "Unknown operand type in throw expression, defaulting to zero");
				exception_value = static_cast<unsigned long long>(0);
			}

			if (!catch_scope_stack_.empty()) {
				bool needs_materialization =
					expr_type == Type::Struct &&
					!is_rvalue &&
					(std::holds_alternative<StringHandle>(exception_value) ||
					 (std::holds_alternative<TempVar>(exception_value) &&
					  isTempVarLValue(std::get<TempVar>(exception_value))));

				if (needs_materialization) {
					TempVar throw_storage_var = var_counter.next();
					StringBuilder temp_name_builder;
					temp_name_builder.append("__catch_throw_value_").append(throw_storage_var.var_number);
					StringHandle throw_storage_name = StringTable::getOrInternStringHandle(temp_name_builder.commit());

					VariableDeclOp materialized_throw_decl;
					materialized_throw_decl.type = expr_type;
					materialized_throw_decl.size_in_bits = SizeInBits{static_cast<int>(type_size)};
					materialized_throw_decl.var_name = throw_storage_name;
					materialized_throw_decl.use_copy_constructor = (exception_type_index.is_valid());

					TypedValue materialized_init;
					materialized_init.type = expr_type;
					materialized_init.size_in_bits = SizeInBits{static_cast<int>(type_size)};
					materialized_init.value = exception_value;
					materialized_init.type_index = exception_type_index;
					materialized_throw_decl.initializer = std::move(materialized_init);

					ir_.addInstruction(IrInstruction(IrOpcode::VariableDecl, std::move(materialized_throw_decl), node.throw_token()));
					exception_value = throw_storage_name;
					is_rvalue = false;
					value_is_materialized = true;
				}

				emitActiveCatchScopeDestructors();
			}

			// Create ThrowOp with typed data
			ThrowOp throw_op;
			throw_op.type_index = exception_type_index;
			throw_op.exception_type = expr_type;  // Store the actual Type enum
			throw_op.size_in_bytes = type_size / 8;  // Convert bits to bytes
			throw_op.is_rvalue = is_rvalue;
			throw_op.value_is_materialized = value_is_materialized;
			throw_op.exception_value = exception_value;

			ir_.addInstruction(IrInstruction(IrOpcode::Throw, std::move(throw_op), node.throw_token()));
		}
	}

	void AstToIr::visitSehTryExceptStatementNode(const SehTryExceptStatementNode& node) {
		// Generate __try/__except structure
		// __try { try_block } __except(filter) { except_block }

		// Generate unique labels using StringBuilder
		static size_t seh_try_counter = 0;
		size_t current_seh_id = seh_try_counter++;

		// Create labels
		StringBuilder except_sb;
		except_sb.append("__seh_except_").append(current_seh_id);
		std::string_view except_label = except_sb.commit();

		StringBuilder end_sb;
		end_sb.append("__seh_end_").append(current_seh_id);
		std::string_view end_label = end_sb.commit();

		StringBuilder except_end_sb;
		except_end_sb.append("__seh_except_end_").append(current_seh_id);
		std::string_view except_end_label = except_end_sb.commit();

		// Get the __except clause and check if filter is constant
		const auto& except_clause = node.except_clause().as<SehExceptClauseNode>();
		const auto& filter_expr = except_clause.filter_expression().as<SehFilterExpressionNode>();
		const auto& filter_inner_expr = filter_expr.expression().as<ExpressionNode>();

		// Detect constant filter: numeric literal or unary-minus on numeric literal
		bool is_constant_filter = false;
		int32_t constant_filter_value = 0;
		TempVar filter_result = var_counter.next();

		if (std::holds_alternative<NumericLiteralNode>(filter_inner_expr)) {
			is_constant_filter = true;
			const auto& lit = std::get<NumericLiteralNode>(filter_inner_expr);
			constant_filter_value = static_cast<int32_t>(std::get<unsigned long long>(lit.value()));
			FLASH_LOG(Codegen, Debug, "SEH filter is constant literal: ", constant_filter_value);
		} else if (std::holds_alternative<UnaryOperatorNode>(filter_inner_expr)) {
			const auto& unary = std::get<UnaryOperatorNode>(filter_inner_expr);
			if (unary.op() == "-" && unary.get_operand().is<ExpressionNode>()) {
				const auto& inner = unary.get_operand().as<ExpressionNode>();
				if (std::holds_alternative<NumericLiteralNode>(inner)) {
					is_constant_filter = true;
					const auto& lit = std::get<NumericLiteralNode>(inner);
					constant_filter_value = -static_cast<int32_t>(std::get<unsigned long long>(lit.value()));
					FLASH_LOG(Codegen, Debug, "SEH filter is constant negated literal: ", constant_filter_value);
				}
			}
		}

		if (is_constant_filter) {
			// For constant filters, evaluate the expression to emit any necessary IR
			// (this is just a constant load, harmless)
			visitExpressionNode(filter_inner_expr);
		}

		// Push SEH context for __leave statement resolution
		pushSehContext(end_label, std::string_view(), false);

		// Emit SehTryBegin marker
		ir_.addInstruction(IrInstruction(IrOpcode::SehTryBegin, BranchOp{.target_label = StringTable::getOrInternStringHandle(except_label)}, node.try_token()));

		// Visit __try block
		visit(node.try_block());

		// Emit SehTryEnd marker
		ir_.addInstruction(IrOpcode::SehTryEnd, {}, node.try_token());

		// Pop SEH context after __try block
		popSehContext();

		// Jump to end after successful __try block execution
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(end_label)}, node.try_token()));

		// Saved exception code var for GetExceptionCode() in __except body
		TempVar saved_exception_code_var;
		bool has_saved_exception_code_for_body = false;

		// For non-constant filters, emit a filter funclet between the try block and except handler
		if (!is_constant_filter) {
			StringBuilder filter_sb;
			filter_sb.append("__seh_filter_").append(current_seh_id);
			std::string_view filter_label = filter_sb.commit();

			// Emit filter funclet label
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(filter_label)}, except_clause.except_token()));

			// Emit SehFilterBegin marker (funclet prologue: saves RCX to [rsp+8], sets RBP from RDX)
			ir_.addInstruction(IrOpcode::SehFilterBegin, {}, except_clause.except_token());

			// Allocate a parent-frame slot to save ExceptionCode for use in __except body
			saved_exception_code_var = var_counter.next();
			has_saved_exception_code_for_body = true;
			SehSaveExceptionCodeOp save_op;
			save_op.saved_var = saved_exception_code_var;
			ir_.addInstruction(IrInstruction(IrOpcode::SehSaveExceptionCode, std::move(save_op), except_clause.except_token()));

			// Set filter funclet context so GetExceptionCode() uses the filter path (reads RCX)
			seh_in_filter_funclet_ = true;

			// Evaluate the filter expression inside the funclet
			// RBP points to parent frame, so local variable access works correctly
			ExprResult filter_result_expr = visitExpressionNode(filter_inner_expr);

			// Restore filter funclet context
			seh_in_filter_funclet_ = false;

			// Determine filter result - TempVar or constant
			SehFilterEndOp filter_end_op;
			if (std::holds_alternative<TempVar>(filter_result_expr.value)) {
				filter_result = std::get<TempVar>(filter_result_expr.value);
				filter_end_op.filter_result = filter_result;
				filter_end_op.is_constant_result = false;
				filter_end_op.constant_result = 0;
				FLASH_LOG(Codegen, Debug, "SEH filter is runtime expression, funclet filter_result=", filter_result.var_number);
			} else if (std::holds_alternative<unsigned long long>(filter_result_expr.value)) {
				// Filter expression returned a constant (e.g. comma expr ending in literal 1)
				filter_end_op.filter_result = filter_result;
				filter_end_op.is_constant_result = true;
				filter_end_op.constant_result = static_cast<int32_t>(std::get<unsigned long long>(filter_result_expr.value));
				FLASH_LOG(Codegen, Debug, "SEH filter funclet returns constant=", filter_end_op.constant_result);
			} else {
				filter_end_op.filter_result = filter_result;
				filter_end_op.is_constant_result = false;
				filter_end_op.constant_result = 0;
				FLASH_LOG(Codegen, Debug, "SEH filter: unknown result type, using default filter_result");
			}
			ir_.addInstruction(IrInstruction(IrOpcode::SehFilterEnd, std::move(filter_end_op), except_clause.except_token()));
		}

		// Emit label for __except handler entry
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(except_label)}, node.try_token()));

		// Emit SehExceptBegin marker with filter result
		SehExceptBeginOp except_op;
		except_op.filter_result = filter_result;
		except_op.is_constant_filter = is_constant_filter;
		except_op.constant_filter_value = constant_filter_value;
		except_op.except_end_label = except_end_label;
		ir_.addInstruction(IrInstruction(IrOpcode::SehExceptBegin, std::move(except_op), except_clause.except_token()));

		// Enter scope for __except block
		symbol_table.enter_scope(ScopeType::Block);

		// Set up GetExceptionCode() context for __except body, saving outer context for nesting
		bool outer_has_saved = seh_has_saved_exception_code_;
		TempVar outer_saved_var = seh_saved_exception_code_var_;
		if (has_saved_exception_code_for_body) {
			seh_has_saved_exception_code_ = true;
			seh_saved_exception_code_var_ = saved_exception_code_var;
		}

		// Visit __except block body
		visit(except_clause.body());

		// Restore outer GetExceptionCode() context
		seh_has_saved_exception_code_ = outer_has_saved;
		seh_saved_exception_code_var_ = outer_saved_var;

		// Emit SehExceptEnd marker
		ir_.addInstruction(IrOpcode::SehExceptEnd, {}, except_clause.except_token());

		// Exit __except block scope
		symbol_table.exit_scope();

		// Jump to end after __except block
		ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(end_label)}, except_clause.except_token()));

		// Emit except end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(except_end_label)}, except_clause.except_token()));

		// Emit end label
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(end_label)}, node.try_token()));
	}

	void AstToIr::visitSehTryFinallyStatementNode(const SehTryFinallyStatementNode& node) {
		// Generate __try/__finally structure
		// __try { try_block } __finally { finally_block }
		//
		// Control flow:
		// 1. Execute __try block
		// 2. On normal exit: jump to __finally handler
		// 3. Execute __finally handler
		// 4. Continue after SEH block

		// Generate unique labels using StringBuilder
		static size_t seh_finally_counter = 0;
		size_t current_seh_id = seh_finally_counter++;

		// Create labels
		StringBuilder finally_sb;
		finally_sb.append("__seh_finally_").append(current_seh_id);
		std::string_view finally_label = finally_sb.commit();

		StringBuilder end_sb;
		end_sb.append("__seh_finally_end_").append(current_seh_id);
		std::string_view end_label = end_sb.commit();

		// Push SEH context for __leave statement resolution
		pushSehContext(end_label, finally_label, true);

		// Emit SehTryBegin marker
		ir_.addInstruction(IrInstruction(IrOpcode::SehTryBegin, BranchOp{.target_label = StringTable::getOrInternStringHandle(finally_label)}, node.try_token()));

		// Visit __try block
		visit(node.try_block());

		// Emit SehTryEnd marker
		ir_.addInstruction(IrOpcode::SehTryEnd, {}, node.try_token());

		// Pop SEH context after __try block
		popSehContext();

		// Normal flow: call the __finally funclet then jump to end
		SehFinallyCallOp call_op;
		call_op.funclet_label = finally_label;
		call_op.end_label = end_label;
		ir_.addInstruction(IrInstruction(IrOpcode::SehFinallyCall, std::move(call_op), node.try_token()));

		// Emit label for __finally funclet entry point
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(finally_label)}, node.try_token()));

		// Get the __finally clause
		const auto& finally_clause = node.finally_clause().as<SehFinallyClauseNode>();

		// Emit SehFinallyBegin marker (sets up funclet prologue)
		ir_.addInstruction(IrOpcode::SehFinallyBegin, {}, finally_clause.finally_token());

		// Enter scope for __finally block
		symbol_table.enter_scope(ScopeType::Block);

		// Visit __finally block body
		visit(finally_clause.body());

		// Emit SehFinallyEnd marker (funclet epilogue + ret)
		ir_.addInstruction(IrOpcode::SehFinallyEnd, {}, finally_clause.finally_token());

		// Exit __finally block scope
		symbol_table.exit_scope();

		// Emit end label (execution continues here after SehFinallyCall)
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(end_label)}, node.try_token()));
	}

	void AstToIr::visitSehLeaveStatementNode(const SehLeaveStatementNode& node) {
		// Generate __leave statement
		// __leave jumps to the end of the current __try block
		// If the __try has a __finally, it calls the __finally funclet first

		const SehContext* seh_ctx = getCurrentSehContext();
		if (!seh_ctx) {
			FLASH_LOG(Codegen, Error, "__leave statement outside of __try block");
			assert(false && "__leave statement outside of __try block");
			return;
		}

		if (seh_ctx->has_finally) {
			// __leave inside __try/__finally: call the funclet then jump to end
			SehFinallyCallOp call_op;
			call_op.funclet_label = seh_ctx->finally_label;
			call_op.end_label = seh_ctx->try_end_label;
			ir_.addInstruction(IrInstruction(IrOpcode::SehFinallyCall, std::move(call_op), node.leave_token()));
		} else {
			// __leave inside __try/__except: just jump to end of __try block
			SehLeaveOp leave_op;
			leave_op.target_label = seh_ctx->try_end_label;
			ir_.addInstruction(IrInstruction(IrOpcode::SehLeave, std::move(leave_op), node.leave_token()));
		}
	}



// SEH context helper methods
void AstToIr::pushSehContext(std::string_view end_label, std::string_view finally_label, bool has_finally) {
	SehContext ctx;
	ctx.try_end_label = end_label;
	ctx.finally_label = finally_label;
	ctx.has_finally = has_finally;
	seh_context_stack_.push_back(ctx);
}



void AstToIr::popSehContext() {
	if (!seh_context_stack_.empty()) {
		seh_context_stack_.pop_back();
	}
}



const AstToIr::SehContext* AstToIr::getCurrentSehContext() const {
	if (seh_context_stack_.empty()) {
		return nullptr;
	}
	return &seh_context_stack_.back();
}



// Emit SehFinallyCall for all enclosing __try/__finally blocks before a return statement.
// Walks from innermost to outermost, calling each __finally funclet in order.
// Returns true if any finally calls were emitted.
bool AstToIr::emitSehFinallyCallsBeforeReturn(const Token& token) {
	bool emitted = false;
	// Walk from innermost (back) to outermost (front)
	for (int i = static_cast<int>(seh_context_stack_.size()) - 1; i >= 0; --i) {
		const SehContext& ctx = seh_context_stack_[i];
		if (ctx.has_finally) {
			// Generate a unique post-finally label for this return point
			static size_t seh_return_finally_counter = 0;
			size_t id = seh_return_finally_counter++;

			StringBuilder post_sb;
			post_sb.append("__seh_ret_finally_").append(id);
			std::string_view post_label = post_sb.commit();

			SehFinallyCallOp call_op;
			call_op.funclet_label = ctx.finally_label;
			call_op.end_label = post_label;
			ir_.addInstruction(IrInstruction(IrOpcode::SehFinallyCall, std::move(call_op), token));

			// Emit the post-finally label so execution continues here after the funclet returns
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(post_label)}, token));

			emitted = true;
		}
	}
	return emitted;
}



void AstToIr::popLoopSehDepth() {
	if (!loop_seh_depth_stack_.empty()) {
		loop_seh_depth_stack_.pop_back();
	}
}



// Emit SehFinallyCall for __try/__finally blocks between break/continue and the enclosing loop.
// Only calls finally blocks that were pushed AFTER the loop began (i.e., inside the loop body).
bool AstToIr::emitSehFinallyCallsBeforeBreakContinue(const Token& token) {
	if (loop_seh_depth_stack_.empty()) return false;

	size_t loop_seh_depth = loop_seh_depth_stack_.back();
	bool emitted = false;
	// Walk from innermost SEH context down to (but not including) the loop's entry depth
	for (int i = static_cast<int>(seh_context_stack_.size()) - 1; i >= static_cast<int>(loop_seh_depth); --i) {
		const SehContext& ctx = seh_context_stack_[i];
		if (ctx.has_finally) {
			static size_t seh_break_finally_counter = 0;
			size_t id = seh_break_finally_counter++;

			StringBuilder post_sb;
			post_sb.append("__seh_brk_finally_").append(id);
			std::string_view post_label = post_sb.commit();

			SehFinallyCallOp call_op;
			call_op.funclet_label = ctx.finally_label;
			call_op.end_label = post_label;
			ir_.addInstruction(IrInstruction(IrOpcode::SehFinallyCall, std::move(call_op), token));

			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(post_label)}, token));

			emitted = true;
		}
	}
	return emitted;
}
