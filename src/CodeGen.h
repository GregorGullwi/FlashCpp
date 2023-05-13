#pragma once

#include "AstNodeTypes.h"
#include "IRTypes.h"
#include <type_traits>
#include <variant>
#include <vector>

class Parser;

class AstToIr {
public:
  explicit AstToIr(Parser &parser) : parser_(parser) {}

  void visit(const ASTNode &node) {
    std::visit(
        [&](const auto &val) {
          using T = std::decay_t<decltype(val)>;
          if constexpr (std::is_same_v<T, FunctionDeclarationNode>) {
            visitFunctionDeclarationNode(val);
          } else if constexpr (std::is_same_v<T, ReturnStatementNode>) {
            visitReturnStatementNode(val);
          }
          // ... add other node types here
        },
        node.node());
  }

  const Ir &getIr() const { return ir_; }

private:
  void visitFunctionDeclarationNode(const FunctionDeclarationNode & /*node*/) {
    /*ir_.addInstruction(
        IrInstruction{IrOpcode::Function, node.getName()});
    for (const auto &statement : node.getStatements()) {
      visit(statement);
    }*/
  }

  void visitReturnStatementNode(const ReturnStatementNode &node) {
    if (node.expression()) {
      const ASTNode &expressionNode =
          parser_.get_inner_node(*node.expression());
      auto operand = generateExpressionIr(expressionNode.as<ExpressionNode>());
      ir_.addInstruction(IrOpcode::Return, operand);
    } else {
      ir_.addInstruction(IrOpcode::Return);
    }
  }

  IrOperand generateExpressionIr(const ExpressionNode &exprNode) {
    return std::visit(
        [&](const auto &expr) -> IrOperand {
          if constexpr (std::is_same_v<decltype(expr), IdentifierNode>) {
            // Handle IdentifierNode
            return generateIdentifierIr(expr);
          } else if constexpr (std::is_same_v<decltype(expr),
                                              StringLiteralNode>) {
            // Handle StringLiteralNode
            return generateStringLiteralIr(expr);
          } else if constexpr (std::is_same_v<decltype(expr),
                                              BinaryOperatorNode>) {
            // Handle BinaryOperatorNode
            return generateBinaryOperatorIr(expr);
          } else if constexpr (std::is_same_v<decltype(expr),
                                              FunctionCallNode>) {
            // Handle FunctionCallNode
            return generateFunctionCallIr(expr);
          }

          return {""};
        },
        exprNode);
  }

  IrOperand generateIdentifierIr(const IdentifierNode &identifierNode) {
    // Generate IR for identifier and return appropriate operand
    // ...
    return { identifierNode.name() };
  }

  IrOperand
  generateStringLiteralIr(const StringLiteralNode &stringLiteralNode) {
    // Generate IR for string literal and return appropriate operand
    // ...
    return { stringLiteralNode.value() };
  }

  IrOperand
  generateBinaryOperatorIr(const BinaryOperatorNode & /*binaryOperatorNode*/) {
    // Generate IR for binary operator expression and return appropriate operand
    // ...
    return IrOperand();
  }

  IrOperand
  generateFunctionCallIr(const FunctionCallNode & /*functionCallNode*/) {
    // Generate IR for function call expression and return appropriate operand
    // ...
    return IrOperand();
  }

  Ir ir_;
  Parser &parser_;
};
