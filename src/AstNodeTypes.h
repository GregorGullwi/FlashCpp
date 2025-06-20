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
#include <memory>

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
	Double,
	LongDouble,
	UserDefined,
	Auto,
	Function,
	Struct,
};

using TypeIndex = size_t;

// Struct member information
struct StructMember {
	std::string name;
	Type type;
	size_t offset;      // Offset in bytes from start of struct
	size_t size;        // Size in bytes
	size_t alignment;   // Alignment requirement

	StructMember(std::string n, Type t, size_t off, size_t sz, size_t align)
		: name(std::move(n)), type(t), offset(off), size(sz), alignment(align) {}
};

// Struct type information
struct StructTypeInfo {
	std::string name;
	std::vector<StructMember> members;
	size_t total_size = 0;      // Total size of struct in bytes
	size_t alignment = 1;       // Alignment requirement of struct

	StructTypeInfo(std::string n) : name(std::move(n)) {}

	void addMember(const std::string& member_name, Type member_type, size_t member_size, size_t member_alignment) {
		// Calculate offset with proper alignment
		size_t offset = (total_size + member_alignment - 1) & ~(member_alignment - 1);

		members.emplace_back(member_name, member_type, offset, member_size, member_alignment);

		// Update struct size and alignment
		total_size = offset + member_size;
		alignment = std::max(alignment, member_alignment);
	}

	void finalize() {
		// Pad struct to its alignment
		total_size = (total_size + alignment - 1) & ~(alignment - 1);
	}

	const StructMember* findMember(const std::string& name) const {
		for (const auto& member : members) {
			if (member.name == name) {
				return &member;
			}
		}
		return nullptr;
	}
};

struct TypeInfo
{
	TypeInfo() : type_(Type::Void), type_index_(0) {}
	TypeInfo(std::string name, Type type, TypeIndex idx) : name_(std::move(name)), type_(type), type_index_(idx) {}

	std::string name_;
	Type type_;
	TypeIndex type_index_;

	// For struct types, store additional information
	std::unique_ptr<StructTypeInfo> struct_info_;

	const char* name() { return name_.c_str(); };

	// Helper methods for struct types
	bool isStruct() const { return type_ == Type::Struct; }
	const StructTypeInfo* getStructInfo() const { return struct_info_.get(); }
	StructTypeInfo* getStructInfo() { return struct_info_.get(); }

	void setStructInfo(std::unique_ptr<StructTypeInfo> info) {
		struct_info_ = std::move(info);
	}
};

extern std::deque<TypeInfo> gTypeInfo;

extern std::unordered_map<std::string, const TypeInfo*> gTypesByName;

extern std::unordered_map<Type, const TypeInfo*> gNativeTypes;

TypeInfo& add_user_type(std::string name);

TypeInfo& add_function_type(std::string name, Type /*return_type*/);

TypeInfo& add_struct_type(std::string name);

void initialize_native_types();

// Type utilities
bool is_integer_type(Type type);
bool is_signed_integer_type(Type type);
bool is_unsigned_integer_type(Type type);
bool is_bool_type(Type type);
bool is_floating_point_type(Type type);
int get_integer_rank(Type type);
int get_floating_point_rank(Type type);
int get_type_size_bits(Type type);
Type promote_integer_type(Type type);
Type promote_floating_point_type(Type type);
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
	uint32_t line_number() const { return identifier_.line(); }

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
	const Token& get_token() const { return identifier_; }
	auto get_lhs() const { return lhs_node_; }
	auto get_rhs() const { return rhs_node_; }

private:
	class Token identifier_;
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
	explicit FunctionCallNode(DeclarationNode& func_decl, ChunkedVector<ASTNode>&& arguments, Token called_from_token)
		: func_decl_(func_decl), arguments_(std::move(arguments)), called_from_(called_from_token) {}

	const auto& arguments() const { return arguments_; }
	const auto& function_declaration() const { return func_decl_; }

	void add_argument(ASTNode argument) { arguments_.push_back(argument); }

	Token called_from() const { return called_from_; }

private:
	DeclarationNode& func_decl_;
	ChunkedVector<ASTNode> arguments_;
	Token called_from_;
};

class StructDeclarationNode {
public:
	explicit StructDeclarationNode(std::string name) : name_(std::move(name)) {}

	const std::string& name() const { return name_; }
	const std::vector<ASTNode>& members() const { return members_; }

	void add_member(ASTNode member) { members_.push_back(member); }

private:
	std::string name_;
	std::vector<ASTNode> members_;
};

class MemberAccessNode {
public:
	explicit MemberAccessNode(ASTNode object, Token member_name)
		: object_(object), member_name_(member_name) {}

	ASTNode object() const { return object_; }
	std::string_view member_name() const { return member_name_.value(); }

private:
	ASTNode object_;
	Token member_name_;
};

using ExpressionNode = std::variant<IdentifierNode, StringLiteralNode, NumericLiteralNode,
	BinaryOperatorNode, UnaryOperatorNode, FunctionCallNode, MemberAccessNode>;

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
		std::optional<ASTNode> expression = std::nullopt, Token return_token = Token())
		: expression_(expression), return_token_(return_token) {}

	std::optional<ASTNode> expression() const { return expression_; }
	const Token& return_token() const { return return_token_; }

private:
	std::optional<ASTNode>
		expression_; // Optional, as a return statement may not have an expression
	Token return_token_;
};

class VariableDeclarationNode {
public:
	explicit VariableDeclarationNode(DeclarationNode declaration, std::optional<ASTNode> initializer = std::nullopt)
		: declaration_(declaration), initializer_(initializer) {}

	const DeclarationNode& declaration() const { return declaration_; }
	std::optional<ASTNode> initializer() const { return initializer_; }

private:
	DeclarationNode declaration_;
	std::optional<ASTNode> initializer_;
};

class IfStatementNode {
public:
	explicit IfStatementNode(ASTNode condition, ASTNode then_statement,
		std::optional<ASTNode> else_statement = std::nullopt,
		std::optional<ASTNode> init_statement = std::nullopt)
		: condition_(condition), then_statement_(then_statement),
		  else_statement_(else_statement), init_statement_(init_statement) {}

	auto get_condition() const { return condition_; }
	auto get_then_statement() const { return then_statement_; }
	auto get_else_statement() const { return else_statement_; }
	auto get_init_statement() const { return init_statement_; }
	bool has_else() const { return else_statement_.has_value(); }
	bool has_init() const { return init_statement_.has_value(); }

private:
	ASTNode condition_;
	ASTNode then_statement_;
	std::optional<ASTNode> else_statement_;
	std::optional<ASTNode> init_statement_; // C++20 if (init; condition)
};

class ForStatementNode {
public:
	explicit ForStatementNode(std::optional<ASTNode> init_statement,
		std::optional<ASTNode> condition,
		std::optional<ASTNode> update_expression,
		ASTNode body_statement)
		: init_statement_(init_statement), condition_(condition),
		  update_expression_(update_expression), body_statement_(body_statement) {}

	auto get_init_statement() const { return init_statement_; }
	auto get_condition() const { return condition_; }
	auto get_update_expression() const { return update_expression_; }
	auto get_body_statement() const { return body_statement_; }
	bool has_init() const { return init_statement_.has_value(); }
	bool has_condition() const { return condition_.has_value(); }
	bool has_update() const { return update_expression_.has_value(); }

private:
	std::optional<ASTNode> init_statement_;    // for (init; condition; update)
	std::optional<ASTNode> condition_;
	std::optional<ASTNode> update_expression_;
	ASTNode body_statement_;
};

class WhileStatementNode {
public:
	explicit WhileStatementNode(ASTNode condition, ASTNode body_statement)
		: condition_(condition), body_statement_(body_statement) {}

	auto get_condition() const { return condition_; }
	auto get_body_statement() const { return body_statement_; }

private:
	ASTNode condition_;
	ASTNode body_statement_;
};

class DoWhileStatementNode {
public:
	explicit DoWhileStatementNode(ASTNode body_statement, ASTNode condition)
		: body_statement_(body_statement), condition_(condition) {}

	auto get_body_statement() const { return body_statement_; }
	auto get_condition() const { return condition_; }

private:
	ASTNode body_statement_;
	ASTNode condition_;
};
