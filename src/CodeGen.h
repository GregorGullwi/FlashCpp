#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "SymbolTable.h"
#include <type_traits>
#include <variant>
#include <vector>
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

	void visitVariableDeclarationNode(const ASTNode& ast_node) {
		const VariableDeclarationNode& node = ast_node.as<VariableDeclarationNode>();
		const auto& decl = node.declaration();
		const auto& type_node = decl.type_node().as<TypeSpecifierNode>();

		// Create variable declaration operands
		std::vector<IrOperand> operands;
		operands.emplace_back(type_node.type());
		operands.emplace_back(static_cast<int>(type_node.size_in_bits()));
		operands.emplace_back(decl.identifier_token().value());

		// Add initializer if present
		if (node.initializer()) {
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
	}

	std::vector<IrOperand>
		generateNumericLiteralIr(const NumericLiteralNode& numericLiteralNode) {
		// Generate IR for numeric literal using the actual type from the literal
		return { numericLiteralNode.type(), static_cast<int>(numericLiteralNode.sizeInBits()), std::get<unsigned long long>(numericLiteralNode.value()) };
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
		if (binaryOperatorNode.op() == "+") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatAdd, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::Add, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == "-") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatSubtract, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::Subtract, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == "*") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatMultiply, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::Multiply, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == "/") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatDivide, std::move(irOperands), binaryOperatorNode.get_token()));
			} else if (is_unsigned_integer_type(commonType)) {
				ir_.addInstruction(IrInstruction(IrOpcode::UnsignedDivide, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::Divide, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == "<<") {
			ir_.addInstruction(IrInstruction(IrOpcode::ShiftLeft, std::move(irOperands), binaryOperatorNode.get_token()));
		}
		else if (binaryOperatorNode.op() == ">>") {
			// Choose signed or unsigned right shift based on left operand type (after promotion)
			if (is_unsigned_integer_type(commonType)) {
				ir_.addInstruction(IrInstruction(IrOpcode::UnsignedShiftRight, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::ShiftRight, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == "&") {
			ir_.addInstruction(IrInstruction(IrOpcode::BitwiseAnd, std::move(irOperands), binaryOperatorNode.get_token()));
		}
		else if (binaryOperatorNode.op() == "|") {
			ir_.addInstruction(IrInstruction(IrOpcode::BitwiseOr, std::move(irOperands), binaryOperatorNode.get_token()));
		}
		else if (binaryOperatorNode.op() == "^") {
			ir_.addInstruction(IrInstruction(IrOpcode::BitwiseXor, std::move(irOperands), binaryOperatorNode.get_token()));
		}
		else if (binaryOperatorNode.op() == "%") {
			ir_.addInstruction(IrInstruction(IrOpcode::Modulo, std::move(irOperands), binaryOperatorNode.get_token()));
		}
		else if (binaryOperatorNode.op() == "==") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatEqual, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::Equal, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == "!=") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatNotEqual, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::NotEqual, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == "<") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatLessThan, std::move(irOperands), binaryOperatorNode.get_token()));
			} else if (is_unsigned_integer_type(commonType)) {
				ir_.addInstruction(IrInstruction(IrOpcode::UnsignedLessThan, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::LessThan, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == "<=") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatLessEqual, std::move(irOperands), binaryOperatorNode.get_token()));
			} else if (is_unsigned_integer_type(commonType)) {
				ir_.addInstruction(IrInstruction(IrOpcode::UnsignedLessEqual, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::LessEqual, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == ">") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatGreaterThan, std::move(irOperands), binaryOperatorNode.get_token()));
			} else if (is_unsigned_integer_type(commonType)) {
				ir_.addInstruction(IrInstruction(IrOpcode::UnsignedGreaterThan, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::GreaterThan, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == ">=") {
			if (is_floating_point_op) {
				ir_.addInstruction(IrInstruction(IrOpcode::FloatGreaterEqual, std::move(irOperands), binaryOperatorNode.get_token()));
			} else if (is_unsigned_integer_type(commonType)) {
				ir_.addInstruction(IrInstruction(IrOpcode::UnsignedGreaterEqual, std::move(irOperands), binaryOperatorNode.get_token()));
			} else {
				ir_.addInstruction(IrInstruction(IrOpcode::GreaterEqual, std::move(irOperands), binaryOperatorNode.get_token()));
			}
		}
		else if (binaryOperatorNode.op() == "&&") {
			ir_.addInstruction(IrInstruction(IrOpcode::LogicalAnd, std::move(irOperands), binaryOperatorNode.get_token()));
		}
		else if (binaryOperatorNode.op() == "||") {
			ir_.addInstruction(IrInstruction(IrOpcode::LogicalOr, std::move(irOperands), binaryOperatorNode.get_token()));
		}
		else {
			assert(false && "Unsupported binary operator");
			return {};
		}

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

	Ir ir_;
	TempVar var_counter{ 0 };
	SymbolTable symbol_table;
};
