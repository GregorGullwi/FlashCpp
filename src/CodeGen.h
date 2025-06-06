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
		}

		ir_.addInstruction(IrInstruction(IrOpcode::FunctionDecl, std::move(funcDeclOperands)));

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

			ir_.addInstruction(IrOpcode::Return, std::move(operands));
		}
		else {
			ir_.addInstruction(IrOpcode::Return);
		}
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
		else if (std::holds_alternative<FunctionCallNode>(exprNode)) {
			const auto& expr = std::get<FunctionCallNode>(exprNode);
			auto operands = generateFunctionCallIr(expr);
			
			// Check if we have enough operands for a function call
			if (operands.size() >= 3) {
				std::vector<IrOperand> return_operands{ operands.end() - 3, operands.end() };
				operands.erase(operands.end() - 3, operands.end());

				ir_.addInstruction(
					IrInstruction(IrOpcode::FunctionCall, std::move(operands)));

				return return_operands;
			} else {
				// Handle error case - function call with insufficient operands
				return {};
			}
		}
		else {
			assert(false && "Not implemented yet");
		}

		return {};
	}

	std::vector<IrOperand> generateIdentifierIr(const IdentifierNode& identifierNode) {
		const std::optional<ASTNode> symbol = symbol_table.lookup(identifierNode.name());
		const auto& decl_node = symbol->as<DeclarationNode>();
		const auto& type_node = decl_node.type_node().as<TypeSpecifierNode>();
		return { type_node.type(), static_cast<int>(type_node.size_in_bits()), identifierNode.name() };
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

	std::vector<IrOperand> generateBinaryOperatorIr(const BinaryOperatorNode& binaryOperatorNode) {
		std::vector<IrOperand> irOperands;

		// Generate IR for the left-hand side and right-hand side of the operation
		auto lhsIrOperands = visitExpressionNode(binaryOperatorNode.get_lhs().as<ExpressionNode>());
		auto rhsIrOperands = visitExpressionNode(binaryOperatorNode.get_rhs().as<ExpressionNode>());

		// Insert the IR for the lhs and rhs into the irOperands vector
		irOperands.insert(irOperands.end(), lhsIrOperands.begin(), lhsIrOperands.end());
		irOperands.insert(irOperands.end(), rhsIrOperands.begin(), rhsIrOperands.end());

		// Generate the IR for the add operation
		if (binaryOperatorNode.op() == "+") {
			ir_.addInstruction(
				IrInstruction(IrOpcode::Add, std::move(irOperands)));
		}

		return {};
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
			irOperands.insert(irOperands.end(), argumentIrOperands.begin(), argumentIrOperands.end());
		});

		// Always add the return type information
		const auto& return_type = decl_node.type_node().as<TypeSpecifierNode>();
		irOperands.emplace_back(return_type.type());
		irOperands.emplace_back(static_cast<int>(return_type.size_in_bits()));
		irOperands.emplace_back(ret_var);

		return irOperands;
	}

	Ir ir_;
	TempVar var_counter{ 0 };
	SymbolTable symbol_table;
};
