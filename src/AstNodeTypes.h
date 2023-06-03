#pragma once

#include <climits>
#include <cstddef>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <deque>
#include <unordered_map>
#include <any>

#include "Token.h"
#include "ChunkedAnyVector.h"

ChunkedAnyVector gChunkedAnyStorage;

class ASTNode {
public:
	ASTNode() = default;

	template <typename T> ASTNode(T* node) : node_(node) {}

	template <typename T, typename... Args>
	static ASTNode emplace_node(Args&&... args) {
		T& t = gChunkedAnyStorage.emplace_back<T>(std::forward<Args>(args)...);
		return ASTNode(&t);
	}

	template <typename T> bool is() const {
		return node_.type() == typeid(T*);
	}

	template <typename T> T& as() {
		return *std::any_cast<T*>(node_);
	}

	template <typename T> const T& as() const {
		return *std::any_cast<T*>(node_);
	}

	auto type_name() const {
		return node_.type().name();
	}

private:
	std::any node_;
};

using ASTNodeHandle = ASTNode;

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

using TypeIndex = size_t;

struct TypeInfo
{
	TypeInfo() = default;
	TypeInfo(std::string name, Type type, TypeIndex idx) : name_(std::move(name)), type_(type), type_index_(idx) {}

	std::string name_;
	Type type_;
	TypeIndex type_index_;

	const char* name() { return name_.c_str(); };
};

std::deque<TypeInfo> gTypeInfo =
{
	{ "void", 	Type::Void,		0 },
	{ "bool",	Type::Bool,		1 },
	{ "char", 	Type::Char,		2 },
	{ "int",	Type::Int,		3 },
	{ "float",	Type::Float,	4 },
};

std::unordered_map<std::string, const TypeInfo*> gTypesByName
{
	{ "void", &gTypeInfo[0] },
	{ "bool", &gTypeInfo[1] },
	{ "char", &gTypeInfo[2] },
	{ "int", &gTypeInfo[3] },
	{ "float", &gTypeInfo[4] },
};

std::unordered_map<Type, const TypeInfo*> gNativeTypes
{
	{ Type::Void, &gTypeInfo[0] },
	{ Type::Bool, &gTypeInfo[1] },
	{ Type::Char, &gTypeInfo[2] },
	{ Type::Int,  &gTypeInfo[3] },
	{ Type::Float,&gTypeInfo[4] },
};

const TypeInfo& add_user_type(std::string name)
{
	const auto& type_info = gTypeInfo.emplace_back(std::move(name), Type::UserDefined, gTypeInfo.size());
	gTypesByName.emplace(type_info.name_, &type_info);
	return type_info;
}

class TypeSpecifierNode {
public:
	TypeSpecifierNode() = default;
	TypeSpecifierNode(Type type, TypeQualifier qualifier, unsigned char sizeInBits,
		const Token& token = {})
		: type_(type), size_(sizeInBits), qualifier_(qualifier), token_(token) {}

	auto type() const { return type_; }
	auto size_in_bits() const { return size_; }
	auto qualifier() const { return qualifier_; }

private:
	Type type_;
	unsigned char size_;
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

class IdentifierNode {
public:
	explicit IdentifierNode(Token identifier) : identifier_(identifier) {}

	std::string_view name() const { return identifier_.value(); }

private:
	Token identifier_;
};

using NumericLiteralValue = std::variant<unsigned long long, double>;

class NumericLiteralNode {
public:
	explicit NumericLiteralNode(Token identifier, NumericLiteralValue value, Type type, TypeQualifier qualifier, unsigned char size) : value_(value), type_(type), size_(size), qualifier_(qualifier), identifier_(identifier) {}

	std::string_view token() const { return identifier_.value(); }
	NumericLiteralValue value() const { return value_; }
	Type type() const { return type_; }
	unsigned char sizeInBits() const { return size_; }
	TypeQualifier qualifier() const { return qualifier_; }

private:
	NumericLiteralValue value_;
	Type type_;
	unsigned char size_;	// Size in bits
	TypeQualifier qualifier_;
	Token identifier_;
};

class StringLiteralNode {
public:
	explicit StringLiteralNode(Token identifier) : identifier_(identifier) {}

	std::string_view value() const { return identifier_.value(); }

private:
	Token identifier_;
};

class BinaryOperatorNode {
public:
	explicit BinaryOperatorNode(Token identifier, size_t lhs_index,
		size_t rhs_index)
		: identifier_(identifier), lhs_index_(lhs_index), rhs_index_(rhs_index) {}

	std::string_view op() const { return identifier_.value(); }
	size_t get_lhs_index() const { return lhs_index_; }
	size_t get_rhs_index() const { return rhs_index_; }

private:
	Token identifier_;
	size_t lhs_index_;
	size_t rhs_index_;
};

class FunctionCallNode {
public:
	explicit FunctionCallNode(Token identifier, size_t function,
		std::vector<size_t> arguments)
		: identifier_(identifier), function_(function),
		arguments_(std::move(arguments)) {}

	size_t function() const { return function_; }
	const std::vector<size_t>& arguments() const { return arguments_; }

private:
	Token identifier_;
	size_t function_;
	std::vector<size_t> arguments_;
};

using ExpressionNode = std::variant<IdentifierNode, StringLiteralNode, NumericLiteralNode,
	BinaryOperatorNode, FunctionCallNode>;

class FunctionDeclarationNode {
public:
	FunctionDeclarationNode() = default;
	FunctionDeclarationNode(ASTNodeHandle return_specifier_node)
		: return_specifier_node_(std::move(return_specifier_node)) {}

	ASTNodeHandle return_type_handle() const {
		return return_specifier_node_;
	}
	const std::vector<ASTNodeHandle>& parameter_handles() const {
		return parameter_handles_;
	}
	void add_parameter_node_handle(ASTNodeHandle parameter_handle) {
		parameter_handles_.push_back(parameter_handle);
	}

private:
	ASTNodeHandle return_specifier_node_;
	std::vector<ASTNodeHandle> parameter_handles_;
};

/*class FunctionDefinitionNode {
public:
		FunctionDefinitionNode(Token definition_token, size_t definition_index,
std::vector<size_t> parameter_indices, size_t body_index) :
definition_token_(definition_token), definition_index_(definition_index),
parameter_indices_(std::move(parameter_indices)), body_index_(body_index) {}

		const Token& definition_token() const { return definition_token_; }
		size_t declaration_index() const { return definition_index_; }
		const std::vector<size_t>& parameter_indices() const { return
parameter_indices_; } size_t body_index() const { return body_index_; }

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
	void set_num_statements(size_t num_statements) {
		num_statements_ = num_statements;
	}

private:
	size_t start_index_ = UINT_MAX;
	size_t num_statements_ = 0;
};

class IfStatementNode {
public:
	explicit IfStatementNode(size_t start_pos, size_t end_pos, size_t condition,
		size_t if_body, size_t else_body)
		: start_pos_(start_pos), end_pos_(end_pos), condition_(condition),
		if_body_(if_body), else_body_(else_body) {}

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
	explicit WhileLoopNode(size_t start_pos, size_t end_pos, size_t condition,
		size_t body)
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
	explicit DoWhileLoopNode(size_t start_pos, size_t end_pos, size_t body,
		size_t condition)
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
	explicit ForLoopNode(size_t start_pos, size_t end_pos, size_t init,
		size_t condition, size_t iteration, size_t body)
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
	explicit ReturnStatementNode(
		std::optional<ASTNodeHandle> expression = std::nullopt)
		: expression_(expression) {}

	std::optional<ASTNodeHandle> expression() const { return expression_; }

private:
	std::optional<ASTNodeHandle>
		expression_; // Optional, as a return statement may not have an expression
};
