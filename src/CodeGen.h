#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include "SymbolTable.h"
#include <type_traits>
#include <variant>
#include <vector>
#include <assert.h>

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
		else if (node.is<FunctionCallNode>()) {
			generateFunctionCallIr(node.as<FunctionCallNode>());
		}
	}

	const Ir& getIr() const { return ir_; }

private:
	void visitFunctionDeclarationNode(const FunctionDeclarationNode& node) {
		const DeclarationNode& func_decl = node.decl_node();
		const TypeSpecifierNode& ret_type = func_decl.type_node().as<TypeSpecifierNode>();
		ir_.addInstruction(
			IrInstruction(IrOpcode::FunctionDecl,
				{ ret_type.type(),
				  static_cast<int>(ret_type.size_in_bits()),
				  func_decl.identifier_token().value() }));

		auto definition_block = node.get_definition();
		if (!definition_block.has_value())
			return;

		(*definition_block)->get_statements().visit([&](ASTNode statement) {
			visit(statement);
		});
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
			auto operands = generateFunctionCallIr(expr);
			
			std::vector<IrOperand> return_operands{ operands.end() - 3, operands.end() };
			operands.erase(operands.end() - 3, operands.end());

			ir_.addInstruction(
				IrInstruction(IrOpcode::FunctionCall, std::move(operands)));

			return return_operands;
		}
		else {
			assert(false && "Not implemented yet");
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
		generateFunctionCallIr(const FunctionCallNode& functionCallNode) {
		std::vector<IrOperand> irOperands;

		const auto& decl_node = functionCallNode.function_declaration();
		auto type = gSymbolTable.lookup(decl_node.identifier_token().value());
		if (!type.has_value())
			return irOperands;	// TODO: Error?

		TempVar ret_var = var_counter.next();
		irOperands.emplace_back(ret_var);
		irOperands.emplace_back(decl_node.identifier_token().value());

		// Generate IR for function arguments
		functionCallNode.arguments().visit([&](ASTNode argument) {
			auto argumentIrOperands = generateExpressionIr(argument.as<ExpressionNode>());
			irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
		});

		// Generate IR for the return type
		const auto& return_type = decl_node.type_node().as<TypeSpecifierNode>();
		irOperands.emplace_back(return_type.type());
		irOperands.emplace_back(static_cast<int>(return_type.size_in_bits()));
		irOperands.emplace_back(ret_var);

		return irOperands;
	}

	Ir ir_;
	TempVar var_counter{ 0 };
};
