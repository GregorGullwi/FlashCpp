#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "SymbolTable.h"
#include <type_traits>
#include <variant>
#include <vector>
#include <unordered_map>
#include <assert.h>
#include "IRConverter.h"

class Parser;

class AstToIr {
public:
	AstToIr() {}

	void visit(const ASTNode& node) {
		if (node.is<FunctionDeclarationNode>()) {
			visitFunctionDeclarationNode(node.as<FunctionDeclarationNode>());
		}
		else if (node.is<ReturnStatementNode>()) {
			visitReturnStatementNode(node.as<ReturnStatementNode>());
		}
		else if (node.is<VariableDeclarationNode>()) {
			visitVariableDeclarationNode(node);
		}
		else if (node.is<IfStatementNode>()) {
			visitIfStatementNode(node.as<IfStatementNode>());
		}
		else if (node.is<ForStatementNode>()) {
			visitForStatementNode(node.as<ForStatementNode>());
		}
		else if (node.is<WhileStatementNode>()) {
			visitWhileStatementNode(node.as<WhileStatementNode>());
		}
		else if (node.is<DoWhileStatementNode>()) {
			visitDoWhileStatementNode(node.as<DoWhileStatementNode>());
		}
		else if (node.is<BreakStatementNode>()) {
			visitBreakStatementNode(node.as<BreakStatementNode>());
		}
		else if (node.is<ContinueStatementNode>()) {
			visitContinueStatementNode(node.as<ContinueStatementNode>());
		}
		else if (node.is<BlockNode>()) {
			visitBlockNode(node.as<BlockNode>());
		}
		else if (node.is<ExpressionNode>()) {
			visitExpressionNode(node.as<ExpressionNode>());
		}
		else {
			assert(false && "Unhandled AST node type");
		}
	}

	const Ir& getIr() const { return ir_; }

private:
	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
		auto definition_block = node.get_definition();
		if (!definition_block.has_value())
			return;

		// Reset the temporary variable counter for each new function
		var_counter = TempVar();

		const DeclarationNode& func_decl = node.decl_node();
		const TypeSpecifierNode& ret_type = func_decl.type_node().as<TypeSpecifierNode>();

		// Create function declaration with return type and name
		std::vector<IrOperand> funcDeclOperands;
		funcDeclOperands.emplace_back(ret_type.type());
		funcDeclOperands.emplace_back(static_cast<int>(ret_type.size_in_bits()));
		funcDeclOperands.emplace_back(func_decl.identifier_token().value());

		// Add parameter types to function declaration
		size_t paramCount = 0;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();
			
			// Add parameter type and size to function declaration
			funcDeclOperands.emplace_back(param_type.type());
			funcDeclOperands.emplace_back(static_cast<int>(param_type.size_in_bits()));
			funcDeclOperands.emplace_back(param_decl.identifier_token().value());
			
			paramCount++;
			var_counter.next();
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(funcDeclOperands), func_decl.identifier_token()));

		symbol_table.enter_scope(ScopeType::Function);

		// Allocate stack space for local variables and parameters
		// Parameters are already in their registers, we just need to allocate space for them
		size_t paramIndex = 0;
		for (const auto& param : node.parameter_nodes()) {
			const DeclarationNode& param_decl = param.as<DeclarationNode>();
			const TypeSpecifierNode& param_type = param_decl.type_node().as<TypeSpecifierNode>();

			symbol_table.insert(param_decl.identifier_token().value(), param);
			paramIndex++;
		}

		(*definition_block)->get_statements().visit([&](ASTNode statement) {
			visit(statement);
		});

		symbol_table.exit_scope();
	}

	void visitReturnStatementNode(const ReturnStatementNode& node) {
		if (node.expression()) {
			auto operands = visitExpressionNode(node.expression()->as<ExpressionNode>());
			// Add the return value's type and size information
			ir_.addInstruction(IrInstruction(IrOpcode::Return, std::move(operands), node.return_token()));
		}
		else {
			// For void returns, we don't need any operands
			ir_.addInstruction(IrOpcode::Return, {}, node.return_token());
		}
	}

	void visitBlockNode(const BlockNode& node) {
		// Visit all statements in the block
		node.get_statements().visit([&](const ASTNode& statement) {
			visit(statement);
		});
	}

	void visitIfStatementNode(const IfStatementNode& node) {
		// Generate unique labels for this if statement
		static size_t if_counter = 0;
		std::string then_label = "if_then_" + std::to_string(if_counter);
		std::string else_label = "if_else_" + std::to_string(if_counter);
		std::string end_label = "if_end_" + std::to_string(if_counter);
		if_counter++;

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
		std::vector<IrOperand> branch_operands;
		branch_operands.insert(branch_operands.end(), condition_operands.begin(), condition_operands.end());
		branch_operands.emplace_back(then_label);
		branch_operands.emplace_back(node.has_else() ? else_label : end_label);
		ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());

		// Then block
		ir_.addInstruction(IrOpcode::Label, {then_label}, Token());

		// Visit then statement
		auto then_stmt = node.get_then_statement();
		if (then_stmt.is<BlockNode>()) {
			then_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		} else {
			visit(then_stmt);
		}

		// Branch to end after then block (skip else)
		if (node.has_else()) {
			ir_.addInstruction(IrOpcode::Branch, {end_label}, Token());
		}

		// Else block (if present)
		if (node.has_else()) {
			ir_.addInstruction(IrOpcode::Label, {else_label}, Token());

			auto else_stmt = node.get_else_statement();
			if (else_stmt.has_value()) {
				if (else_stmt->is<BlockNode>()) {
					else_stmt->as<BlockNode>().get_statements().visit([&](ASTNode statement) {
						visit(statement);
					});
				} else {
					visit(*else_stmt);
				}
			}
		}

		// End label
		ir_.addInstruction(IrOpcode::Label, {end_label}, Token());
	}

	void visitForStatementNode(const ForStatementNode& node) {
		// Generate unique labels for this for loop
		static size_t for_counter = 0;
		std::string loop_start_label = "for_start_" + std::to_string(for_counter);
		std::string loop_body_label = "for_body_" + std::to_string(for_counter);
		std::string loop_increment_label = "for_increment_" + std::to_string(for_counter);
		std::string loop_end_label = "for_end_" + std::to_string(for_counter);
		for_counter++;

		// Execute init statement (if present)
		if (node.has_init()) {
			auto init_stmt = node.get_init_statement();
			if (init_stmt.has_value()) {
				visit(*init_stmt);
			}
		}

		// Mark loop begin for break/continue support
		ir_.addInstruction(IrOpcode::LoopBegin, {loop_start_label, loop_end_label, loop_increment_label}, Token());

		// Loop start: evaluate condition
		ir_.addInstruction(IrOpcode::Label, {loop_start_label}, Token());

		// Evaluate condition (if present, otherwise infinite loop)
		if (node.has_condition()) {
			auto condition_operands = visitExpressionNode(node.get_condition()->as<ExpressionNode>());

			// Generate conditional branch: if true goto body, else goto end
			std::vector<IrOperand> branch_operands;
			branch_operands.insert(branch_operands.end(), condition_operands.begin(), condition_operands.end());
			branch_operands.emplace_back(loop_body_label);
			branch_operands.emplace_back(loop_end_label);
			ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());
		}

		// Loop body label
		ir_.addInstruction(IrOpcode::Label, {loop_body_label}, Token());

		// Visit loop body
		auto body_stmt = node.get_body_statement();
		if (body_stmt.is<BlockNode>()) {
			body_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		} else {
			visit(body_stmt);
		}

		// Loop increment label (for continue statements)
		ir_.addInstruction(IrOpcode::Label, {loop_increment_label}, Token());

		// Execute update/increment expression (if present)
		if (node.has_update()) {
			visitExpressionNode(node.get_update_expression()->as<ExpressionNode>());
		}

		// Branch back to loop start
		ir_.addInstruction(IrOpcode::Branch, {loop_start_label}, Token());

		// Loop end label
		ir_.addInstruction(IrOpcode::Label, {loop_end_label}, Token());

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitWhileStatementNode(const WhileStatementNode& node) {
		// Generate unique labels for this while loop
		static size_t while_counter = 0;
		std::string loop_start_label = "while_start_" + std::to_string(while_counter);
		std::string loop_body_label = "while_body_" + std::to_string(while_counter);
		std::string loop_end_label = "while_end_" + std::to_string(while_counter);
		while_counter++;

		// Mark loop begin for break/continue support
		// For while loops, continue jumps to loop_start (re-evaluate condition)
		ir_.addInstruction(IrOpcode::LoopBegin, {loop_start_label, loop_end_label, loop_start_label}, Token());

		// Loop start: evaluate condition
		ir_.addInstruction(IrOpcode::Label, {loop_start_label}, Token());

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch: if true goto body, else goto end
		std::vector<IrOperand> branch_operands;
		branch_operands.insert(branch_operands.end(), condition_operands.begin(), condition_operands.end());
		branch_operands.emplace_back(loop_body_label);
		branch_operands.emplace_back(loop_end_label);
		ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());

		// Loop body label
		ir_.addInstruction(IrOpcode::Label, {loop_body_label}, Token());

		// Visit loop body
		auto body_stmt = node.get_body_statement();
		if (body_stmt.is<BlockNode>()) {
			body_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		} else {
			visit(body_stmt);
		}

		// Branch back to loop start (re-evaluate condition)
		ir_.addInstruction(IrOpcode::Branch, {loop_start_label}, Token());

		// Loop end label
		ir_.addInstruction(IrOpcode::Label, {loop_end_label}, Token());

		// Mark loop end
		ir_.addInstruction(IrOpcode::LoopEnd, {}, Token());
	}

	void visitDoWhileStatementNode(const DoWhileStatementNode& node) {
		// Generate unique labels for this do-while loop
		static size_t do_while_counter = 0;
		std::string loop_start_label = "do_while_start_" + std::to_string(do_while_counter);
		std::string loop_condition_label = "do_while_condition_" + std::to_string(do_while_counter);
		std::string loop_end_label = "do_while_end_" + std::to_string(do_while_counter);
		do_while_counter++;

		// Mark loop begin for break/continue support
		// For do-while loops, continue jumps to condition check (not body start)
		ir_.addInstruction(IrOpcode::LoopBegin, {loop_start_label, loop_end_label, loop_condition_label}, Token());

		// Loop start: execute body first (do-while always executes at least once)
		ir_.addInstruction(IrOpcode::Label, {loop_start_label}, Token());

		// Visit loop body
		auto body_stmt = node.get_body_statement();
		if (body_stmt.is<BlockNode>()) {
			body_stmt.as<BlockNode>().get_statements().visit([&](ASTNode statement) {
				visit(statement);
			});
		} else {
			visit(body_stmt);
		}

		// Condition check label (for continue statements)
		ir_.addInstruction(IrOpcode::Label, {loop_condition_label}, Token());

		// Evaluate condition
		auto condition_operands = visitExpressionNode(node.get_condition().as<ExpressionNode>());

		// Generate conditional branch: if true goto start, else goto end
		std::vector<IrOperand> branch_operands;
		branch_operands.insert(branch_operands.end(), condition_operands.begin(), condition_operands.end());
		branch_operands.emplace_back(loop_start_label);
		branch_operands.emplace_back(loop_end_label);
		ir_.addInstruction(IrOpcode::ConditionalBranch, std::move(branch_operands), Token());

		// Loop end label
		ir_.addInstruction(IrOpcode::Label, {loop_end_label}, Token());

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

	void visitVariableDeclarationNode(const ASTNode& ast_node) {
		const VariableDeclarationNode& node = ast_node.as<VariableDeclarationNode>();
		const auto& decl = node.declaration();
		const auto& type_node = decl.type_node().as<TypeSpecifierNode>();

		// Create variable declaration operands
		std::vector<IrOperand> operands;
		operands.emplace_back(type_node.type());
		operands.emplace_back(static_cast<int>(type_node.size_in_bits()));
		operands.emplace_back(decl.identifier_token().value());

		// For arrays, add the array size
		if (decl.is_array()) {
			auto size_expr = decl.array_size();
			if (size_expr.has_value()) {
				auto size_operands = visitExpressionNode(size_expr->as<ExpressionNode>());
				// Add array size as an operand
				operands.insert(operands.end(), size_operands.begin(), size_operands.end());
			}
		}

		// Add initializer if present (for non-arrays)
		if (node.initializer() && !decl.is_array()) {
			auto init_operands = visitExpressionNode(node.initializer()->as<ExpressionNode>());
			operands.insert(operands.end(), init_operands.begin(), init_operands.end());
		}

		if (!symbol_table.insert(decl.identifier_token().value(), node.declaration_node())) {
			assert(false && "Expected identifier to be unique");
		}

		ir_.addInstruction(IrOpcode::VariableDecl, std::move(operands), node.declaration().identifier_token());
	}

	std::vector<IrOperand> visitExpressionNode(const ExpressionNode& exprNode) {
		if (std::holds_alternative<IdentifierNode>(exprNode)) {
			const auto& expr = std::get<IdentifierNode>(exprNode);
			return generateIdentifierIr(expr);
		}
		else if (std::holds_alternative<NumericLiteralNode>(exprNode)) {
			const auto& expr = std::get<NumericLiteralNode>(exprNode);
			return generateNumericLiteralIr(expr);
		}
		else if (std::holds_alternative<StringLiteralNode>(exprNode)) {
			const auto& expr = std::get<StringLiteralNode>(exprNode);
			return generateStringLiteralIr(expr);
		}
		else if (std::holds_alternative<BinaryOperatorNode>(exprNode)) {
			const auto& expr = std::get<BinaryOperatorNode>(exprNode);
			return generateBinaryOperatorIr(expr);
		}
		else if (std::holds_alternative<UnaryOperatorNode>(exprNode)) {
			const auto& expr = std::get<UnaryOperatorNode>(exprNode);
			return generateUnaryOperatorIr(expr);
		}
		else if (std::holds_alternative<FunctionCallNode>(exprNode)) {
			const auto& expr = std::get<FunctionCallNode>(exprNode);
			return generateFunctionCallIr(expr);
		}
		else if (std::holds_alternative<ArraySubscriptNode>(exprNode)) {
			const auto& expr = std::get<ArraySubscriptNode>(exprNode);
			return generateArraySubscriptIr(expr);
		}
		else {
			assert(false && "Not implemented yet");
		}

		return {};
	}

	std::vector<IrOperand> generateIdentifierIr(const IdentifierNode& identifierNode) {
		const std::optional<ASTNode> symbol = symbol_table.lookup(identifierNode.name());
		if (!symbol.has_value()) {
			assert(false && "Expected symbol to exist");
			return {};
		}

		if (symbol->is<DeclarationNode>()) {
			const auto& decl_node = symbol->as<DeclarationNode>();
			const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
			return { type_node.type(), static_cast<int>(type_node.size_in_bits()), identifierNode.name() };
		}

		// If we get here, the symbol is not a DeclarationNode
		assert(false && "Identifier is not a DeclarationNode");
		return {};
	}

	std::vector<IrOperand>
		generateNumericLiteralIr(const NumericLiteralNode& numericLiteralNode) {
		// Generate IR for numeric literal using the actual type from the literal
		// Check if it's a floating-point type
		if (is_floating_point_type(numericLiteralNode.type())) {
			// For floating-point literals, the value is stored as double
			return { numericLiteralNode.type(), static_cast<int>(numericLiteralNode.sizeInBits()), std::get<double>(numericLiteralNode.value()) };
		} else {
			// For integer literals, the value is stored as unsigned long long
			return { numericLiteralNode.type(), static_cast<int>(numericLiteralNode.sizeInBits()), std::get<unsigned long long>(numericLiteralNode.value()) };
		}
	}

	std::vector<IrOperand> generateTypeConversion(const std::vector<IrOperand>& operands, Type fromType, Type toType, const Token& source_token) {
		if (fromType == toType) {
			return operands; // No conversion needed
		}

		int fromSize = get_type_size_bits(fromType);
		int toSize = get_type_size_bits(toType);

		TempVar resultVar = var_counter.next();
		std::vector<IrOperand> conversionOperands;
		conversionOperands.push_back(resultVar);
		conversionOperands.push_back(fromType);
		conversionOperands.push_back(fromSize);
		conversionOperands.insert(conversionOperands.end(), operands.begin() + 2, operands.end()); // Skip type and size, add value
		conversionOperands.push_back(toSize);

		if (fromSize < toSize) {
			// Extension needed
			if (is_signed_integer_type(fromType)) {
				ir_.addInstruction(IrInstruction(IrOpcode::SignExtend, std::move(conversionOperands), source_token));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::ZeroExtend, std::move(conversionOperands), source_token));
			}
		} else if (fromSize > toSize) {
			// Truncation needed
			ir_.addInstruction(IrInstruction(IrOpcode::Truncate, std::move(conversionOperands), source_token));
		}

		// Return the converted operands
		return { toType, toSize, resultVar };
	}

	std::vector<IrOperand>
		generateStringLiteralIr(const StringLiteralNode& stringLiteralNode) {
		// Generate IR for string literal and return appropriate operand
		// ...
		return { stringLiteralNode.value() };
	}

	std::vector<IrOperand> generateUnaryOperatorIr(const UnaryOperatorNode& unaryOperatorNode) {
		std::vector<IrOperand> irOperands;

		// Generate IR for the operand
		auto operandIrOperands = visitExpressionNode(unaryOperatorNode.get_operand().as<ExpressionNode>());

		// Get the type of the operand
		Type operandType = std::get<Type>(operandIrOperands[0]);

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Add the result variable first
		irOperands.emplace_back(result_var);

		// Add the operand
		irOperands.insert(irOperands.end(), operandIrOperands.begin(), operandIrOperands.end());

		// Generate the IR for the operation based on the operator
		if (unaryOperatorNode.op() == "!") {
			// Logical NOT
			ir_.addInstruction(IrInstruction(IrOpcode::LogicalNot, std::move(irOperands), Token()));
		}
		else if (unaryOperatorNode.op() == "~") {
			// Bitwise NOT
			ir_.addInstruction(IrInstruction(IrOpcode::BitwiseNot, std::move(irOperands), Token()));
		}
		else if (unaryOperatorNode.op() == "-") {
			// Unary minus (negation)
			ir_.addInstruction(IrInstruction(IrOpcode::Negate, std::move(irOperands), Token()));
		}
		else if (unaryOperatorNode.op() == "+") {
			// Unary plus (no-op, just return the operand)
			return operandIrOperands;
		}
		else if (unaryOperatorNode.op() == "++") {
			// Increment operator (prefix or postfix)
			if (unaryOperatorNode.is_prefix()) {
				// Prefix increment: ++x
				ir_.addInstruction(IrInstruction(IrOpcode::PreIncrement, std::move(irOperands), Token()));
			} else {
				// Postfix increment: x++
				ir_.addInstruction(IrInstruction(IrOpcode::PostIncrement, std::move(irOperands), Token()));
			}
		}
		else if (unaryOperatorNode.op() == "--") {
			// Decrement operator (prefix or postfix)
			if (unaryOperatorNode.is_prefix()) {
				// Prefix decrement: --x
				ir_.addInstruction(IrInstruction(IrOpcode::PreDecrement, std::move(irOperands), Token()));
			} else {
				// Postfix decrement: x--
				ir_.addInstruction(IrInstruction(IrOpcode::PostDecrement, std::move(irOperands), Token()));
			}
		}
		else {
			assert(false && "Unary operator not implemented yet");
		}

		// Return the result
		return { operandType, std::get<int>(operandIrOperands[1]), result_var };
	}

	std::vector<IrOperand> generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode) {
		std::vector<IrOperand> irOperands;

		// Generate IR for the left-hand side and right-hand side of the operation
		auto lhsIrOperands = visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>());
		auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

		// Get the types of the operands
		Type lhsType = std::get<Type>(lhsIrOperands[0]);
		Type rhsType = std::get<Type>(rhsIrOperands[0]);

		// Apply integer promotions and find common type
		Type commonType = get_common_type(lhsType, rhsType);

		// Generate conversions if needed
		if (lhsType != commonType) {
			lhsIrOperands = generateTypeConversion(lhsIrOperands, lhsType, commonType, binaryOperatorNode.get_token());
		}
		if (rhsType != commonType) {
			rhsIrOperands = generateTypeConversion(rhsIrOperands, rhsType, commonType, binaryOperatorNode.get_token());
		}

		// Check if we're dealing with floating-point operations
		bool is_floating_point_op = is_floating_point_type(commonType);

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Add the result variable first
		irOperands.emplace_back(result_var);

		// Add the left-hand side operands
		irOperands.insert(irOperands.end(), lhsIrOperands.begin(), lhsIrOperands.end());

		// Add the right-hand side operands
		irOperands.insert(irOperands.end(), rhsIrOperands.begin(), rhsIrOperands.end());

		// Generate the IR for the operation based on the operator and operand types
		// Use a lookup table approach for better performance and maintainability
		IrOpcode opcode;
		const auto& op = binaryOperatorNode.op();

		// Simple operators (no type variants)
		static const std::unordered_map<std::string_view, IrOpcode> simple_ops = {
			{"&", IrOpcode::BitwiseAnd}, {"|", IrOpcode::BitwiseOr}, {"^", IrOpcode::BitwiseXor},
			{"%", IrOpcode::Modulo}, {"<<", IrOpcode::ShiftLeft},
			{"&&", IrOpcode::LogicalAnd}, {"||", IrOpcode::LogicalOr},
			{"=", IrOpcode::Assignment}, {"+=", IrOpcode::AddAssign}, {"-=", IrOpcode::SubAssign},
			{"*=", IrOpcode::MulAssign}, {"/=", IrOpcode::DivAssign}, {"%=", IrOpcode::ModAssign},
			{"&=", IrOpcode::AndAssign}, {"|=", IrOpcode::OrAssign}, {"^=", IrOpcode::XorAssign},
			{"<<=", IrOpcode::ShlAssign}, {">>=", IrOpcode::ShrAssign}
		};

		auto simple_it = simple_ops.find(op);
		if (simple_it != simple_ops.end()) {
			opcode = simple_it->second;
		}
		// Arithmetic operators (float vs int)
		else if (op == "+") {
			opcode = is_floating_point_op ? IrOpcode::FloatAdd : IrOpcode::Add;
		}
		else if (op == "-") {
			opcode = is_floating_point_op ? IrOpcode::FloatSubtract : IrOpcode::Subtract;
		}
		else if (op == "*") {
			opcode = is_floating_point_op ? IrOpcode::FloatMultiply : IrOpcode::Multiply;
		}
		// Division (float vs unsigned vs signed)
		else if (op == "/") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatDivide;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedDivide;
			} else {
				opcode = IrOpcode::Divide;
			}
		}
		// Right shift (unsigned vs signed)
		else if (op == ">>") {
			opcode = is_unsigned_integer_type(commonType) ? IrOpcode::UnsignedShiftRight : IrOpcode::ShiftRight;
		}
		// Comparison operators (float vs unsigned vs signed)
		else if (op == "==") {
			opcode = is_floating_point_op ? IrOpcode::FloatEqual : IrOpcode::Equal;
		}
		else if (op == "!=") {
			opcode = is_floating_point_op ? IrOpcode::FloatNotEqual : IrOpcode::NotEqual;
		}
		else if (op == "<") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatLessThan;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedLessThan;
			} else {
				opcode = IrOpcode::LessThan;
			}
		}
		else if (op == "<=") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatLessEqual;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedLessEqual;
			} else {
				opcode = IrOpcode::LessEqual;
			}
		}
		else if (op == ">") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatGreaterThan;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedGreaterThan;
			} else {
				opcode = IrOpcode::GreaterThan;
			}
		}
		else if (op == ">=") {
			if (is_floating_point_op) {
				opcode = IrOpcode::FloatGreaterEqual;
			} else if (is_unsigned_integer_type(commonType)) {
				opcode = IrOpcode::UnsignedGreaterEqual;
			} else {
				opcode = IrOpcode::GreaterEqual;
			}
		}
		else {
			assert(false && "Unsupported binary operator");
			return {};
		}

		ir_.addInstruction(IrInstruction(opcode, std::move(irOperands), binaryOperatorNode.get_token()));

		// Return the result variable with its type and size
		return { lhsIrOperands[0], lhsIrOperands[1], result_var };
	}

	std::vector<IrOperand> generateFunctionCallIr(const FunctionCallNode& functionCallNode) {
		std::vector<IrOperand> irOperands;

		const auto& decl_node = functionCallNode.function_declaration();
		auto type = gSymbolTable.lookup(decl_node.identifier_token().value());

		// Always add the return variable and function name
		TempVar ret_var = var_counter.next();
		irOperands.emplace_back(ret_var);
		irOperands.emplace_back(decl_node.identifier_token().value());

		// Generate IR for function arguments
		functionCallNode.arguments().visit([&](ASTNode argument) {
			auto argumentIrOperands = visitExpressionNode(argument.as<ExpressionNode>());
			// For variables, we need to add the type and size
			if (std::holds_alternative<IdentifierNode>(argument.as<ExpressionNode>())) {
				const auto& identifier = std::get<IdentifierNode>(argument.as<ExpressionNode>());
				const std::optional<ASTNode> symbol = symbol_table.lookup(identifier.name());
				const auto& decl_node = symbol->as<DeclarationNode>();
				const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
				irOperands.emplace_back(type_node.type());
				irOperands.emplace_back(static_cast<int>(type_node.size_in_bits()));
				irOperands.emplace_back(identifier.name());
			}
			else {
				irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
			}
		});

		// Add the function call instruction
		ir_.addInstruction(IrInstruction(IrOpcode::FunctionCall, std::move(irOperands), functionCallNode.called_from()));

		// Return the result variable with its type and size
		const auto& return_type = decl_node.type_node().as<TypeSpecifierNode>();
		return { return_type.type(), static_cast<int>(return_type.size_in_bits()), ret_var };
	}

	std::vector<IrOperand> generateArraySubscriptIr(const ArraySubscriptNode& arraySubscriptNode) {
		// Generate IR for array[index] expression
		// This computes the address: base_address + (index * element_size)

		// Get the array expression (should be an identifier for now)
		auto array_operands = visitExpressionNode(arraySubscriptNode.array_expr().as<ExpressionNode>());

		// Get the index expression
		auto index_operands = visitExpressionNode(arraySubscriptNode.index_expr().as<ExpressionNode>());

		// Get array type information
		Type array_type = std::get<Type>(array_operands[0]);
		int element_size_bits = std::get<int>(array_operands[1]);

		// Create a temporary variable for the result
		TempVar result_var = var_counter.next();

		// Build operands for array access IR instruction
		// Format: [result_var, array_type, element_size, array_name/temp, index_type, index_size, index_value]
		std::vector<IrOperand> irOperands;
		irOperands.emplace_back(result_var);

		// Array operands (type, size, name/temp)
		irOperands.insert(irOperands.end(), array_operands.begin(), array_operands.end());

		// Index operands (type, size, value)
		irOperands.insert(irOperands.end(), index_operands.begin(), index_operands.end());

		// For now, we'll use a Load-like instruction to read from the computed address
		// The IRConverter will handle the address calculation
		// We'll add a new IR opcode for this: ArrayAccess
		ir_.addInstruction(IrInstruction(IrOpcode::ArrayAccess, std::move(irOperands), arraySubscriptNode.bracket_token()));

		// Return the result with the element type
		return { array_type, element_size_bits, result_var };
	}

	Ir ir_;
	TempVar var_counter{ 0 };
	SymbolTable symbol_table;
};
