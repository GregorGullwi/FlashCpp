#pragma once
#include "AstNodeTypes_Core.h"
#include <cassert>
#include <format>
#include <functional>


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
inline CVQualifier operator|(CVQualifier a, CVQualifier b) {
	return static_cast<CVQualifier>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline CVQualifier& operator|=(CVQualifier& a, CVQualifier b) {
	return a = a | b;
}
inline bool hasCVQualifier(CVQualifier cv, CVQualifier flag) {
	return (static_cast<uint8_t>(cv) & static_cast<uint8_t>(flag)) != 0;
}

// Reference qualifiers - mutually exclusive enum (not a bitmask)
enum class ReferenceQualifier : uint8_t {
	None = 0,
	LValueReference = 1 << 0,  // &
	RValueReference = 1 << 1,  // &&
};

using CVReferenceQualifier = ReferenceQualifier;

// Overloadable operator kinds for struct member operator overloads.
// Stored as an enum instead of a string for efficient comparison.
enum class OverloadableOperator : uint8_t {
	None = 0,          // Not an operator overload
	// Assignment
	Assign,            // = (generic, when copy/move not yet determined)
	CopyAssign,        // = (copy assignment: operator=(const T&))
	MoveAssign,        // = (move assignment: operator=(T&&))
	// Arithmetic
	Plus,              // +
	Minus,             // -
	Multiply,          // *
	Divide,            // /
	Modulo,            // %
	// Compound assignment
	PlusAssign,        // +=
	MinusAssign,       // -=
	MultiplyAssign,    // *=
	DivideAssign,      // /=
	ModuloAssign,      // %=
	// Bitwise
	BitwiseAnd,        // &
	BitwiseOr,         // |
	BitwiseXor,        // ^
	BitwiseNot,        // ~
	LeftShift,         // <<
	RightShift,        // >>
	// Bitwise compound assignment
	AndAssign,         // &=
	OrAssign,          // |=
	XorAssign,         // ^=
	LeftShiftAssign,   // <<=
	RightShiftAssign,  // >>=
	// Comparison
	Equal,             // ==
	NotEqual,          // !=
	Less,              // <
	Greater,           // >
	LessEqual,         // <=
	GreaterEqual,      // >=
	Spaceship,         // <=>
	// Logical
	LogicalNot,        // !
	LogicalAnd,        // &&
	LogicalOr,         // ||
	// Increment/Decrement
	Increment,         // ++
	Decrement,         // --
	// Member access
	Arrow,             // ->
	ArrowStar,         // ->*
	// Subscript and call
	Subscript,         // []
	Call,              // ()
	// Comma
	Comma,             // ,
	// Stream (same as shift but listed for clarity in overload contexts)
	// New/Delete
	New,               // new
	Delete,            // delete
	NewArray,          // new[]
	DeleteArray,       // delete[]
	// Conversion operators use a type index, not this enum
};

// Returns true for Assign, CopyAssign, or MoveAssign
inline bool isAssignOperator(OverloadableOperator op) {
	return op == OverloadableOperator::Assign
	    || op == OverloadableOperator::CopyAssign
	    || op == OverloadableOperator::MoveAssign;
}

inline bool isOverloadableBinaryOperator(OverloadableOperator op) {
	switch (op) {
	case OverloadableOperator::Assign:
	case OverloadableOperator::CopyAssign:
	case OverloadableOperator::MoveAssign:
	case OverloadableOperator::Plus:
	case OverloadableOperator::Minus:
	case OverloadableOperator::Multiply:
	case OverloadableOperator::Divide:
	case OverloadableOperator::Modulo:
	case OverloadableOperator::PlusAssign:
	case OverloadableOperator::MinusAssign:
	case OverloadableOperator::MultiplyAssign:
	case OverloadableOperator::DivideAssign:
	case OverloadableOperator::ModuloAssign:
	case OverloadableOperator::BitwiseAnd:
	case OverloadableOperator::BitwiseOr:
	case OverloadableOperator::BitwiseXor:
	case OverloadableOperator::LeftShift:
	case OverloadableOperator::RightShift:
	case OverloadableOperator::AndAssign:
	case OverloadableOperator::OrAssign:
	case OverloadableOperator::XorAssign:
	case OverloadableOperator::LeftShiftAssign:
	case OverloadableOperator::RightShiftAssign:
	case OverloadableOperator::Equal:
	case OverloadableOperator::NotEqual:
	case OverloadableOperator::Less:
	case OverloadableOperator::Greater:
	case OverloadableOperator::LessEqual:
	case OverloadableOperator::GreaterEqual:
	case OverloadableOperator::Spaceship:
	case OverloadableOperator::LogicalAnd:
	case OverloadableOperator::LogicalOr:
	case OverloadableOperator::Comma:
		return true;
	default:
		return false;
	}
}

inline OverloadableOperator stringToOverloadableOperator(std::string_view symbol) {
	if (symbol.empty()) return OverloadableOperator::None;
	// Single-character operators (most common first)
	if (symbol.size() == 1) {
		switch (symbol[0]) {
		case '=': return OverloadableOperator::Assign;
		case '+': return OverloadableOperator::Plus;
		case '-': return OverloadableOperator::Minus;
		case '*': return OverloadableOperator::Multiply;
		case '/': return OverloadableOperator::Divide;
		case '%': return OverloadableOperator::Modulo;
		case '&': return OverloadableOperator::BitwiseAnd;
		case '|': return OverloadableOperator::BitwiseOr;
		case '^': return OverloadableOperator::BitwiseXor;
		case '~': return OverloadableOperator::BitwiseNot;
		case '<': return OverloadableOperator::Less;
		case '>': return OverloadableOperator::Greater;
		case '!': return OverloadableOperator::LogicalNot;
		case ',': return OverloadableOperator::Comma;
		default: return OverloadableOperator::None;
		}
	}
	// Two-character operators — switch on first char, then check second
	if (symbol.size() == 2) {
		switch (symbol[0]) {
		case '=': return (symbol[1] == '=') ? OverloadableOperator::Equal : OverloadableOperator::None;
		case '!': return (symbol[1] == '=') ? OverloadableOperator::NotEqual : OverloadableOperator::None;
		case '<': return (symbol[1] == '=') ? OverloadableOperator::LessEqual
		              : (symbol[1] == '<') ? OverloadableOperator::LeftShift
		              : OverloadableOperator::None;
		case '>': return (symbol[1] == '=') ? OverloadableOperator::GreaterEqual
		              : (symbol[1] == '>') ? OverloadableOperator::RightShift
		              : OverloadableOperator::None;
		case '+': return (symbol[1] == '=') ? OverloadableOperator::PlusAssign
		              : (symbol[1] == '+') ? OverloadableOperator::Increment
		              : OverloadableOperator::None;
		case '-': return (symbol[1] == '=') ? OverloadableOperator::MinusAssign
		              : (symbol[1] == '-') ? OverloadableOperator::Decrement
		              : (symbol[1] == '>') ? OverloadableOperator::Arrow
		              : OverloadableOperator::None;
		case '*': return (symbol[1] == '=') ? OverloadableOperator::MultiplyAssign : OverloadableOperator::None;
		case '/': return (symbol[1] == '=') ? OverloadableOperator::DivideAssign : OverloadableOperator::None;
		case '%': return (symbol[1] == '=') ? OverloadableOperator::ModuloAssign : OverloadableOperator::None;
		case '&': return (symbol[1] == '=') ? OverloadableOperator::AndAssign
		              : (symbol[1] == '&') ? OverloadableOperator::LogicalAnd
		              : OverloadableOperator::None;
		case '|': return (symbol[1] == '=') ? OverloadableOperator::OrAssign
		              : (symbol[1] == '|') ? OverloadableOperator::LogicalOr
		              : OverloadableOperator::None;
		case '^': return (symbol[1] == '=') ? OverloadableOperator::XorAssign : OverloadableOperator::None;
		case '(': return (symbol[1] == ')') ? OverloadableOperator::Call : OverloadableOperator::None;
		case '[': return (symbol[1] == ']') ? OverloadableOperator::Subscript : OverloadableOperator::None;
		default: return OverloadableOperator::None;
		}
	}
	// Three-character operators
	if (symbol == "<=>") return OverloadableOperator::Spaceship;
	if (symbol == "<<=") return OverloadableOperator::LeftShiftAssign;
	if (symbol == ">>=") return OverloadableOperator::RightShiftAssign;
	if (symbol == "->*") return OverloadableOperator::ArrowStar;
	// Keyword operators
	if (symbol == "new") return OverloadableOperator::New;
	if (symbol == "delete") return OverloadableOperator::Delete;
	if (symbol == "new[]") return OverloadableOperator::NewArray;
	if (symbol == "delete[]") return OverloadableOperator::DeleteArray;
	return OverloadableOperator::None;
}

inline std::string_view overloadableOperatorToString(OverloadableOperator op) {
	switch (op) {
	case OverloadableOperator::None: return "";
	case OverloadableOperator::Assign: return "=";
	case OverloadableOperator::CopyAssign: return "=";
	case OverloadableOperator::MoveAssign: return "=";
	case OverloadableOperator::Plus: return "+";
	case OverloadableOperator::Minus: return "-";
	case OverloadableOperator::Multiply: return "*";
	case OverloadableOperator::Divide: return "/";
	case OverloadableOperator::Modulo: return "%";
	case OverloadableOperator::PlusAssign: return "+=";
	case OverloadableOperator::MinusAssign: return "-=";
	case OverloadableOperator::MultiplyAssign: return "*=";
	case OverloadableOperator::DivideAssign: return "/=";
	case OverloadableOperator::ModuloAssign: return "%=";
	case OverloadableOperator::BitwiseAnd: return "&";
	case OverloadableOperator::BitwiseOr: return "|";
	case OverloadableOperator::BitwiseXor: return "^";
	case OverloadableOperator::BitwiseNot: return "~";
	case OverloadableOperator::LeftShift: return "<<";
	case OverloadableOperator::RightShift: return ">>";
	case OverloadableOperator::AndAssign: return "&=";
	case OverloadableOperator::OrAssign: return "|=";
	case OverloadableOperator::XorAssign: return "^=";
	case OverloadableOperator::LeftShiftAssign: return "<<=";
	case OverloadableOperator::RightShiftAssign: return ">>=";
	case OverloadableOperator::Equal: return "==";
	case OverloadableOperator::NotEqual: return "!=";
	case OverloadableOperator::Less: return "<";
	case OverloadableOperator::Greater: return ">";
	case OverloadableOperator::LessEqual: return "<=";
	case OverloadableOperator::GreaterEqual: return ">=";
	case OverloadableOperator::Spaceship: return "<=>";
	case OverloadableOperator::LogicalNot: return "!";
	case OverloadableOperator::LogicalAnd: return "&&";
	case OverloadableOperator::LogicalOr: return "||";
	case OverloadableOperator::Increment: return "++";
	case OverloadableOperator::Decrement: return "--";
	case OverloadableOperator::Arrow: return "->";
	case OverloadableOperator::ArrowStar: return "->*";
	case OverloadableOperator::Subscript: return "[]";
	case OverloadableOperator::Call: return "()";
	case OverloadableOperator::Comma: return ",";
	case OverloadableOperator::New: return "new";
	case OverloadableOperator::Delete: return "delete";
	case OverloadableOperator::NewArray: return "new[]";
	case OverloadableOperator::DeleteArray: return "delete[]";
	default:
		assert(false && "Unhandled OverloadableOperator value");
		return "";
	}
}

// Target data model - controls the size of 'long' and 'wchar_t' types
// Windows uses LLP64: long is 32-bit, wchar_t is 16-bit unsigned
// Linux/Unix uses LP64: long is 64-bit, wchar_t is 32-bit signed
enum class TargetDataModel {
	LLP64,     // Windows x64: long = 32 bits, wchar_t = 16 bits unsigned (COFF)
	LP64       // Linux/Unix x64: long = 64 bits, wchar_t = 32 bits signed (ELF)
};

// Global data model setting - set by main.cpp based on target platform
// Default is platform-dependent
extern TargetDataModel g_target_data_model;

enum class Type : int_fast16_t {
	Invalid = 0,          // Must be 0 so zero-initialized memory is detected as uninitialized
	Void,
	Bool,
	Char,
	UnsignedChar,
	WChar,             // wchar_t - distinct built-in type (mangled as 'w')
	Char8,             // char8_t (C++20) - distinct built-in type (mangled as 'Du')
	Char16,            // char16_t (C++11) - distinct built-in type (mangled as 'Ds')
	Char32,            // char32_t (C++11) - distinct built-in type (mangled as 'Di')
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
	FunctionPointer,
	MemberFunctionPointer,
	MemberObjectPointer,  // Pointer to data member: int MyClass::*
	UserDefined,
	Auto,
	DeclTypeAuto,
	Function,
	Struct,
	Enum,
	Nullptr,              // nullptr_t type
	Template,             // Nested template param
};

inline bool isPlaceholderAutoType(Type type) {
	return type == Type::Auto || type == Type::DeclTypeAuto;
}

// Type classification model:
// - Primitive builtins are identified entirely by the Type enum and never need a
//   gTypeInfo lookup for identity.
// - Struct, Enum, and UserDefined represent semantic types whose concrete
//   identity lives in TypeIndex/gTypeInfo.
// - Type::Template placeholders may also carry a TypeIndex during substitution,
//   but callers still handle that unresolved case explicitly today instead of
//   folding it into needs_type_index().
// - is_struct_type() remains the narrower "struct/class-like object" helper and
//   intentionally excludes enums and unresolved template placeholders.
inline bool is_primitive_type(Type type) {
	switch (type) {
	case Type::Void:
	case Type::Bool:
	case Type::Char:
	case Type::UnsignedChar:
	case Type::WChar:
	case Type::Char8:
	case Type::Char16:
	case Type::Char32:
	case Type::Short:
	case Type::UnsignedShort:
	case Type::Int:
	case Type::UnsignedInt:
	case Type::Long:
	case Type::UnsignedLong:
	case Type::LongLong:
	case Type::UnsignedLongLong:
	case Type::Float:
	case Type::Double:
	case Type::LongDouble:
	case Type::Nullptr:
		return true;
	default:
		return false;
	}
}

inline bool needs_type_index(Type type) {
	return type == Type::Struct || type == Type::Enum || type == Type::UserDefined;
}

inline bool isIntegralType(Type type) {
	switch (type) {
	case Type::Bool:
	case Type::Char:
	case Type::Short:
	case Type::Int:
	case Type::Long:
	case Type::LongLong:
	case Type::UnsignedChar:
	case Type::UnsignedShort:
	case Type::UnsignedInt:
	case Type::UnsignedLong:
	case Type::UnsignedLongLong:
	case Type::WChar:      // wchar_t is integral per C++20 [basic.fundamental]
	case Type::Char8:      // char8_t is integral per C++20 [basic.fundamental]
	case Type::Char16:     // char16_t is integral per C++20 [basic.fundamental]
	case Type::Char32:     // char32_t is integral per C++20 [basic.fundamental]
		return true;
	default:
		return false;
	}
}

inline bool isFloatingPointType(Type type) {
	return type == Type::Float || type == Type::Double || type == Type::LongDouble;
}

inline bool isUnsignedIntegralType(Type type) {
	switch (type) {
	case Type::UnsignedChar:
	case Type::UnsignedShort:
	case Type::UnsignedInt:
	case Type::UnsignedLong:
	case Type::UnsignedLongLong:
	case Type::Char8:
	case Type::Char16:
	case Type::Char32:
		return true;
	// wchar_t is target-dependent: unsigned on Windows (LLP64), signed on Linux (LP64)
	case Type::WChar:
		return g_target_data_model == TargetDataModel::LLP64;
	default:
		return false;
	}
}

// Strong wrapper for type indices into gTypeInfo[].
// Explicit construction prevents accidental int/size_t → TypeIndex implicit
// conversion at write sites; read sites use .value explicitly.
struct TypeIndex {
	size_t value = 0;
	// Non-explicit default ctor: keeps TypeIndex{} and aggregate-init working.
	constexpr TypeIndex() noexcept = default;
	// Explicit single-arg ctor: prevents bare integer → TypeIndex conversion.
	constexpr explicit TypeIndex(size_t v) noexcept : value(v) {}
	// Increment operators for loop variables.
	TypeIndex& operator++() noexcept { ++value; return *this; }
	TypeIndex operator++(int) noexcept { TypeIndex tmp = *this; ++value; return tmp; }
	// Spaceship operator covers all relational and equality comparisons.
	constexpr auto operator<=>(const TypeIndex&) const noexcept = default;
	// True when the index is non-zero (i.e., refers to a real type entry).
	constexpr bool is_valid() const noexcept { return value > 0; }
};

namespace std {
template<>
struct hash<TypeIndex> {
	size_t operator()(TypeIndex idx) const noexcept { return std::hash<size_t>{}(idx.value); }
};
template<>
struct formatter<TypeIndex, char> : formatter<size_t, char> {
	auto format(const TypeIndex& idx, format_context& ctx) const {
		return formatter<size_t, char>::format(idx.value, ctx);
	}
};
}  // namespace std
inline std::ostream& operator<<(std::ostream& os, const TypeIndex& idx) {
	return os << idx.value;
}


// Identity record that travels with every deferred/lazy template member.
// Slice 1 populates the source-side fields; Slice 2 fills instantiated_lookup_name.
struct DeferredMemberIdentity {
	enum class Kind : uint8_t {
		Function,
		Constructor,
		Destructor,
	};

	Kind kind = Kind::Function;

	ASTNode original_member_node;              // authoritative source declaration
	StringHandle template_owner_name;          // e.g. integral_constant
	StringHandle instantiated_owner_name;      // e.g. integral_constant$hash

	StringHandle original_lookup_name;         // parsed spelling: operator value_type / operator T
	StringHandle instantiated_lookup_name;     // canonical spelling: operator int / operator float (0 = same as original)

	OverloadableOperator operator_kind = OverloadableOperator::None;  // valid when operator overload
	bool is_operator = false;
	bool is_const_method = false;
	CVQualifier cv_qualifier = CVQualifier::None;
	ReferenceQualifier ref_qualifier = ReferenceQualifier::None;
	uint16_t parameter_count = 0;
};

// Return the effective lookup name from an identity:
// uses instantiated_lookup_name when it is set, otherwise original_lookup_name.
inline StringHandle effectiveLookupName(const DeferredMemberIdentity& id) {
	return (id.instantiated_lookup_name.handle != 0)
		? id.instantiated_lookup_name
		: id.original_lookup_name;
}

// Deferred template member function body information.
// Stores a deferred member body for parsing during class-template instantiation.
struct DeferredTemplateMemberBody {
	DeferredMemberIdentity identity;
	SaveHandle body_start;                    // Handle to saved position at '{'
	SaveHandle initializer_list_start;        // Handle to saved position at ':' for ctor initializer list
	size_t struct_type_index;                 // Type index (0 during template definition)
	bool has_initializer_list;                // True if constructor has an initializer list
	InlineVector<StringHandle, 4> template_param_names; // Template parameter names
};

/// Helper function to get the C++ name string for a Type
/// Returns the string used in C++ source code (e.g., "int", "unsigned long")
/// Returns empty string_view for non-primitive types
inline std::string_view getTypeName(Type t) {
	switch (t) {
		case Type::Int: return "int";
		case Type::UnsignedInt: return "unsigned int";
		case Type::Long: return "long";
		case Type::UnsignedLong: return "unsigned long";
		case Type::LongLong: return "long long";
		case Type::UnsignedLongLong: return "unsigned long long";
		case Type::Short: return "short";
		case Type::UnsignedShort: return "unsigned short";
		case Type::Char: return "char";
		case Type::UnsignedChar: return "unsigned char";
		case Type::WChar: return "wchar_t";
		case Type::Char8: return "char8_t";
		case Type::Char16: return "char16_t";
		case Type::Char32: return "char32_t";
		case Type::Bool: return "bool";
		case Type::Float: return "float";
		case Type::Double: return "double";
		case Type::LongDouble: return "long double";
		case Type::Void: return "void";
		default: return "";
	}
}

/// Helper function to determine if a Type is signed (for MOVSX vs MOVZX)
/// MSVC treats char as signed by default.
inline bool isSignedType(Type t) {
	switch (t) {
		case Type::Char:      // char is signed by default in MSVC
		case Type::Short:
		case Type::Int:
		case Type::Long:
		case Type::LongLong:
			return true;
		// wchar_t is target-dependent: signed on Linux (LP64), unsigned on Windows (LLP64)
		case Type::WChar:
			return g_target_data_model != TargetDataModel::LLP64;  // signed on LP64, unsigned on LLP64
		// Explicitly unsigned types
		case Type::Bool:
		case Type::UnsignedChar:
		case Type::Char8:     // char8_t is always unsigned (C++20)
		case Type::Char16:    // char16_t is always unsigned
		case Type::Char32:    // char32_t is always unsigned
		case Type::UnsignedShort:
		case Type::UnsignedInt:
		case Type::UnsignedLong:
		case Type::UnsignedLongLong:
		// Non-integer types
		case Type::Float:
		case Type::Double:
		case Type::LongDouble:
		case Type::Void:
		case Type::UserDefined:
		case Type::Auto:
		case Type::DeclTypeAuto:
		case Type::Function:
		case Type::Struct:
		case Type::Enum:
		case Type::FunctionPointer:
		case Type::MemberFunctionPointer:
		case Type::MemberObjectPointer:
		case Type::Nullptr:
		case Type::Invalid:
		default:
			return false;
	}
}

// Linkage specification for functions (C vs C++)
enum class Linkage : uint8_t {
	None,           // Default C++ linkage (with name mangling)
	C,              // C linkage (no name mangling)
	CPlusPlus,      // Explicit C++ linkage
	DllImport,      // __declspec(dllimport) - symbol imported from DLL
	DllExport,      // __declspec(dllexport) - symbol exported from DLL
};

// Calling conventions (primarily for x86, but tracked for compatibility and diagnostics)
enum class CallingConvention : uint8_t {
	Default,        // Platform default (x64: Microsoft x64, x86: __cdecl)
	Cdecl,          // __cdecl - caller cleans stack, supports variadic
	Stdcall,        // __stdcall - callee cleans stack, no variadic
	Fastcall,       // __fastcall - first args in registers
	Vectorcall,     // __vectorcall - optimized for SIMD
	Thiscall,       // __thiscall - C++ member functions (this in register)
	Clrcall,        // __clrcall - .NET/CLI interop
};

// Access specifier for struct/class members
enum class AccessSpecifier {
	Public,
	Protected,
	Private
};

// Friend declaration types
enum class FriendKind {
	Function,      // friend void func();
	Class,         // friend class ClassName;
	MemberFunction, // friend void Class::func();
	TemplateClass  // template<typename T1, typename T2> friend struct pair;
};

// Base class specifier for inheritance
struct BaseClassSpecifier {
	std::string_view name;           // Base class name
	TypeIndex type_index;       // Index into gTypeInfo for base class type
	AccessSpecifier access;     // Inheritance access (public/protected/private)
	bool is_virtual;            // True for virtual inheritance (Phase 3)
	size_t offset;              // Offset of base subobject in derived class
	bool is_deferred;           // True for template parameters (resolved at instantiation)

	BaseClassSpecifier(std::string_view n, TypeIndex tidx, AccessSpecifier acc, bool virt = false, size_t off = 0, bool deferred = false)
		: name(n), type_index(tidx), access(acc), is_virtual(virt), offset(off), is_deferred(deferred) {}
};

// Deferred base class specifier for decltype bases in templates
// These are resolved during template instantiation
struct DeferredBaseClassSpecifier {
	ASTNode decltype_expression;  // The parsed decltype expression
	AccessSpecifier access;        // Inheritance access (public/protected/private)
	bool is_virtual;               // True for virtual inheritance

	DeferredBaseClassSpecifier(ASTNode expr, AccessSpecifier acc, bool virt = false)
		: decltype_expression(expr), access(acc), is_virtual(virt) {}
};

struct TemplateArgumentNodeInfo {
	ASTNode node;
	bool is_pack = false;
	bool is_dependent = false;
};

struct DeferredTemplateBaseClassSpecifier {
	StringHandle base_template_name;
	std::vector<TemplateArgumentNodeInfo> template_arguments;
	std::optional<StringHandle> member_type; // e.g., ::type
	AccessSpecifier access;
	bool is_virtual;
	bool is_pack_expansion = false; // e.g., Base<Args>...

	DeferredTemplateBaseClassSpecifier(StringHandle name,
	                                   std::vector<TemplateArgumentNodeInfo> args,
	                                   std::optional<StringHandle> member,
	                                   AccessSpecifier acc,
	                                   bool virt,
	                                   bool pack_expansion = false)
		: base_template_name(name),
		  template_arguments(std::move(args)),
		  member_type(member),
		  access(acc),
		  is_virtual(virt),
		  is_pack_expansion(pack_expansion) {}
};

// Function signature for function pointers
struct FunctionSignature {
	Type return_type;
	std::vector<Type> parameter_types;
	Linkage linkage = Linkage::None;           // C vs C++ linkage
	std::optional<std::string> class_name;     // For member function pointers
	bool is_const = false;                     // For const member functions
	bool is_volatile = false;                  // For volatile member functions
};

// Deferred static_assert information - stored during template definition, evaluated during instantiation
struct DeferredStaticAssert {
	ASTNode condition_expr;  // The condition expression to evaluate
	StringHandle message;    // The assertion message (interned in StringTable for concatenated literals)
	
	DeferredStaticAssert(ASTNode expr, StringHandle msg)
		: condition_expr(expr), message(msg) {}
};

// Struct member information
struct StructMember {
	StringHandle name;
	Type type;
	TypeIndex type_index;   // Index into gTypeInfo for complex types (structs, etc.)
	size_t offset;          // Offset in bytes from start of struct
	size_t size;            // Size in bytes
	std::optional<size_t> bitfield_width; // Width in bits for bitfield members
	size_t bitfield_bit_offset = 0; // Bit offset within the storage unit for bitfield members
	size_t referenced_size_bits; // Size of the referenced value in bits (for references)
	size_t alignment;       // Alignment requirement
	AccessSpecifier access; // Access level (public/protected/private)
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None;  // None, LValueReference (&), or RValueReference (&&)
	std::optional<ASTNode> default_initializer;  // C++11 default member initializer
	bool is_array;          // True if member is an array
	std::vector<size_t> array_dimensions;  // Dimensions for multidimensional arrays
	int pointer_depth;      // Pointer indirection level (e.g., int* = 1, int** = 2)
	std::optional<FunctionSignature> function_signature;  // For FunctionPointer members: stores return type and parameter types

	// Convenience helpers for common checks
	bool is_reference() const { return reference_qualifier != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return reference_qualifier == ReferenceQualifier::RValueReference; }

	StructMember(StringHandle n, Type t, TypeIndex tidx, size_t off, size_t sz, size_t align,
	            AccessSpecifier acc,
	            std::optional<ASTNode> init,
	            ReferenceQualifier ref_qual,
	            size_t ref_size_bits,
	            bool is_arr,
	            std::vector<size_t> arr_dims,
	            int ptr_depth,
	            std::optional<size_t> bf_width)
		: name(n), type(t), type_index(tidx), offset(off), size(sz),
		  bitfield_width(bf_width), referenced_size_bits(ref_size_bits ? ref_size_bits : sz * 8), alignment(align),
		  access(acc), reference_qualifier(ref_qual),
		  default_initializer(std::move(init)), is_array(is_arr), array_dimensions(std::move(arr_dims)),
		  pointer_depth(ptr_depth) {}
	
	StringHandle getName() const {
		return name;
	}
};

// Forward declaration for member function support
class FunctionDeclarationNode;

// Struct member function information
struct StructMemberFunction {
	StringHandle name;
	ASTNode function_decl;  // FunctionDeclarationNode, ConstructorDeclarationNode, or DestructorDeclarationNode
	AccessSpecifier access; // Access level (public/protected/private)
	bool is_constructor;    // True if this is a constructor
	bool is_destructor;     // True if this is a destructor
	OverloadableOperator operator_kind; // None for non-operators; non-None implies operator overload

	// Virtual function support (Phase 2)
	bool is_virtual = false;        // True if declared with 'virtual' keyword
	bool is_pure_virtual = false;   // True if pure virtual (= 0)
	bool is_override = false;       // True if declared with 'override' keyword
	bool is_final = false;          // True if declared with 'final' keyword
	int vtable_index = -1;          // Index in vtable, -1 if not virtual

	// CV qualifiers for member functions (Phase 4)
	CVQualifier cv_qualifier = CVQualifier::None;

	// noexcept tracking for type traits
	bool is_noexcept = false;       // True if declared noexcept (e.g., void foo() noexcept)

	// Convenience accessors
	bool is_operator_overload() const { return operator_kind != OverloadableOperator::None; }
	bool is_const() const { return (static_cast<uint8_t>(cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0; }
	bool is_volatile() const { return (static_cast<uint8_t>(cv_qualifier) & static_cast<uint8_t>(CVQualifier::Volatile)) != 0; }

	// Convenience accessor for operator symbol string (for logging/mangling)
	std::string_view operator_symbol() const { return overloadableOperatorToString(operator_kind); }

	StructMemberFunction(StringHandle n, ASTNode func_decl, AccessSpecifier acc = AccessSpecifier::Public,
	                     bool is_ctor = false, bool is_dtor = false,
	                     OverloadableOperator op_kind = OverloadableOperator::None)
		: name(n), function_decl(func_decl), access(acc), is_constructor(is_ctor), is_destructor(is_dtor),
		  operator_kind(op_kind) {}
	
	StringHandle getName() const {
		return name;
	}
};

// MSVC RTTI structures - multi-component format for runtime compatibility
// These structures match the MSVC ABI for RTTI to work with __dynamic_cast

// ??_R0 - Type Descriptor (simplified type_info equivalent)
struct MSVCTypeDescriptor {
	const void* vtable;              // Pointer to type_info vtable (usually null in our case)
	const void* spare;               // Reserved/spare pointer (unused)
	char name[1];                    // Variable-length mangled name (null-terminated)
};

// ??_R1 - Base Class Descriptor
struct MSVCBaseClassDescriptor {
	const MSVCTypeDescriptor* type_descriptor;  // Pointer to base class type descriptor (??_R0)
	uint32_t num_contained_bases;    // Number of nested base classes
	int32_t mdisp;                   // Member displacement (offset in class)
	int32_t pdisp;                   // Vbtable displacement (-1 if not virtual base)
	int32_t vdisp;                   // Displacement inside vbtable (0 if not virtual base)
	uint32_t attributes;             // Flags (virtual, ambiguous, etc.)
};

// ??_R2 - Base Class Array (array of pointers to ??_R1)
struct MSVCBaseClassArray {
	const MSVCBaseClassDescriptor* base_class_descriptors[1]; // Variable-length array
};

// ??_R3 - Class Hierarchy Descriptor
struct MSVCClassHierarchyDescriptor {
	uint32_t signature;              // Always 0
	uint32_t attributes;             // Bit flags (multiple inheritance, virtual inheritance, etc.)
	uint32_t num_base_classes;       // Number of base classes (including self)
	const MSVCBaseClassArray* base_class_array;  // Pointer to base class array (??_R2)
};

// ??_R4 - Complete Object Locator (referenced by vtable)
struct MSVCCompleteObjectLocator {
	uint32_t signature;              // 0 for 32-bit, 1 for 64-bit
	uint32_t offset;                 // Offset of this vtable in the complete class
	uint32_t cd_offset;              // Constructor displacement offset
	const MSVCTypeDescriptor* type_descriptor;        // Pointer to type descriptor (??_R0)
	const MSVCClassHierarchyDescriptor* hierarchy;    // Pointer to class hierarchy (??_R3)
};

// Itanium C++ ABI RTTI structures - standard format for Linux/Unix systems
// These structures match the Itanium C++ ABI specification for RTTI

// Base class info structure for __vmi_class_type_info
struct ItaniumBaseClassTypeInfo {
	const void* base_type;      // Pointer to base class type_info (__class_type_info*)
	int64_t offset_flags;       // Combined offset and flags
	
	// Flags in offset_flags:
	// bit 0: __virtual_mask (0x1) - base class is virtual
	// bit 1: __public_mask (0x2) - base class is public
	// bits 8+: offset of base class in derived class (signed)
};

// __class_type_info - Type info for classes without base classes
struct ItaniumClassTypeInfo {
	const void* vtable;         // Pointer to vtable for __class_type_info
	const char* name;           // Mangled type name (e.g., "3Foo" for class Foo)
};

// __si_class_type_info - Type info for classes with single, public, non-virtual base
struct ItaniumSIClassTypeInfo {
	const void* vtable;         // Pointer to vtable for __si_class_type_info
	const char* name;           // Mangled type name
	const void* base_type;      // Pointer to base class type_info (__class_type_info*)
};

// __vmi_class_type_info - Type info for classes with multiple or virtual bases
struct ItaniumVMIClassTypeInfo {
	const void* vtable;         // Pointer to vtable for __vmi_class_type_info
	const char* name;           // Mangled type name
	uint32_t flags;             // Inheritance flags
	uint32_t base_count;        // Number of direct base classes
	ItaniumBaseClassTypeInfo base_info[1];  // Variable-length array of base class info
	
	// Flags:
	// __non_diamond_repeat_mask = 0x1 - has repeated bases (but not diamond)
	// __diamond_shaped_mask = 0x2     - has diamond-shaped inheritance
};

// Legacy RTTITypeInfo for compatibility with existing code
// This will hold references to both MSVC and Itanium structures
struct RTTITypeInfo {
	const char* type_name;           // Mangled type name
	const char* demangled_name;      // Human-readable type name
	size_t num_bases;                // Number of base classes
	const RTTITypeInfo** base_types; // Array of pointers to base class type_info

	// MSVC RTTI structures
	MSVCCompleteObjectLocator* col;         // ??_R4 - Complete Object Locator
	MSVCClassHierarchyDescriptor* chd;      // ??_R3 - Class Hierarchy Descriptor
	MSVCBaseClassArray* bca;                // ??_R2 - Base Class Array
	std::vector<MSVCBaseClassDescriptor*> base_descriptors;  // ??_R1 - Base Class Descriptors
	MSVCTypeDescriptor* type_descriptor;    // ??_R0 - Type Descriptor

	// Itanium C++ ABI RTTI structures
	void* itanium_type_info;        // Pointer to __class_type_info, __si_class_type_info, or __vmi_class_type_info
	enum class ItaniumTypeInfoKind {
		None,
		ClassTypeInfo,      // __class_type_info (no bases)
		SIClassTypeInfo,    // __si_class_type_info (single inheritance)
		VMIClassTypeInfo    // __vmi_class_type_info (multiple/virtual inheritance)
	} itanium_kind;

	RTTITypeInfo(const char* mangled, const char* demangled, size_t num_base = 0)
		: type_name(mangled), demangled_name(demangled), num_bases(num_base), base_types(nullptr),
		  col(nullptr), chd(nullptr), bca(nullptr), type_descriptor(nullptr),
		  itanium_type_info(nullptr), itanium_kind(ItaniumTypeInfoKind::None) {}

	// Check if this type is derived from another type (for dynamic_cast)
	bool isDerivedFrom(const RTTITypeInfo* base) const {
		if (this == base) return true;

		for (size_t i = 0; i < num_bases; ++i) {
			if (base_types[i] && base_types[i]->isDerivedFrom(base)) {
				return true;
			}
		}
		return false;
	}
};

// Static member information
struct StructStaticMember {
	StringHandle name;
	Type type;
	TypeIndex type_index;   // Index into gTypeInfo for complex types
	size_t size;            // Size in bytes
	size_t alignment;       // Alignment requirement
	AccessSpecifier access; // Access level (public/protected/private)
	std::optional<ASTNode> initializer;  // Optional initializer expression
	CVQualifier cv_qualifier = CVQualifier::None;  // CV qualifiers (const, volatile)
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None;  // None, LValueReference (&), or RValueReference (&&)
	int pointer_depth = 0;  // Pointer indirection level (e.g., int* = 1, int** = 2)

	// Convenience helpers for common checks
	bool is_const() const { return hasCVQualifier(cv_qualifier, CVQualifier::Const); }
	bool is_reference() const { return reference_qualifier != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return reference_qualifier == ReferenceQualifier::RValueReference; }

	StructStaticMember(StringHandle n, Type t, TypeIndex tidx, size_t sz, size_t align, AccessSpecifier acc = AccessSpecifier::Public,
	                   std::optional<ASTNode> init = std::nullopt, CVQualifier cv_qual = CVQualifier::None,
	                   ReferenceQualifier ref_qual = ReferenceQualifier::None, int ptr_depth = 0)
		: name(n), type(t), type_index(tidx), size(sz), alignment(align), access(acc),
		  initializer(init), cv_qualifier(cv_qual), reference_qualifier(ref_qual), pointer_depth(ptr_depth) {}
	
	StringHandle getName() const {
		return name;
	}
};
