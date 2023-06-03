#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include <type_traits>
#include <variant>
#include <vector>

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
	}

	const Ir& getIr() const { return ir_; }

private:
	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
		const DeclarationNode& func_decl = node.return_type_handle().as<DeclarationNode>();
		const TypeSpecifierNode& ret_type = func_decl.type_handle().as<TypeSpecifierNode>();
		ir_.addInstruction(
			IrInstruction(IrOpcode::FunctionDecl,
				{ ret_type.type(),
				  static_cast<int>(ret_type.size_in_bits()),
				  func_decl.identifier_token().value() }));
		/*for (const auto &statement : node.getStatements()) {
		  visit(statement);
		}*/
	}

	void visitReturnStatementNode(const ReturnStatementNode& node) {
		if (node.expression()) {
			auto operands = generateExpressionIr(node.expression()->as<ExpressionNode>());
			ir_.addInstruction(IrOpcode::Return, std::move(operands));
		}
		else {
			ir_.addInstruction(IrOpcode::Return);
		}
	}

	std::vector<IrOperand> generateExpressionIr(const ExpressionNode& exprNode) {
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
		else if (std::holds_alternative<FunctionCallNode>(exprNode)) {
			const auto& expr = std::get<FunctionCallNode>(exprNode);
			return generateFunctionCallIr(expr);
		}

		return {};
	}

	std::vector<IrOperand> generateIdentifierIr(const IdentifierNode& identifierNode) {
		// Generate IR for identifier and return appropriate operand
		// ...
		return { identifierNode.name() };
	}

	std::vector<IrOperand>
		generateNumericLiteralIr(const NumericLiteralNode& numericLiteralNode) {
		// Generate IR for numeric literal and return appropriate operand
		// ...
		// only supports ints for now
		return { Type::Int, static_cast<int>(numericLiteralNode.sizeInBits()), std::get<unsigned long long>(numericLiteralNode.value()) };
	}

	std::vector<IrOperand>
		generateStringLiteralIr(const StringLiteralNode& stringLiteralNode) {
		// Generate IR for string literal and return appropriate operand
		// ...
		return { stringLiteralNode.value() };
	}

	std::vector<IrOperand>
		generateBinaryOperatorIr(const BinaryOperatorNode& /*binaryOperatorNode*/) {
		// Generate IR for binary operator expression and return appropriate operand
		// ...
		return {};
	}

	std::vector<IrOperand>
		generateFunctionCallIr(const FunctionCallNode& /*functionCallNode*/) {
		// Generate IR for function call expression and return appropriate operand
		// ...
		return {};
	}

	Ir ir_;
};
