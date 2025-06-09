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
#include <optional>

#include "Token.h"
#include "ChunkedAnyVector.h"

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
	UnsignedChar,
	Short,
	UnsignedShort,
	Int,
	UnsignedInt,
	Long,
	UnsignedLong,
	LongLong,
	UnsignedLongLong,
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

extern std::deque<TypeInfo> gTypeInfo;

extern std::unordered_map<std::string, const TypeInfo*> gTypesByName;

extern std::unordered_map<Type, const TypeInfo*> gNativeTypes;

TypeInfo& add_user_type(std::string name);

TypeInfo& add_function_type(std::string name, Type /*return_type*/);

void initialize_native_types();

// Integer promotion and conversion utilities
bool is_integer_type(Type type);
bool is_signed_integer_type(Type type);
bool is_unsigned_integer_type(Type type);
bool is_bool_type(Type type);
int get_integer_rank(Type type);
int get_type_size_bits(Type type);
Type promote_integer_type(Type type);
Type get_common_type(Type left, Type right);
bool requires_conversion(Type from, Type to);

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
	explicit BinaryOperatorNode(Token identifier, ASTNode lhs_node,
		ASTNode rhs_node)
		: identifier_(identifier), lhs_node_(lhs_node), rhs_node_(rhs_node) {}

	std::string_view op() const { return identifier_.value(); }
	auto get_lhs() const { return lhs_node_; }
	auto get_rhs() const { return rhs_node_; }

private:
	Token identifier_;
	ASTNode lhs_node_;
	ASTNode rhs_node_;
};

class UnaryOperatorNode {
public:
	explicit UnaryOperatorNode(Token identifier, ASTNode operand_node, bool is_prefix = true)
		: identifier_(identifier), operand_node_(operand_node), is_prefix_(is_prefix) {}

	std::string_view op() const { return identifier_.value(); }
	auto get_operand() const { return operand_node_; }
	bool is_prefix() const { return is_prefix_; }

private:
	Token identifier_;
	ASTNode operand_node_;
	bool is_prefix_;
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
	FunctionDeclarationNode() = delete;
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
	BinaryOperatorNode, UnaryOperatorNode, FunctionCallNode>;

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

class ForLoopNode {
public:
    ForLoopNode(ASTNode init, ASTNode condition, ASTNode increment, ASTNode body)
        : init_(std::move(init))
        , condition_(std::move(condition))
        , increment_(std::move(increment))
        , body_(std::move(body)) {}

    const ASTNode& init() const { return init_; }
    const ASTNode& condition() const { return condition_; }
    const ASTNode& increment() const { return increment_; }
    const ASTNode& body() const { return body_; }

private:
    ASTNode init_;
    ASTNode condition_;
    ASTNode increment_;
    ASTNode body_;
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
