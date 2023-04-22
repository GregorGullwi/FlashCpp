#pragma once

#include <string_view>
#include <vector>
#include <cstddef>

class DeclarationNode {
public:
	DeclarationNode(std::string_view type, bool is_const = false, bool is_static = false)
		: type_(type), is_const_(is_const), is_static_(is_static) {}

	std::string_view type() const { return type_; }
	bool is_const() const { return is_const_; }
	bool is_static() const { return is_static_; }

private:
	std::string_view type_;
	bool is_const_;
	bool is_static_;
};

class ExpressionNode {
public:
	size_t start_pos;
	size_t end_pos;
};

class IdentifierNode : public ExpressionNode {
public:
	explicit IdentifierNode(size_t start_pos, size_t end_pos, const std::string_view& name)
		: name_(name) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	std::string_view name() const { return name_; }

private:
	std::string_view name_;
};

class StringLiteralNode : public ExpressionNode {
public:
	explicit StringLiteralNode(size_t start_pos, size_t end_pos, const std::string_view& value)
		: value_(value) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	std::string_view value() const { return value_; }

private:
	std::string_view value_;
};

class BinaryOperatorNode : public ExpressionNode {
public:
	explicit BinaryOperatorNode(size_t start_pos, size_t end_pos, const std::string_view& op)
		: op_(op) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	std::string_view op() const { return op_; }

private:
	std::string_view op_;
};

class FunctionCallNode : public ExpressionNode {
public:
	explicit FunctionCallNode(size_t start_pos, size_t end_pos, size_t function, std::vector<size_t> arguments)
		: function_(function), arguments_(std::move(arguments)) {
		this->start_pos = start_pos;
		this->end_pos = end_pos;
	}

	size_t function() const { return function_; }
	const std::vector<size_t>& arguments() const { return arguments_; }

private:
	size_t function_;
	std::vector<size_t> arguments_;
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