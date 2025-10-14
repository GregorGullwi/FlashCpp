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

// CV-qualifiers (const/volatile) - separate from sign qualifiers
// These can be combined with type qualifiers using bitwise operations
enum class CVQualifier : uint8_t {
	None = 0,
	Const = 1 << 0,
	Volatile = 1 << 1,
	ConstVolatile = Const | Volatile
};

enum class Type : int_fast16_t {
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

// Access specifier for struct/class members
enum class AccessSpecifier {
	Public,
	Protected,
	Private
};

// Struct member information
struct StructMember {
	std::string name;
	Type type;
	TypeIndex type_index;   // Index into gTypeInfo for complex types (structs, etc.)
	size_t offset;          // Offset in bytes from start of struct
	size_t size;            // Size in bytes
	size_t alignment;       // Alignment requirement
	AccessSpecifier access; // Access level (public/protected/private)

	StructMember(std::string n, Type t, TypeIndex tidx, size_t off, size_t sz, size_t align, AccessSpecifier acc = AccessSpecifier::Public)
		: name(std::move(n)), type(t), type_index(tidx), offset(off), size(sz), alignment(align), access(acc) {}
};

// Struct type information
struct StructTypeInfo {
	std::string name;
	std::vector<StructMember> members;
	size_t total_size = 0;      // Total size of struct in bytes
	size_t alignment = 1;       // Alignment requirement of struct
	AccessSpecifier default_access; // Default access for struct (public) vs class (private)

	StructTypeInfo(std::string n, AccessSpecifier default_acc = AccessSpecifier::Public)
		: name(std::move(n)), default_access(default_acc) {}

	void addMember(const std::string& member_name, Type member_type, TypeIndex type_index,
	               size_t member_size, size_t member_alignment, AccessSpecifier access = AccessSpecifier::Public) {
		// Calculate offset with proper alignment
		size_t offset = (total_size + member_alignment - 1) & ~(member_alignment - 1);

		members.emplace_back(member_name, member_type, type_index, offset, member_size, member_alignment, access);

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

// Get the natural alignment for a type (in bytes)
// This follows the x64 Windows ABI alignment rules
inline size_t get_type_alignment(Type type, size_t type_size_bytes) {
	switch (type) {
		case Type::Void:
			return 1;
		case Type::Bool:
		case Type::Char:
		case Type::UnsignedChar:
			return 1;
		case Type::Short:
		case Type::UnsignedShort:
			return 2;
		case Type::Int:
		case Type::UnsignedInt:
		case Type::Long:
		case Type::UnsignedLong:
		case Type::Float:
			return 4;
		case Type::LongLong:
		case Type::UnsignedLongLong:
		case Type::Double:
			return 8;
		case Type::LongDouble:
			// On x64 Windows, long double is 8 bytes (same as double)
			return 8;
		case Type::Struct:
			// For structs, alignment is determined by the struct's alignment field
			// This should be passed separately
			return type_size_bytes;
		default:
			// For other types, use the size as alignment (up to 8 bytes max on x64)
			return std::min(type_size_bytes, size_t(8));
	}
}

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

// Pointer level information - stores CV-qualifiers for each pointer level
// Example: const int* const* volatile
//   - Level 0 (base): const int
//   - Level 1: const pointer to (const int)
//   - Level 2: volatile pointer to (const pointer to const int)
struct PointerLevel {
	CVQualifier cv_qualifier = CVQualifier::None;

	PointerLevel() = default;
	explicit PointerLevel(CVQualifier cv) : cv_qualifier(cv) {}
};

class TypeSpecifierNode {
public:
	TypeSpecifierNode() = default;
	TypeSpecifierNode(Type type, TypeQualifier qualifier, unsigned char sizeInBits,
		const Token& token = {}, CVQualifier cv_qualifier = CVQualifier::None)
		: type_(type), size_(sizeInBits), qualifier_(qualifier), cv_qualifier_(cv_qualifier), token_(token), type_index_(0) {}

	// Constructor for struct types
	TypeSpecifierNode(Type type, TypeIndex type_index, unsigned char sizeInBits,
		const Token& token = {}, CVQualifier cv_qualifier = CVQualifier::None)
		: type_(type), size_(sizeInBits), qualifier_(TypeQualifier::None), cv_qualifier_(cv_qualifier), token_(token), type_index_(type_index) {}

	auto type() const { return type_; }
	auto size_in_bits() const { return size_; }
	auto qualifier() const { return qualifier_; }
	auto cv_qualifier() const { return cv_qualifier_; }
	auto type_index() const { return type_index_; }
	bool is_const() const { return (static_cast<uint8_t>(cv_qualifier_) & static_cast<uint8_t>(CVQualifier::Const)) != 0; }
	bool is_volatile() const { return (static_cast<uint8_t>(cv_qualifier_) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0; }

	// Pointer support
	bool is_pointer() const { return !pointer_levels_.empty(); }
	size_t pointer_depth() const { return pointer_levels_.empty() ? 0 : pointer_levels_.size(); }
	const std::vector<PointerLevel>& pointer_levels() const { return pointer_levels_; }
	void add_pointer_level(CVQualifier cv = CVQualifier::None) { pointer_levels_.push_back(PointerLevel(cv)); }

	// Get readable string representation
	std::string getReadableString() const;

private:
	Type type_;
	unsigned char size_;
	TypeQualifier qualifier_;
	CVQualifier cv_qualifier_;  // CV-qualifier for the base type
	Token token_;
	TypeIndex type_index_;      // Index into gTypeInfo for user-defined types (structs, etc.)
	std::vector<PointerLevel> pointer_levels_;  // Empty if not a pointer, one entry per * level
};

class DeclarationNode {
public:
	DeclarationNode() = default;
	DeclarationNode(ASTNode type_node, Token identifier)
		: type_node_(type_node), identifier_(std::move(identifier)), array_size_(std::nullopt) {}
	DeclarationNode(ASTNode type_node, Token identifier, std::optional<ASTNode> array_size)
		: type_node_(type_node), identifier_(std::move(identifier)), array_size_(array_size) {}

	ASTNode type_node() const { return type_node_; }
	const Token& identifier_token() const { return identifier_; }
	uint32_t line_number() const { return identifier_.line(); }
	bool is_array() const { return array_size_.has_value(); }
	std::optional<ASTNode> array_size() const { return array_size_; }

private:
	ASTNode type_node_;
	Token identifier_;
	std::optional<ASTNode> array_size_;  // For array declarations like int arr[10]
};

class IdentifierNode {
public:
	explicit IdentifierNode(Token identifier) : identifier_(identifier) {}

	std::optional<Token> try_get_parent_token() { return parent_token_; }
	std::string_view name() const { return identifier_.value(); }

private:
	Token identifier_;
	std::optional<Token> parent_token_;
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

// Struct member with access specifier
struct StructMemberDecl {
	ASTNode declaration;
	AccessSpecifier access;

	StructMemberDecl(ASTNode decl, AccessSpecifier acc)
		: declaration(decl), access(acc) {}
};

class StructDeclarationNode {
public:
	explicit StructDeclarationNode(std::string name, bool is_class = false)
		: name_(std::move(name)), is_class_(is_class) {}

	const std::string& name() const { return name_; }
	const std::vector<StructMemberDecl>& members() const { return members_; }
	bool is_class() const { return is_class_; }
	AccessSpecifier default_access() const {
		return is_class_ ? AccessSpecifier::Private : AccessSpecifier::Public;
	}

	void add_member(ASTNode member, AccessSpecifier access) {
		members_.emplace_back(member, access);
	}

private:
	std::string name_;
	std::vector<StructMemberDecl> members_;
	bool is_class_;  // true for class, false for struct
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

class ArraySubscriptNode {
public:
	explicit ArraySubscriptNode(ASTNode array_expr, ASTNode index_expr, Token bracket_token)
		: array_expr_(array_expr), index_expr_(index_expr), bracket_token_(bracket_token) {}

	ASTNode array_expr() const { return array_expr_; }
	ASTNode index_expr() const { return index_expr_; }
	const Token& bracket_token() const { return bracket_token_; }

private:
	ASTNode array_expr_;
	ASTNode index_expr_;
	Token bracket_token_;
};

using ExpressionNode = std::variant<IdentifierNode, StringLiteralNode, NumericLiteralNode,
	BinaryOperatorNode, UnaryOperatorNode, FunctionCallNode, MemberAccessNode, ArraySubscriptNode>;

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

// ForLoopNode removed - use ForStatementNode instead for C++20 compatibility

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

class InitializerListNode {
public:
	explicit InitializerListNode() {}

	void add_initializer(ASTNode init_expr) {
		initializers_.push_back(init_expr);
	}

	const std::vector<ASTNode>& initializers() const {
		return initializers_;
	}

	size_t size() const {
		return initializers_.size();
	}

private:
	std::vector<ASTNode> initializers_;
};

class VariableDeclarationNode {
public:
	explicit VariableDeclarationNode(ASTNode declaration_node, std::optional<ASTNode> initializer = std::nullopt)
		: declaration_node_(declaration_node), initializer_(initializer) {}

	const DeclarationNode& declaration() const { return declaration_node_.as<DeclarationNode>(); }
	const ASTNode& declaration_node() const { return declaration_node_; }
	const std::optional<ASTNode>& initializer() const { return initializer_; }

private:
	ASTNode declaration_node_;
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

class BreakStatementNode {
public:
	explicit BreakStatementNode(Token break_token = Token())
		: break_token_(break_token) {}

	const Token& break_token() const { return break_token_; }

private:
	Token break_token_;
};

class ContinueStatementNode {
public:
	explicit ContinueStatementNode(Token continue_token = Token())
		: continue_token_(continue_token) {}

	const Token& continue_token() const { return continue_token_; }

private:
	Token continue_token_;
};
