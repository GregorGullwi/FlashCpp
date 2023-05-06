#pragma once

#include <string_view>
#include <vector>
#include <cstddef>
#include <variant>

using ASTNodeHandle = size_t;

enum class TypeQualifier {
	None,
	Signed,
	Unsigned,
};

enum class Type {
	Void,
	Bool,
	Char,
	Int,
	Float,
	UserDefined,
	Auto,
};

class TypeSpecifierNode {
public:
	TypeSpecifierNode() = default;
	TypeSpecifierNode(Type type, TypeQualifier qualifier, size_t size, const Token& token = {})
		: type_(type), qualifier_(qualifier), size_(size), token_(token) {}

	Type type() const { return type_; }

private:
	Type type_;
	size_t size_;
	TypeQualifier qualifier_;
	Token token_;
};

class DeclarationNode {
public:
	DeclarationNode() = default;
	DeclarationNode(ASTNodeHandle type_handle, Token identifier)
		: type_handle_(type_handle), identifier_(std::move(identifier)) {}

	ASTNodeHandle type_handle() const { return type_handle_; }
	const Token& identifier_token() const { return identifier_; }

private:
	ASTNodeHandle type_handle_;
	Token identifier_;
};

class ExpressionNode {
public:
	explicit ExpressionNode(Token token)
		: token_(std::move(token)) {}
	Token token_;
};

class IdentifierNode : public ExpressionNode {
public:
	explicit IdentifierNode(Token token)
		: ExpressionNode(token) {}

	std::string_view name() const { return token_.value(); }

private:
};

class StringLiteralNode : public ExpressionNode {
public:
	explicit StringLiteralNode(Token token)
		: ExpressionNode(token) {}

	std::string_view value() const { return token_.value(); }

private:
};

class BinaryOperatorNode : public ExpressionNode {
public:
	explicit BinaryOperatorNode(Token token, size_t lhs_index, size_t rhs_index)
		: ExpressionNode(token), lhs_index_(lhs_index), rhs_index_(rhs_index) {}

	std::string_view op() const { return token_.value(); }
	size_t get_lhs_index() const { return lhs_index_; }
	size_t get_rhs_index() const { return rhs_index_; }

private:
	size_t lhs_index_;
	size_t rhs_index_;
};

class FunctionCallNode : public ExpressionNode {
public:
	explicit FunctionCallNode(Token token, size_t function, std::vector<size_t> arguments)
		: ExpressionNode(token), function_(function), arguments_(std::move(arguments)) {}

	size_t function() const { return function_; }
	const std::vector<size_t>& arguments() const { return arguments_; }

private:
	size_t function_;
	std::vector<size_t> arguments_;
};

class FunctionDeclarationNode {
public:
	FunctionDeclarationNode() = default;
	FunctionDeclarationNode(ASTNodeHandle return_specifier_node)
		: return_specifier_node_(std::move(return_specifier_node)) {}

	ASTNodeHandle return_specifier_node_handle() const { return return_specifier_node_; }
	const std::vector<ASTNodeHandle>& parameter_handles() const { return parameter_handles_; }
	void add_parameter_node_handle(ASTNodeHandle parameter_handle) { parameter_handles_.push_back(parameter_handle); }

private:
	ASTNodeHandle return_specifier_node_;
	std::vector<ASTNodeHandle> parameter_handles_;
};

/*class FunctionDefinitionNode {
public:
	FunctionDefinitionNode(Token definition_token, size_t definition_index, std::vector<size_t> parameter_indices, size_t body_index)
		: definition_token_(definition_token), definition_index_(definition_index), parameter_indices_(std::move(parameter_indices)), body_index_(body_index) {}

	const Token& definition_token() const { return definition_token_; }
	size_t declaration_index() const { return definition_index_; }
	const std::vector<size_t>& parameter_indices() const { return parameter_indices_; }
	size_t body_index() const { return body_index_; }

private:
	Token definition_token_;
	size_t definition_index_;
	std::vector<size_t> parameter_indices_;
	size_t body_index_;
};*/

class BlockNode {
public:
	explicit BlockNode(size_t start_index) : start_index_(start_index) {}

	size_t start_index() const { return start_index_; }
	void set_num_statements(size_t num_statements) { num_statements_ = num_statements; }

private:
	size_t start_index_ = UINT_MAX;
	size_t num_statements_ = 0;
};


class IfStatementNode {
public:
	explicit IfStatementNode(size_t start_pos, size_t end_pos, size_t condition, size_t if_body, size_t else_body)
		: start_pos_(start_pos), end_pos_(end_pos), condition_(condition), if_body_(if_body), else_body_(else_body) {}

	size_t start_pos() const { return start_pos_; }
	size_t end_pos() const { return end_pos_; }
	size_t condition() const { return condition_; }
	size_t if_body() const { return if_body_; }
	size_t else_body() const { return else_body_; }

private:
	size_t start_pos_;
	size_t end_pos_;
	size_t condition_;
	size_t if_body_;
	size_t else_body_;
};

class LoopStatementNode {
public:
	size_t start_pos;
	size_t end_pos;
};

class WhileLoopNode : public LoopStatementNode {
public:
	explicit WhileLoopNode(size_t start_pos, size_t end_pos, size_t condition, size_t body)
		: condition_(condition), body_(body) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	size_t condition() const { return condition_; }
	size_t body() const { return body_; }

private:
	size_t condition_;
	size_t body_;
};

class DoWhileLoopNode : public LoopStatementNode {
public:
	explicit DoWhileLoopNode(size_t start_pos, size_t end_pos, size_t body, size_t condition)
	: condition_(condition), body_(body) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	size_t condition() const { return condition_; }
	size_t body() const { return body_; }

private:
	size_t condition_;
	size_t body_;
};

class ForLoopNode : public LoopStatementNode {
public:
	explicit ForLoopNode(size_t start_pos, size_t end_pos, size_t init, size_t condition, size_t iteration, size_t body)
		: init_(init), condition_(condition), iteration_(iteration), body_(body) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	size_t init() const { return init_; }
	size_t condition() const { return condition_; }
	size_t iteration() const { return iteration_; }
	size_t body() const { return body_; }

private:
	size_t init_;
	size_t condition_;
	size_t iteration_;
	size_t body_;
};

class ReturnStatementNode {
public:
	explicit ReturnStatementNode(std::optional<ASTNodeHandle> expression = std::nullopt)
		: expression_(expression) {}

	std::optional<ASTNodeHandle> expression() const { return expression_; }

private:
	std::optional<ASTNodeHandle> expression_; // Optional, as a return statement may not have an expression
};

class ASTNode {
public:
	using NodeType = std::variant<
		std::monostate,
		TypeSpecifierNode,
		DeclarationNode,
		ExpressionNode,
		IdentifierNode,
		StringLiteralNode,
		BinaryOperatorNode,
		FunctionCallNode,
		FunctionDeclarationNode,
		BlockNode,
		IfStatementNode,
		LoopStatementNode,
		WhileLoopNode,
		DoWhileLoopNode,
		ForLoopNode,
		ReturnStatementNode
	>;

	ASTNode() = default;

	template <typename T>
	ASTNode(T&& node) : node_(std::forward<T>(node)) {}

	template <typename T>
	bool is() const {
		return std::holds_alternative<T>(node_);
	}

	template <typename T>
	T& as() {
		return std::get<T>(node_);
	}

	template <typename T>
	const T& as() const {
		return std::get<T>(node_);
	}

	const NodeType& node() const {
		return node_;
	}

private:
	NodeType node_;
};
