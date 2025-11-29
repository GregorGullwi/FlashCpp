// ===== src/ParserTypes.h (header-only) =====

#pragma once
#include "AstNodeTypes.h"
#include <vector>
#include <optional>
#include <string_view>

namespace FlashCpp {

// Result of parsing a parameter list
struct ParsedParameterList {
	std::vector<ASTNode> parameters;
	bool is_variadic = false;
};

// Unified representation of what kind of function we're parsing
enum class FunctionKind {
	Free,           // Global or namespace-scope function
	Member,         // Non-static member function
	StaticMember,   // Static member function
	Constructor,    // Constructor
	Destructor,     // Destructor
	Operator,       // Operator overload (can be member or free)
	Conversion,     // Conversion operator (operator int())
	Lambda          // Lambda expression (future)
};

// CV and ref qualifiers for member functions
struct MemberQualifiers {
	bool is_const = false;
	bool is_volatile = false;
	bool is_lvalue_ref = false;   // &
	bool is_rvalue_ref = false;   // &&
};

// Function specifiers (can appear after parameters)
struct FunctionSpecifiers {
	bool is_virtual = false;
	bool is_override = false;
	bool is_final = false;
	bool is_pure_virtual = false;  // = 0
	bool is_defaulted = false;     // = default
	bool is_deleted = false;       // = delete
	bool is_noexcept = false;
	std::optional<ASTNode> noexcept_expr;  // For noexcept(expr)
	bool is_implicit = false;      // Compiler-generated (implicit copy ctor, operator=, etc.)
};

// Storage and linkage specifiers
struct StorageSpecifiers {
	bool is_static = false;
	bool is_inline = false;
	bool is_constexpr = false;
	bool is_consteval = false;
	bool is_constinit = false;
	bool is_extern = false;
	Linkage linkage = Linkage::None;
	CallingConvention calling_convention = CallingConvention::Default;
};

// Context for parsing a function (where it lives)
struct FunctionParsingContext {
	FunctionKind kind = FunctionKind::Free;
	std::string_view parent_struct_name;      // For members
	size_t parent_struct_type_index = 0;      // Type index of parent struct
	StructDeclarationNode* parent_struct = nullptr;
	bool is_out_of_line = false;              // A::f defined outside class
	std::vector<std::string_view> template_params;  // Enclosing template params
	AccessSpecifier access = AccessSpecifier::Public;
};

// Result of parsing function header (everything except the body)
struct ParsedFunctionHeader {
	TypeSpecifierNode* return_type = nullptr;
	Token name_token;
	ParsedParameterList params;
	MemberQualifiers member_quals;
	FunctionSpecifiers specifiers;
	StorageSpecifiers storage;
	std::vector<ASTNode> template_params;       // If function template
	std::optional<ASTNode> requires_clause;     // C++20 requires
	std::optional<ASTNode> trailing_return_type;
};

} // namespace FlashCpp
