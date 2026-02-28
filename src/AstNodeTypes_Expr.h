#pragma once
#include "AstNodeTypes_Template.h"

};

// Namespace declaration node
class NamespaceDeclarationNode {
public:
	explicit NamespaceDeclarationNode(std::string_view name)
		: name_(name) {}

	std::string_view name() const { return name_; }
	const std::vector<ASTNode>& declarations() const { return declarations_; }
	bool is_anonymous() const { return name_.empty(); }

	void add_declaration(ASTNode decl) {
		declarations_.push_back(decl);
	}

private:
	std::string_view name_;  // Points directly into source text from lexer token (empty for anonymous namespaces)
	std::vector<ASTNode> declarations_;  // Declarations within the namespace
};

// Using directive node: using namespace std;
class UsingDirectiveNode {
public:
	explicit UsingDirectiveNode(NamespaceHandle namespace_handle, Token using_token)
		: namespace_handle_(namespace_handle), using_token_(using_token) {}

	NamespaceHandle namespace_handle() const { return namespace_handle_; }
	const Token& using_token() const { return using_token_; }

private:
	NamespaceHandle namespace_handle_;  // Handle to namespace, e.g., handle for "std::filesystem"
	Token using_token_;  // For error reporting
};

// Using declaration node: using std::vector;
class UsingDeclarationNode {
public:
	explicit UsingDeclarationNode(NamespaceHandle namespace_handle, Token identifier, Token using_token)
		: namespace_handle_(namespace_handle), identifier_(identifier), using_token_(using_token) {}

	NamespaceHandle namespace_handle() const { return namespace_handle_; }
	std::string_view identifier_name() const { return identifier_.value(); }
	const Token& identifier_token() const { return identifier_; }
	const Token& using_token() const { return using_token_; }

private:
	NamespaceHandle namespace_handle_;  // Handle to namespace, e.g., handle for "std" in "using std::vector;"
	Token identifier_;  // The identifier being imported (e.g., "vector")
	Token using_token_;  // For error reporting
};

// C++20 using enum declaration: using enum EnumType;
// Brings all enumerators of a scoped enum into the current scope
class UsingEnumNode {
public:
	explicit UsingEnumNode(StringHandle enum_type_name, Token using_token)
		: enum_type_name_(enum_type_name), using_token_(using_token) {}

	StringHandle enum_type_name() const { return enum_type_name_; }
	const Token& using_token() const { return using_token_; }

private:
	StringHandle enum_type_name_;  // Name of the enum type (e.g., "Color")
	Token using_token_;  // For error reporting
};

// Namespace alias node: namespace fs = std::filesystem;
class NamespaceAliasNode {
public:
	explicit NamespaceAliasNode(Token alias_name, NamespaceHandle target_namespace)
		: alias_name_(alias_name), target_namespace_(target_namespace) {}

	std::string_view alias_name() const { return alias_name_.value(); }
	NamespaceHandle target_namespace() const { return target_namespace_; }
	const Token& alias_token() const { return alias_name_; }

private:
	Token alias_name_;  // The alias (e.g., "fs")
	NamespaceHandle target_namespace_;  // Handle to target namespace, e.g., handle for "std::filesystem"
};

// Enumerator node - represents a single enumerator in an enum
class EnumeratorNode {
public:
	EnumeratorNode(Token name, std::optional<ASTNode> value = std::nullopt)
		: name_(name), value_(value) {}

	std::string_view name() const { return name_.value(); }
	const Token& name_token() const { return name_; }
	bool has_value() const { return value_.has_value(); }
	const std::optional<ASTNode>& value() const { return value_; }

private:
	Token name_;                    // Enumerator name
	std::optional<ASTNode> value_;  // Optional initializer expression
};

// Enum declaration node - represents enum or enum class
class EnumDeclarationNode {
public:
	explicit EnumDeclarationNode(StringHandle name_handle, bool is_scoped = false)
		: name_(StringTable::getStringView(name_handle)), is_scoped_(is_scoped), is_forward_declaration_(false), underlying_type_() {}

	std::string_view name() const { return name_; }
	bool is_scoped() const { return is_scoped_; }  // true for enum class, false for enum
	bool is_forward_declaration() const { return is_forward_declaration_; }
	bool has_underlying_type() const { return underlying_type_.has_value(); }
	const std::optional<ASTNode>& underlying_type() const { return underlying_type_; }
	const std::vector<ASTNode>& enumerators() const { return enumerators_; }

	void set_underlying_type(ASTNode type) {
		underlying_type_ = type;
	}

	void set_is_forward_declaration(bool value) {
		is_forward_declaration_ = value;
	}

	void add_enumerator(ASTNode enumerator) {
		enumerators_.push_back(enumerator);
	}

private:
	std::string_view name_;                 // Points directly into source text from lexer token
	bool is_scoped_;                        // true for enum class, false for enum
	bool is_forward_declaration_;           // true for forward declarations without body
	std::optional<ASTNode> underlying_type_; // Optional underlying type (TypeSpecifierNode)
	std::vector<ASTNode> enumerators_;      // List of EnumeratorNode
};

class MemberAccessNode {
public:
	explicit MemberAccessNode(ASTNode object, Token member_name, bool is_arrow = false)
		: object_(object), member_name_(member_name), is_arrow_(is_arrow) {}

	ASTNode object() const { return object_; }
	std::string_view member_name() const { return member_name_.value(); }
	const Token& member_token() const { return member_name_; }
	bool is_arrow() const { return is_arrow_; }

private:
	ASTNode object_;
	Token member_name_;
	bool is_arrow_;  // True if accessed via -> instead of .
};

// Pointer-to-member access node: obj.*ptr_to_member or obj->*ptr_to_member
// Used in patterns like: (declval<T>().*declval<Fp>())(args...)
// The RHS is an expression (pointer to member), not a simple identifier
class PointerToMemberAccessNode {
public:
	explicit PointerToMemberAccessNode(ASTNode object, ASTNode member_pointer, Token operator_token, bool is_arrow)
		: object_(object), member_pointer_(member_pointer), operator_token_(operator_token), is_arrow_(is_arrow) {}

	ASTNode object() const { return object_; }
	ASTNode member_pointer() const { return member_pointer_; }
	const Token& operator_token() const { return operator_token_; }
	bool is_arrow() const { return is_arrow_; }  // true for ->*, false for .*
	std::string_view op() const { return is_arrow_ ? "->*" : ".*"; }

private:
	ASTNode object_;           // The object expression (LHS)
	ASTNode member_pointer_;   // The pointer-to-member expression (RHS)
	Token operator_token_;     // The operator token (for error reporting)
	bool is_arrow_;            // true for ->*, false for .*
};

// Member function call node (e.g., obj.method(args))
class MemberFunctionCallNode {
public:
	explicit MemberFunctionCallNode(ASTNode object, const FunctionDeclarationNode& func_decl,
	                                ChunkedVector<ASTNode>&& arguments, Token called_from_token)
		: object_(object), func_decl_(func_decl), arguments_(std::move(arguments)), called_from_(called_from_token) {}

	ASTNode object() const { return object_; }
	const auto& arguments() const { return arguments_; }
	const auto& function_declaration() const { return func_decl_; }
	Token called_from() const { return called_from_; }

	void add_argument(ASTNode argument) { arguments_.push_back(argument); }

private:
	ASTNode object_;                    // The object on which the method is called
	const FunctionDeclarationNode& func_decl_; // The member function declaration
	ChunkedVector<ASTNode> arguments_;   // Arguments to the function call
	Token called_from_;                  // Token for error reporting
};

// Pseudo-destructor call: obj.~Type() or ptr->~Type()
// Used in patterns like: decltype(declval<T&>().~T())
// The result type is always void
class PseudoDestructorCallNode {
public:
	// Constructor for simple type names: obj.~Type()
	explicit PseudoDestructorCallNode(ASTNode object, Token type_name_token, bool is_arrow)
		: object_(object), qualified_type_name_(), type_name_token_(type_name_token), is_arrow_access_(is_arrow) {}
	
	// Constructor with qualified type: obj.~std::string()
	explicit PseudoDestructorCallNode(ASTNode object, std::string_view qualified_type_name, Token type_name_token, bool is_arrow)
		: object_(object), qualified_type_name_(StringTable::getOrInternStringHandle(qualified_type_name)), type_name_token_(type_name_token), is_arrow_access_(is_arrow) {}

	ASTNode object() const { return object_; }
	std::string_view type_name() const { return type_name_token_.value(); }
	// Returns the qualified type name handle if present (empty handle if simple name)
	StringHandle qualified_type_name() const { return qualified_type_name_; }
	bool has_qualified_name() const { return qualified_type_name_.isValid(); }
	const Token& type_name_token() const { return type_name_token_; }
	bool is_arrow_access() const { return is_arrow_access_; }

private:
	ASTNode object_;                    // The object on which destructor is called
	StringHandle qualified_type_name_;  // Full qualified name for types like std::string (empty if simple name)
	Token type_name_token_;             // The type name token after ~
	bool is_arrow_access_;              // true for ptr->~Type(), false for obj.~Type()
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

// sizeof operator node - can take either a type or an expression
class SizeofExprNode {
public:
	// Constructor for sizeof(type)
	explicit SizeofExprNode(ASTNode type_node, Token sizeof_token)
		: type_or_expr_(type_node), sizeof_token_(sizeof_token), is_type_(true) {}

	// Constructor for sizeof(expression)
	static SizeofExprNode from_expression(ASTNode expr_node, Token sizeof_token) {
		SizeofExprNode node(expr_node, sizeof_token);
		node.is_type_ = false;
		return node;
	}

	ASTNode type_or_expr() const { return type_or_expr_; }
	const Token& sizeof_token() const { return sizeof_token_; }
	bool is_type() const { return is_type_; }

private:
	ASTNode type_or_expr_;  // Either TypeSpecifierNode or ExpressionNode
	Token sizeof_token_;
	bool is_type_;
};

// sizeof... operator node - returns the number of elements in a parameter pack
class SizeofPackNode {
public:
	explicit SizeofPackNode(std::string_view pack_name, Token sizeof_token)
		: pack_name_(pack_name), sizeof_token_(sizeof_token) {}

	std::string_view pack_name() const { return pack_name_; }
	const Token& sizeof_token() const { return sizeof_token_; }

private:
	std::string_view pack_name_;  // Name of the parameter pack
	Token sizeof_token_;
};

// alignof operator node - returns the alignment requirement of a type
class AlignofExprNode {
public:
	// Constructor for alignof(type)
	explicit AlignofExprNode(ASTNode type_node, Token alignof_token)
		: type_or_expr_(type_node), alignof_token_(alignof_token), is_type_(true) {}

	// Constructor for alignof(expression)
	static AlignofExprNode from_expression(ASTNode expr_node, Token alignof_token) {
		AlignofExprNode node(expr_node, alignof_token);
		node.is_type_ = false;
		return node;
	}

	ASTNode type_or_expr() const { return type_or_expr_; }
	const Token& alignof_token() const { return alignof_token_; }
	bool is_type() const { return is_type_; }

private:
	ASTNode type_or_expr_;  // Either TypeSpecifierNode or ExpressionNode
	Token alignof_token_;
	bool is_type_;
};

// noexcept operator node - returns true if expression is noexcept, false otherwise
// This is the noexcept(expr) operator, not the noexcept specifier
class NoexceptExprNode {
public:
	// Constructor for noexcept(expression)
	explicit NoexceptExprNode(ASTNode expr_node, Token noexcept_token)
		: expr_(expr_node), noexcept_token_(noexcept_token) {}

	ASTNode expr() const { return expr_; }
	const Token& noexcept_token() const { return noexcept_token_; }

private:
	ASTNode expr_;  // The expression to check
	Token noexcept_token_;
};

// offsetof operator node - offsetof(struct_type, member)
class OffsetofExprNode {
public:
	explicit OffsetofExprNode(ASTNode type_node, Token member_name, Token offsetof_token)
		: type_node_(type_node), member_name_(member_name), offsetof_token_(offsetof_token) {}

	ASTNode type_node() const { return type_node_; }
	std::string_view member_name() const { return member_name_.value(); }
	const Token& offsetof_token() const { return offsetof_token_; }

private:
	ASTNode type_node_;      // TypeSpecifierNode for the struct type
	Token member_name_;      // Name of the member
	Token offsetof_token_;
};

// Type trait intrinsic expression node - __is_void(T), __is_integral(T), etc.
enum class TypeTraitKind {
	// Primary type categories
	IsVoid,
	IsNullptr,
	IsIntegral,
	IsFloatingPoint,
	IsArray,
	IsPointer,
	IsLvalueReference,
	IsRvalueReference,
	IsMemberObjectPointer,
	IsMemberFunctionPointer,
	IsEnum,
	IsUnion,
	IsClass,
	IsFunction,
	// Composite type categories
	IsReference,        // __is_reference - lvalue or rvalue reference
	IsArithmetic,       // __is_arithmetic - integral or floating point
	IsFundamental,      // __is_fundamental - void, nullptr, arithmetic
	IsObject,           // __is_object - not function, not reference, not void
	IsScalar,           // __is_scalar - arithmetic, pointer, enum, member pointer, nullptr
	IsCompound,         // __is_compound - array, function, pointer, reference, class, union, enum, member pointer
	// Type relationships (binary trait - takes 2 types)
	IsBaseOf,
	IsSame,
	IsConvertible,      // __is_convertible(From, To) - check if From can convert to To
	IsNothrowConvertible,  // __is_nothrow_convertible(From, To) - check if From can convert to To without throwing
	IsAssignable,
	IsTriviallyAssignable,
	IsNothrowAssignable,
	IsLayoutCompatible,
	IsPointerInterconvertibleBaseOf,
	// Type properties
	IsConst,            // __is_const - has const qualifier
	IsVolatile,         // __is_volatile - has volatile qualifier
	IsSigned,           // __is_signed - signed integral type
	IsUnsigned,         // __is_unsigned - unsigned integral type
	IsBoundedArray,     // __is_bounded_array - array with known bound
	IsUnboundedArray,   // __is_unbounded_array - array with unknown bound
	IsPolymorphic,
	IsFinal,
	IsAbstract,
	IsEmpty,
	IsAggregate,         // __is_aggregate - type is an aggregate
	IsStandardLayout,
	HasUniqueObjectRepresentations,
	IsTriviallyCopyable,
	IsTrivial,
	IsPod,
	IsLiteralType,       // __is_literal_type - deprecated in C++17, removed in C++20
	// Constructibility traits (variadic - takes T + Args...)
	IsConstructible,
	IsTriviallyConstructible,
	IsNothrowConstructible,
	// Destructibility traits (unary)
	IsDestructible,
	IsTriviallyDestructible,
	IsNothrowDestructible,
	HasTrivialDestructor,    // __has_trivial_destructor(T) - GCC/Clang intrinsic, equivalent to IsTriviallyDestructible
	HasVirtualDestructor,    // __has_virtual_destructor(T) - check if type has virtual destructor
	// Special traits
	UnderlyingType,      // __underlying_type(T) - returns the underlying type of an enum
	IsConstantEvaluated, // __is_constant_evaluated() - no arguments, returns bool
	IsCompleteOrUnbounded // __is_complete_or_unbounded - helper for standard library, always returns true
};

class TypeTraitExprNode {
public:
	// Constructor for unary type traits (single type argument)
	explicit TypeTraitExprNode(TypeTraitKind kind, ASTNode type_node, Token trait_token)
		: kind_(kind), type_node_(type_node), second_type_node_(std::nullopt), additional_type_nodes_(), trait_token_(trait_token) {}

	// Constructor for binary type traits (two type arguments, like __is_base_of, __is_assignable)
	explicit TypeTraitExprNode(TypeTraitKind kind, ASTNode type_node, ASTNode second_type_node, Token trait_token)
		: kind_(kind), type_node_(type_node), second_type_node_(second_type_node), additional_type_nodes_(), trait_token_(trait_token) {}

	// Constructor for variadic type traits (T + Args..., like __is_constructible)
	explicit TypeTraitExprNode(TypeTraitKind kind, ASTNode type_node, std::vector<ASTNode> additional_types, Token trait_token)
		: kind_(kind), type_node_(type_node), second_type_node_(std::nullopt), additional_type_nodes_(std::move(additional_types)), trait_token_(trait_token) {}

	// Constructor for no-argument traits (like __is_constant_evaluated)
	explicit TypeTraitExprNode(TypeTraitKind kind, Token trait_token)
		: kind_(kind), type_node_(), second_type_node_(std::nullopt), additional_type_nodes_(), trait_token_(trait_token) {}

	TypeTraitKind kind() const { return kind_; }
	ASTNode type_node() const { return type_node_; }
	bool has_type() const { return type_node_.is<TypeSpecifierNode>(); }
	bool has_second_type() const { return second_type_node_.has_value(); }
	ASTNode second_type_node() const { return second_type_node_.value_or(ASTNode()); }
	const std::vector<ASTNode>& additional_type_nodes() const { return additional_type_nodes_; }
	const Token& trait_token() const { return trait_token_; }

	// Check if this is a binary trait (takes exactly 2 types)
	bool is_binary_trait() const {
		return kind_ == TypeTraitKind::IsBaseOf ||
		       kind_ == TypeTraitKind::IsSame ||
		       kind_ == TypeTraitKind::IsConvertible ||
		       kind_ == TypeTraitKind::IsNothrowConvertible ||
		       kind_ == TypeTraitKind::IsAssignable ||
		       kind_ == TypeTraitKind::IsTriviallyAssignable ||
		       kind_ == TypeTraitKind::IsNothrowAssignable ||
		       kind_ == TypeTraitKind::IsLayoutCompatible ||
		       kind_ == TypeTraitKind::IsPointerInterconvertibleBaseOf;
	}

	// Check if this is a variadic trait (takes T + Args...)
	bool is_variadic_trait() const {
		return kind_ == TypeTraitKind::IsConstructible ||
		       kind_ == TypeTraitKind::IsTriviallyConstructible ||
		       kind_ == TypeTraitKind::IsNothrowConstructible;
	}

	// Check if this is a no-argument trait
	bool is_no_arg_trait() const {
		return kind_ == TypeTraitKind::IsConstantEvaluated;
	}

	// Get the string name of the trait for error messages
	std::string_view trait_name() const {
		switch (kind_) {
			case TypeTraitKind::IsVoid: return "__is_void";
			case TypeTraitKind::IsNullptr: return "__is_nullptr";
			case TypeTraitKind::IsIntegral: return "__is_integral";
			case TypeTraitKind::IsFloatingPoint: return "__is_floating_point";
			case TypeTraitKind::IsArray: return "__is_array";
			case TypeTraitKind::IsPointer: return "__is_pointer";
			case TypeTraitKind::IsLvalueReference: return "__is_lvalue_reference";
			case TypeTraitKind::IsRvalueReference: return "__is_rvalue_reference";
			case TypeTraitKind::IsMemberObjectPointer: return "__is_member_object_pointer";
			case TypeTraitKind::IsMemberFunctionPointer: return "__is_member_function_pointer";
			case TypeTraitKind::IsEnum: return "__is_enum";
			case TypeTraitKind::IsUnion: return "__is_union";
			case TypeTraitKind::IsClass: return "__is_class";
			case TypeTraitKind::IsFunction: return "__is_function";
			case TypeTraitKind::IsReference: return "__is_reference";
			case TypeTraitKind::IsArithmetic: return "__is_arithmetic";
			case TypeTraitKind::IsFundamental: return "__is_fundamental";
			case TypeTraitKind::IsObject: return "__is_object";
			case TypeTraitKind::IsScalar: return "__is_scalar";
			case TypeTraitKind::IsCompound: return "__is_compound";
			case TypeTraitKind::IsBaseOf: return "__is_base_of";
			case TypeTraitKind::IsSame: return "__is_same";
			case TypeTraitKind::IsConvertible: return "__is_convertible";
			case TypeTraitKind::IsNothrowConvertible: return "__is_nothrow_convertible";
			case TypeTraitKind::IsConst: return "__is_const";
			case TypeTraitKind::IsVolatile: return "__is_volatile";
			case TypeTraitKind::IsSigned: return "__is_signed";
			case TypeTraitKind::IsUnsigned: return "__is_unsigned";
			case TypeTraitKind::IsBoundedArray: return "__is_bounded_array";
			case TypeTraitKind::IsUnboundedArray: return "__is_unbounded_array";
			case TypeTraitKind::IsPolymorphic: return "__is_polymorphic";
			case TypeTraitKind::IsFinal: return "__is_final";
			case TypeTraitKind::IsAbstract: return "__is_abstract";
			case TypeTraitKind::IsEmpty: return "__is_empty";
			case TypeTraitKind::IsAggregate: return "__is_aggregate";
			case TypeTraitKind::IsStandardLayout: return "__is_standard_layout";
			case TypeTraitKind::HasUniqueObjectRepresentations: return "__has_unique_object_representations";
			case TypeTraitKind::IsTriviallyCopyable: return "__is_trivially_copyable";
			case TypeTraitKind::IsTrivial: return "__is_trivial";
			case TypeTraitKind::IsPod: return "__is_pod";
			case TypeTraitKind::IsLiteralType: return "__is_literal_type";
			case TypeTraitKind::IsConstructible: return "__is_constructible";
			case TypeTraitKind::IsTriviallyConstructible: return "__is_trivially_constructible";
			case TypeTraitKind::IsNothrowConstructible: return "__is_nothrow_constructible";
			case TypeTraitKind::IsAssignable: return "__is_assignable";
			case TypeTraitKind::IsTriviallyAssignable: return "__is_trivially_assignable";
			case TypeTraitKind::IsNothrowAssignable: return "__is_nothrow_assignable";
			case TypeTraitKind::IsDestructible: return "__is_destructible";
			case TypeTraitKind::IsTriviallyDestructible: return "__is_trivially_destructible";
			case TypeTraitKind::IsNothrowDestructible: return "__is_nothrow_destructible";
			case TypeTraitKind::UnderlyingType: return "__underlying_type";
			case TypeTraitKind::IsConstantEvaluated: return "__is_constant_evaluated";
			case TypeTraitKind::IsLayoutCompatible: return "__is_layout_compatible";
			case TypeTraitKind::IsPointerInterconvertibleBaseOf: return "__is_pointer_interconvertible_base_of";
			case TypeTraitKind::HasTrivialDestructor: return "__has_trivial_destructor";
			case TypeTraitKind::HasVirtualDestructor: return "__has_virtual_destructor";
			case TypeTraitKind::IsCompleteOrUnbounded: return "__is_complete_or_unbounded";
			default: return "__unknown_trait";
		}
	}

private:
	TypeTraitKind kind_;
	ASTNode type_node_;      // TypeSpecifierNode for the first type argument
	std::optional<ASTNode> second_type_node_;  // TypeSpecifierNode for the second type argument (for binary traits)
	std::vector<ASTNode> additional_type_nodes_;  // Additional type arguments (for variadic traits like __is_constructible)
	Token trait_token_;      // Token for the trait (for error reporting)
};

// New expression node: new Type, new Type(args), new Type[size], new (address) Type
class NewExpressionNode {
public:
	explicit NewExpressionNode(ASTNode type_node, bool is_array = false,
	                          std::optional<ASTNode> size_expr = std::nullopt,
	                          ChunkedVector<ASTNode, 128, 256> constructor_args = {},
	                          std::optional<ASTNode> placement_address = std::nullopt)
		: type_node_(type_node), is_array_(is_array),
		  size_expr_(size_expr), constructor_args_(std::move(constructor_args)),
		  placement_address_(placement_address) {}

	const ASTNode& type_node() const { return type_node_; }
	bool is_array() const { return is_array_; }
	const std::optional<ASTNode>& size_expr() const { return size_expr_; }
	const ChunkedVector<ASTNode, 128, 256>& constructor_args() const { return constructor_args_; }
	const std::optional<ASTNode>& placement_address() const { return placement_address_; }

private:
	ASTNode type_node_;  // TypeSpecifierNode
	bool is_array_;      // true for new[], false for new
	std::optional<ASTNode> size_expr_;  // For new Type[size], the size expression
	ChunkedVector<ASTNode, 128, 256> constructor_args_;  // For new Type(args)
	std::optional<ASTNode> placement_address_;  // For new (address) Type, the placement address
};

// Delete expression node: delete ptr, delete[] ptr
class DeleteExpressionNode {
public:
	explicit DeleteExpressionNode(ASTNode expr, bool is_array = false)
		: expr_(expr), is_array_(is_array) {}

	const ASTNode& expr() const { return expr_; }
	bool is_array() const { return is_array_; }

private:
	ASTNode expr_;       // Expression to delete
	bool is_array_;      // true for delete[], false for delete
};

// Static cast expression node: static_cast<Type>(expr)
class StaticCastNode {
public:
	explicit StaticCastNode(ASTNode target_type, ASTNode expr, Token cast_token)
		: target_type_(target_type), expr_(expr), cast_token_(cast_token) {}

	const ASTNode& target_type() const { return target_type_; }
	const ASTNode& expr() const { return expr_; }
	const Token& cast_token() const { return cast_token_; }

private:
	ASTNode target_type_;  // TypeSpecifierNode - the type to cast to
	ASTNode expr_;         // ExpressionNode - the expression to cast
	Token cast_token_;     // Token for error reporting
};

// Dynamic cast expression node: dynamic_cast<Type>(expr)
class DynamicCastNode {
public:
	explicit DynamicCastNode(ASTNode target_type, ASTNode expr, Token cast_token)
		: target_type_(target_type), expr_(expr), cast_token_(cast_token) {}

	const ASTNode& target_type() const { return target_type_; }
	const ASTNode& expr() const { return expr_; }
	const Token& cast_token() const { return cast_token_; }

private:
	ASTNode target_type_;  // TypeSpecifierNode - the type to cast to (must be pointer or reference)
	ASTNode expr_;         // ExpressionNode - the expression to cast (must be polymorphic)
	Token cast_token_;     // Token for error reporting
};

// Const cast expression node: const_cast<Type>(expr)
class ConstCastNode {
public:
	explicit ConstCastNode(ASTNode target_type, ASTNode expr, Token cast_token)
		: target_type_(target_type), expr_(expr), cast_token_(cast_token) {}

	const ASTNode& target_type() const { return target_type_; }
	const ASTNode& expr() const { return expr_; }
	const Token& cast_token() const { return cast_token_; }

private:
	ASTNode target_type_;  // TypeSpecifierNode - the type to cast to (adds/removes const/volatile)
	ASTNode expr_;         // ExpressionNode - the expression to cast
	Token cast_token_;     // Token for error reporting
};

// Reinterpret cast expression node: reinterpret_cast<Type>(expr)
class ReinterpretCastNode {
public:
	explicit ReinterpretCastNode(ASTNode target_type, ASTNode expr, Token cast_token)
		: target_type_(target_type), expr_(expr), cast_token_(cast_token) {}

	const ASTNode& target_type() const { return target_type_; }
	const ASTNode& expr() const { return expr_; }
	const Token& cast_token() const { return cast_token_; }

private:
	ASTNode target_type_;  // TypeSpecifierNode - the type to cast to (bit pattern reinterpretation)
	ASTNode expr_;         // ExpressionNode - the expression to cast
	Token cast_token_;     // Token for error reporting
};

// Typeid expression node: typeid(expr) or typeid(Type)
class TypeidNode {
public:
	// Constructor for typeid(expr)
	explicit TypeidNode(ASTNode operand, bool is_type, Token typeid_token)
		: operand_(operand), is_type_(is_type), typeid_token_(typeid_token) {}

	const ASTNode& operand() const { return operand_; }
	bool is_type() const { return is_type_; }  // true if operand is a type, false if expression
	const Token& typeid_token() const { return typeid_token_; }

private:
	ASTNode operand_;      // Either TypeSpecifierNode or ExpressionNode
	bool is_type_;         // true for typeid(Type), false for typeid(expr)
	Token typeid_token_;   // Token for error reporting
};

// Lambda capture node - represents a single capture in a lambda
class LambdaCaptureNode {
public:
	enum class CaptureKind {
		ByValue,      // [x]
		ByReference,  // [&x]
		AllByValue,   // [=]
		AllByReference, // [&]
		This,         // [this]
		CopyThis      // [*this] (C++17)
	};

	explicit LambdaCaptureNode(CaptureKind kind, Token identifier = Token(), std::optional<ASTNode> initializer = std::nullopt)
		: kind_(kind), identifier_(identifier), initializer_(initializer) {}

	CaptureKind kind() const { return kind_; }
	std::string_view identifier_name() const { return identifier_.value(); }
	const Token& identifier_token() const { return identifier_; }
	bool is_capture_all() const {
		return kind_ == CaptureKind::AllByValue || kind_ == CaptureKind::AllByReference;
	}
	bool has_initializer() const { return initializer_.has_value(); }
	const std::optional<ASTNode>& initializer() const { return initializer_; }

private:
	CaptureKind kind_;
	Token identifier_;  // Empty for capture-all and [this]
	std::optional<ASTNode> initializer_;  // For init-captures like [x = expr]
};

// Lambda expression node
class LambdaExpressionNode {
public:
	explicit LambdaExpressionNode(
		std::vector<LambdaCaptureNode> captures,
		std::vector<ASTNode> parameters,
		ASTNode body,
		std::optional<ASTNode> return_type = std::nullopt,
		Token lambda_token = Token(),
		bool is_mutable = false,
		std::vector<std::string_view> template_params = {},
		bool is_noexcept = false,
		bool is_constexpr = false,
		bool is_consteval = false)
		: captures_(std::move(captures)),
		  parameters_(std::move(parameters)),
		  body_(body),
		  return_type_(return_type),
		  lambda_token_(lambda_token),
		  lambda_id_(next_lambda_id_++),
		  is_mutable_(is_mutable),
		  template_params_(std::move(template_params)),
		  is_noexcept_(is_noexcept),
		  is_constexpr_(is_constexpr),
		  is_consteval_(is_consteval) {}

	const std::vector<LambdaCaptureNode>& captures() const { return captures_; }
	const std::vector<ASTNode>& parameters() const { return parameters_; }
	const ASTNode& body() const { return body_; }
	const std::optional<ASTNode>& return_type() const { return return_type_; }
	const Token& lambda_token() const { return lambda_token_; }
	size_t lambda_id() const { return lambda_id_; }
	bool is_mutable() const { return is_mutable_; }
	const std::vector<std::string_view>& template_params() const { return template_params_; }
	bool has_template_params() const { return !template_params_.empty(); }
	bool is_noexcept() const { return is_noexcept_; }
	bool is_constexpr() const { return is_constexpr_; }
	bool is_consteval() const { return is_consteval_; }

	// Generate a unique name for the lambda's generated function
	StringHandle generate_lambda_name() const {
		return StringTable::getOrInternStringHandle(StringBuilder().append("__lambda_"sv).append(lambda_id_));
	}

private:
	std::vector<LambdaCaptureNode> captures_;
	std::vector<ASTNode> parameters_;
	ASTNode body_;
	std::optional<ASTNode> return_type_;  // Optional return type (e.g., -> int)
	Token lambda_token_;  // For error reporting
	size_t lambda_id_;    // Unique ID for this lambda
	bool is_mutable_;     // Whether the lambda is marked as mutable
	std::vector<std::string_view> template_params_;  // C++20 template lambda params
	bool is_noexcept_;    // Whether the lambda is noexcept
	bool is_constexpr_;   // Whether the lambda is constexpr
	bool is_consteval_;   // Whether the lambda is consteval

	static inline size_t next_lambda_id_ = 0;  // Counter for generating unique IDs
};

// Template parameter reference node - represents a reference to a template parameter in expressions
class TemplateParameterReferenceNode {
public:
	explicit TemplateParameterReferenceNode(StringHandle param_name, Token token)
		: param_name_(param_name), token_(token) {}

	StringHandle param_name() const { return param_name_; }
	const Token& token() const { return token_; }

private:
	StringHandle param_name_;  // Name of the template parameter being referenced
	Token token_;                  // Token for error reporting
};

// InitializerListConstructionNode - represents the compiler-generated construction of std::initializer_list
// This is the "compiler magic" that creates a backing array and initializer_list from a braced-init-list
// e.g., Container c{1, 2, 3}; where Container takes std::initializer_list<int>
// The backing array lives until the end of the full-expression (on the stack)
class InitializerListConstructionNode {
public:
	InitializerListConstructionNode(
		ASTNode element_type,           // Type of elements (e.g., int in initializer_list<int>)
		ASTNode target_type,            // The full initializer_list type
		std::vector<ASTNode> elements,  // The initializer expressions {1, 2, 3}
		Token called_from
	) : element_type_(std::move(element_type)), target_type_(std::move(target_type)),
	    elements_(std::move(elements)), called_from_(called_from) {}

	const ASTNode& element_type() const { return element_type_; }
	const ASTNode& target_type() const { return target_type_; }
	const std::vector<ASTNode>& elements() const { return elements_; }
	size_t size() const { return elements_.size(); }
	const Token& called_from() const { return called_from_; }

private:
	ASTNode element_type_;           // Element type (e.g., TypeSpecifierNode for int)
	ASTNode target_type_;            // Full initializer_list type
	std::vector<ASTNode> elements_;  // The braced initializer expressions
	Token called_from_;              // For error reporting
};

// Throw expression node: throw or throw expr
// Unlike ThrowStatementNode which is used as a statement, ThrowExpressionNode is used
// when throw is part of an expression (e.g., inside parentheses: (throw bad_access()))
class ThrowExpressionNode {
public:
	// throw expression
	explicit ThrowExpressionNode(ASTNode expression, Token throw_token)
		: expression_(expression), throw_token_(throw_token), is_rethrow_(false) {}

	// throw (rethrow)
	explicit ThrowExpressionNode(Token throw_token)
		: expression_(), throw_token_(throw_token), is_rethrow_(true) {}

	const std::optional<ASTNode>& expression() const { return expression_; }
	bool is_rethrow() const { return is_rethrow_; }
	const Token& throw_token() const { return throw_token_; }

private:
	std::optional<ASTNode> expression_;  // The expression to throw (nullopt for rethrow)
	Token throw_token_;                   // For error reporting
	bool is_rethrow_;                     // True if this is a rethrow (throw)
};

// ============================================================================
// SEH / Concepts expression nodes
// ============================================================================

// SEH filter expression node: the expression in __except(filter_expression)
// Returns EXCEPTION_EXECUTE_HANDLER (1), EXCEPTION_CONTINUE_SEARCH (0), or EXCEPTION_CONTINUE_EXECUTION (-1)
class SehFilterExpressionNode {
public:
	explicit SehFilterExpressionNode(ASTNode expression, Token except_token)
		: expression_(expression), except_token_(except_token) {}

	const ASTNode& expression() const { return expression_; }
	const Token& except_token() const { return except_token_; }

private:
	ASTNode expression_;     // The filter expression
	Token except_token_;     // For error reporting
};

// Requires expression node: requires { expression; }
// Used inside concept definitions and requires clauses
class RequiresExpressionNode {
public:
	explicit RequiresExpressionNode(
		std::vector<ASTNode> requirements,
		Token requires_token = Token())
		: requirements_(std::move(requirements)),
		  requires_token_(requires_token) {}

	const std::vector<ASTNode>& requirements() const { return requirements_; }
	const Token& requires_token() const { return requires_token_; }

private:
	std::vector<ASTNode> requirements_;  // List of requirement expressions
	Token requires_token_;               // For error reporting
};

using ExpressionNode = std::variant<IdentifierNode, QualifiedIdentifierNode, StringLiteralNode, NumericLiteralNode, BoolLiteralNode,
	BinaryOperatorNode, UnaryOperatorNode, TernaryOperatorNode, FunctionCallNode, ConstructorCallNode, MemberAccessNode, PointerToMemberAccessNode, MemberFunctionCallNode,
	ArraySubscriptNode, SizeofExprNode, SizeofPackNode, AlignofExprNode, OffsetofExprNode, TypeTraitExprNode, NewExpressionNode, DeleteExpressionNode, StaticCastNode,
	DynamicCastNode, ConstCastNode, ReinterpretCastNode, TypeidNode, LambdaExpressionNode, TemplateParameterReferenceNode, FoldExpressionNode, PackExpansionExprNode, PseudoDestructorCallNode, NoexceptExprNode, InitializerListConstructionNode, ThrowExpressionNode>;


