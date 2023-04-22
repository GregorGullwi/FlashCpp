#pragma once

#include <string_view>
#include <vector>
#include <iostream>
#include <variant>
#include <unordered_set>

#include "LexerNodeTypes.h"

enum class CppStandard
{
	CPP98,
	CPP11,
	CPP14,
	CPP17,
	CPP20,
	CPP23,
};

#include <cctype>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <variant>

class Lexer {
public:
	explicit Lexer(std::string_view source)
		: source_(source), current_(source.begin()), line_(1), column_(1) {}

	std::vector<std::variant<DeclarationNode, ExpressionNode, IdentifierNode,
		StringLiteralNode, BinaryOperatorNode, FunctionCallNode, IfStatementNode, LoopStatementNode,
		WhileLoopNode, DoWhileLoopNode, ForLoopNode>>
		tokenize() {
		std::vector<std::variant<DeclarationNode, ExpressionNode, IdentifierNode,
			StringLiteralNode, BinaryOperatorNode, FunctionCallNode, IfStatementNode, LoopStatementNode,
			WhileLoopNode, DoWhileLoopNode, ForLoopNode>>
			tokens;

		while (current_ != source_.end()) {
			if (std::isspace(*current_)) {
				consumeWhitespace();
			}
			else if (std::isalpha(*current_) || *current_ == '_') {
				auto idNode = consumeIdentifier();
				if (current_ != source_.end() && *current_ == '(') {
					tokens.push_back(consumeFunctionCall(idNode));
				} else if (idNode.name() == "if") {
					tokens.push_back(consumeIfStatement());
				} else if (idNode.name() == "while") {
					tokens.push_back(consumeWhileLoop());
				} else if (idNode.name() == "do") {
					tokens.push_back(consumeDoWhileLoop());
				} else if (idNode.name() == "for") {
					tokens.push_back(consumeForLoop());
				} else {
					tokens.push_back(idNode);
				}
			}
			else if (std::isdigit(*current_)) {
				tokens.push_back(consumeNumericLiteral());
			}
			else if (*current_ == '\"') {
				tokens.push_back(consumeStringLiteral());
			}
			else {
				auto op = consumeOperator();
				if (!op.empty()) {
					tokens.push_back(BinaryOperatorNode{ 0, 0, op });
				}
				else {
					throw std::runtime_error("Invalid character found");
				}
			}
		}

		return tokens;
	}

private:
	std::string_view source_;
	std::string_view::const_iterator current_;
	size_t line_;
	size_t column_;

	void consumeWhitespace() {
		while (current_ != source_.end() && std::isspace(*current_)) {
			if (*current_ == '\n') {
				++line_;
				column_ = 1;
			}
			else {
				++column_;
			}
			++current_;
		}
	}

	IdentifierNode consumeIdentifier() {
		auto start = current_;
		size_t startPos = column_;
		while (current_ != source_.end() && (std::isalnum(*current_) || *current_ == '_')) {
			++column_;
			++current_;
		}
		return IdentifierNode(startPos, column_, std::string_view(&*start, current_ - start));
	}

	FunctionCallNode consumeFunctionCall(const IdentifierNode& idNode) {
		size_t startPos = idNode.start_pos;
		size_t endPos = 0;
		std::vector<size_t> argumentPositions;

		++current_; // Consume opening parenthesis
		++column_;

		while (current_ != source_.end() && *current_ != ')') {
			if (std::isspace(*current_)) {
				consumeWhitespace();
			}
			else {
				argumentPositions.push_back(column_);
				// You may need to add more specific parsing for different argument types (e.g., literals, expressions)
				while (current_ != source_.end() && *current_ != ',' && *current_ != ')') {
					++column_;
					++current_;
				}
			}

			if (current_ != source_.end() && *current_ == ',') {
				++current_; // Consume comma
				++column_;
			}
		}

		if (current_ == source_.end()) {
			throw std::runtime_error("Unterminated function call");
		}

		++current_; // Consume closing parenthesis
		endPos = column_;
		++column_;

		return FunctionCallNode(startPos, endPos, idNode.start_pos, std::move(argumentPositions));
	}

	IfStatementNode consumeIfStatement() {
		size_t startPos = column_;
		size_t endPos = 0;
		size_t conditionPos = 0;
		size_t ifBodyPos = 0;
		size_t elseBodyPos = 0;

		++current_; // Consume 'if' keyword
		++column_;

		consumeWhitespace();

		if (current_ == source_.end() || *current_ != '(') {
			throw std::runtime_error("Expected '(' after 'if'");
		}

		++current_; // Consume opening parenthesis
		++column_;

		conditionPos = column_;
		// You may need to add more specific parsing for the condition expression
		while (current_ != source_.end() && *current_ != ')') {
			++column_;
			++current_;
		}

		if (current_ == source_.end()) {
			throw std::runtime_error("Unterminated if condition");
		}

		++current_; // Consume closing parenthesis
		++column_;

		consumeWhitespace();

		ifBodyPos = column_;
		// You may need to add more specific parsing for the if body (e.g., handling '{' and '}')
		while (current_ != source_.end() && *current_ != 'e' && *current_ != ';') {
			++column_;
			++current_;
		}

		if (current_ != source_.end() && *current_ == ';') {
			++column_;
			++current_;
		}

		consumeWhitespace();

		if (current_ != source_.end() && *current_ == 'e') {
			auto elseIdNode = consumeIdentifier();
			if (elseIdNode.name() != "else") {
				throw std::runtime_error("Expected 'else' after 'if' body");
			}

			consumeWhitespace();

			elseBodyPos = column_;
			// You may need to add more specific parsing for the else body (e.g., handling '{' and '}')
			while (current_ != source_.end() && *current_ != ';') {
				++column_;
				++current_;
			}

			if (current_ != source_.end() && *current_ == ';') {
				++column_;
				++current_;
			}
		}

		endPos = column_;

		return IfStatementNode(startPos, endPos, conditionPos, ifBodyPos, elseBodyPos);
	}

	WhileLoopNode consumeWhileLoop() {
		size_t startPos = column_;
		size_t endPos = 0;
		size_t conditionPos = 0;
		size_t bodyPos = 0;

		++current_; // Consume 'while' keyword
		++column_;

		consumeWhitespace();

		if (current_ == source_.end() || *current_ != '(') {
			throw std::runtime_error("Expected '(' after 'while'");
		}

		++current_; // Consume opening parenthesis
		++column_;

		conditionPos = column_;
		consumeExpression(); // Parse condition expression

		if (current_ == source_.end() || *current_ != ')') {
			throw std::runtime_error("Expected ')' after 'while' condition");
		}

		++current_; // Consume closing parenthesis
		++column_;

		consumeWhitespace();

		bodyPos = column_;
		consumeStatement(); // Parse loop body

		endPos = column_;

		return WhileLoopNode(startPos, endPos, conditionPos, bodyPos);
	}

	DoWhileLoopNode consumeDoWhileLoop() {
		size_t startPos = column_;
		size_t endPos = 0;
		size_t bodyPos = 0;
		size_t conditionPos = 0;

		++current_; // Consume 'do' keyword
		++column_;

		consumeWhitespace();

		bodyPos = column_;
		consumeStatement(); // Parse loop body

		consumeWhitespace();

		if (current_ == source_.end() || *current_ != 'w') {
			throw std::runtime_error("Expected 'while' after 'do' loop body");
		}

		consumeIdentifier(); // Consume 'while' keyword

		consumeWhitespace();

		if (current_ == source_.end() || *current_ != '(') {
			throw std::runtime_error("Expected '(' after 'do ... while'");
		}

		++current_; // Consume opening parenthesis
		++column_;

		conditionPos = column_;
		consumeExpression(); // Parse condition expression

		if (current_ == source_.end() || *current_ != ')') {
			throw std::runtime_error("Expected ')' after 'do ... while' condition");
		}

		++current_; // Consume closing parenthesis
		++column_;

		endPos = column_;

		return DoWhileLoopNode(startPos, endPos, bodyPos, conditionPos);
	}

	ForLoopNode consumeForLoop() {
		size_t startPos = column_;
		size_t endPos = 0;
		size_t initPos = 0;
		size_t conditionPos = 0;
		size_t iterationPos = 0;
		size_t bodyPos = 0;

		++current_; // Consume 'for' keyword
		++column_;

		consumeWhitespace();

		if (current_ == source_.end() || *current_ != '(') {
			throw std::runtime_error("Expected '(' after 'for'");
		}

		++current_; // Consume opening parenthesis
		++column_;

		initPos = column_;
		consumeExpression(); // Parse initialization expression

		if (current_ == source_.end() || *current_ != ';') {
			throw std::runtime_error("Expected ';' after 'for' initialization");
		}

		++current_; // Consume ';'
		++column_;

		conditionPos = column_;
		consumeExpression(); // Parse condition expression

		if (current_ == source_.end() || *current_ != ';') {
			throw std::runtime_error("Expected ';' after 'for' condition");
		}

		++current_; // Consume ';'
		++column_;

		iterationPos = column_;
		consumeExpression(); // Parse iteration expression

		if (current_ == source_.end() || *current_ != ')') {
			throw std::runtime_error("Expected ')' after 'for' iteration");
		}

		++current_; // Consume closing parenthesis
		++column_;

		consumeWhitespace();

		bodyPos = column_;
		consumeStatement(); // Parse loop body

		endPos = column_;

		return ForLoopNode(startPos, endPos, initPos, conditionPos, iterationPos, bodyPos);
	}

	ExpressionNode consumeNumericLiteral() {
		auto start = current_;
		size_t startPos = column_;
		while (current_ != source_.end() && std::isdigit(*current_)) {
			++column_;
			++current_;
		}
		return ExpressionNode{ startPos, column_ };
	}

	StringLiteralNode consumeStringLiteral() {
		++current_; // Consume opening double quote
		auto start = current_;
		size_t startPos = column_;

		while (current_ != source_.end() && *current_ != '\"') {
			if (*current_ == '\\') {
				// Skip the escaped character
				++current_;
				++column_;
			}

			++column_;
			++current_;
		}

		if (current_ == source_.end()) {
			throw std::runtime_error("Unterminated string literal");
		}

		++current_; // Consume closing double quote
		++column_;

		return StringLiteralNode(startPos, column_, std::string_view(&*start, current_ - start - 1));
	}

	void consumeStatement() {
		consumeWhitespace();

		if (current_ == source_.end() || *current_ != '{') {
			throw std::runtime_error("Expected '{' at the beginning of a statement");
		}

		++current_; // Consume opening curly brace
		++column_;

		while (current_ != source_.end() && *current_ != '}') {
			consumeExpression(); // Parse expressions within the statement

			if (current_ == source_.end() || *current_ != ';') {
				throw std::runtime_error("Expected ';' after expression");
			}

			++current_; // Consume ';'
			++column_;

			consumeWhitespace();
		}

		if (current_ == source_.end() || *current_ != '}') {
			throw std::runtime_error("Expected '}' at the end of a statement");
		}

		++current_; // Consume closing curly brace
		++column_;
	}

	void consumeExpression() {
		consumeWhitespace();

		while (current_ != source_.end() && *current_ != ';' && *current_ != ')') {
			if (std::isalnum(*current_) || *current_ == '_' || *current_ == '(') {
				++current_;
				++column_;
			}
			else {
				throw std::runtime_error("Unexpected character in expression");
			}

			consumeWhitespace();
		}
	}

	std::string_view consumeOperator() {
		std::string_view op;
		if (current_ + 1 != source_.end()) {
			// Check for two character operators first
			std::string_view possibleOp(&*current_, 2);
			auto it = twoCharOperators.find(possibleOp);
			if (it != twoCharOperators.end()) {
				op = possibleOp;
				current_ += 2;
				column_ += 2;
			}
		}

		// If we haven't found a two character operator, try one character operators
		if (op.empty()) {
			std::string_view possibleOp(&*current_, 1);
			if (isOperatorChar(possibleOp.front())) {
				op = possibleOp;
				++current_;
				++column_;
			}
		}

		return op;
	}

	bool isOperatorChar(char c) {
		static const std::unordered_set<char> operatorChars = { '+', '-', '*', '/', '%', '<', '>', '=', '&', '|', '^', '!', '~' };
		return operatorChars.find(c) != operatorChars.end();
	}

	const std::unordered_set<std::string_view> twoCharOperators = { "++", "--", "==", "!=", "<=", ">=", "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=" };
};
