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
	Function,
};

using TypeIndex = size_t;

struct TypeInfo
{
	TypeInfo() : type_(Type::Void), type_index_(0) {}
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
	{ "void", & gTypeInfo[0] },
	{ "bool", &gTypeInfo[1] },
	{ "char", &gTypeInfo[2] },
	{ "int", &gTypeInfo[3] },
	{ "float", &gTypeInfo[4] },
};

std::unordered_map<Type, const TypeInfo*> gNativeTypes
{
	{ Type::Void, & gTypeInfo[0] },
	{ Type::Bool, &gTypeInfo[1] },
	{ Type::Char, &gTypeInfo[2] },
	{ Type::Int,  &gTypeInfo[3] },
	{ Type::Float,&gTypeInfo[4] },
};

TypeInfo& add_user_type(std::string name)
{
	auto& type_info = gTypeInfo.emplace_back(std::move(name), Type::UserDefined, gTypeInfo.size());
	gTypesByName.emplace(type_info.name_, &type_info);
	return type_info;
}

TypeInfo& add_function_type(std::string name, Type return_type)
{
	auto& type_info = gTypeInfo.emplace_back(std::move(name), Type::Function, gTypeInfo.size());
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
	DeclarationNode(ASTNode type_node, Token identifier)
		: type_node_(type_node), identifier_(std::move(identifier)) {}

	ASTNode type_node() const { return type_node_; }
	const Token& identifier_token() const { return identifier_; }

private:
	ASTNode type_node_;
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

class BlockNode {
public:
	explicit BlockNode() {}

	const auto& get_statements() const { return statements_; }
	void add_statement_node(ASTNode node) { statements_.push_back(node); }

private:
	ChunkedVector<ASTNode, 128, 256> statements_;
};

class FunctionDeclarationNode {
public:
	FunctionDeclarationNode() = default;
	FunctionDeclarationNode(DeclarationNode& decl_node)
		: decl_node_(decl_node) {}

	const DeclarationNode& decl_node() const {
		return decl_node_;
	}
	const std::vector<ASTNode>& parameter_nodes() const {
		return parameter_nodes_;
	}
	void add_parameter_node(ASTNode parameter_node) {
		parameter_nodes_.push_back(parameter_node);
	}
	auto get_definition() const {
		return definition_block_;
	}
	bool set_definition(BlockNode& block_node) {
		if (definition_block_.has_value())
			return false;

		definition_block_.emplace(&block_node);
		return true;
	}

private:
	DeclarationNode& decl_node_;
	std::vector<ASTNode> parameter_nodes_;
	std::optional<BlockNode*> definition_block_;
};

class FunctionCallNode {
public:
	explicit FunctionCallNode(DeclarationNode& func_decl, ChunkedVector<ASTNode>&& arguments)
		: func_decl_(func_decl), arguments_(std::move(arguments)) {}

	const auto& arguments() const { return arguments_; }
	const auto& function_declaration() const { return func_decl_; }

	void add_argument(ASTNode argument) { arguments_.push_back(argument); }

private:
	DeclarationNode& func_decl_;
	ChunkedVector<ASTNode> arguments_;
};

using ExpressionNode = std::variant<IdentifierNode, StringLiteralNode, NumericLiteralNode,
	BinaryOperatorNode, FunctionCallNode>;

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
		std::optional<ASTNode> expression = std::nullopt)
		: expression_(expression) {}

	std::optional<ASTNode> expression() const { return expression_; }

private:
	std::optional<ASTNode>
		expression_; // Optional, as a return statement may not have an expression
};
