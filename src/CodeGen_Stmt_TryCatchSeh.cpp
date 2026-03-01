#include "CodeGen.h"

	void AstToIr::visitTryStatementNode(const TryStatementNode& node) {
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

		// Visit try block
		visit(node.try_block());

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
				catch_op.continuation_label = end_label;
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
			ir_.addInstruction(IrOpcode::CatchEnd, CatchEndOp{.continuation_label = end_label}, catch_clause.catch_token());

			// Exit catch block scope
			symbol_table.exit_scope();

			// Jump to end after catch block
			ir_.addInstruction(IrInstruction(IrOpcode::Branch, BranchOp{.target_label = StringTable::getOrInternStringHandle(end_label)}, catch_clause.catch_token()));

			// Emit catch end label
			ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(catch_end_label)}, catch_clause.catch_token()));
		}

		// End of out-of-line catch handlers; resume normal flow after try/catch.
		ir_.addInstruction(IrInstruction(IrOpcode::Label, LabelOp{.label_name = StringTable::getOrInternStringHandle(handlers_end_label)}, node.try_token()));
	}

	void AstToIr::visitThrowStatementNode(const ThrowStatementNode& node) {
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
			auto filter_operands = visitExpressionNode(filter_inner_expr);

			// Restore filter funclet context
			seh_in_filter_funclet_ = false;

			// Determine filter result - TempVar or constant
			SehFilterEndOp filter_end_op;
			if (filter_operands.size() >= 3 && std::holds_alternative<TempVar>(filter_operands[2])) {
				filter_result = std::get<TempVar>(filter_operands[2]);
				filter_end_op.filter_result = filter_result;
				filter_end_op.is_constant_result = false;
				filter_end_op.constant_result = 0;
				FLASH_LOG(Codegen, Debug, "SEH filter is runtime expression, funclet filter_result=", filter_result.var_number);
			} else if (filter_operands.size() >= 3 && std::holds_alternative<unsigned long long>(filter_operands[2])) {
				// Filter expression returned a constant (e.g. comma expr ending in literal 1)
				filter_end_op.filter_result = filter_result;
				filter_end_op.is_constant_result = true;
				filter_end_op.constant_result = static_cast<int32_t>(std::get<unsigned long long>(filter_operands[2]));
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

