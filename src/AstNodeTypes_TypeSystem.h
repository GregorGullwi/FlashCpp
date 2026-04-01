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
	None = 0,		  // Not an operator overload
	// Assignment
	Assign,			// = (generic, when copy/move not yet determined)
	CopyAssign,		// = (copy assignment: operator=(const T&))
	MoveAssign,		// = (move assignment: operator=(T&&))
	// Arithmetic
	Plus,			  // +
	Minus,			 // -
	Multiply,		  // *
	Divide,			// /
	Modulo,			// %
	// Compound assignment
	PlusAssign,		// +=
	MinusAssign,		 // -=
	MultiplyAssign,	// *=
	DivideAssign,	  // /=
	ModuloAssign,	  // %=
	// Bitwise
	BitwiseAnd,		// &
	BitwiseOr,		   // |
	BitwiseXor,		// ^
	BitwiseNot,		// ~
	LeftShift,		   // <<
	RightShift,		// >>
	// Bitwise compound assignment
	AndAssign,		   // &=
	OrAssign,		  // |=
	XorAssign,		   // ^=
	LeftShiftAssign,	 // <<=
	RightShiftAssign,  // >>=
	// Comparison
	Equal,			 // ==
	NotEqual,		  // !=
	Less,			  // <
	Greater,			 // >
	LessEqual,		   // <=
	GreaterEqual,	  // >=
	Spaceship,		   // <=>
	// Logical
	LogicalNot,		// !
	LogicalAnd,		// &&
	LogicalOr,		   // ||
	// Increment/Decrement
	Increment,		   // ++
	Decrement,		   // --
	// Member access
	Arrow,			 // ->
	ArrowStar,		   // ->*
	// Subscript and call
	Subscript,		   // []
	Call,			  // ()
	// Comma
	Comma,			 // ,
	// Stream (same as shift but listed for clarity in overload contexts)
	// New/Delete
	New,			   // new
	Delete,			// delete
	NewArray,		  // new[]
	DeleteArray,		 // delete[]
	// Conversion operators use a type index, not this enum
};

// Returns true for Assign, CopyAssign, or MoveAssign
inline bool isAssignOperator(OverloadableOperator op) {
	return op == OverloadableOperator::Assign || op == OverloadableOperator::CopyAssign || op == OverloadableOperator::MoveAssign;
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
	if (symbol.empty())
		return OverloadableOperator::None;
	// Single-character operators (most common first)
	if (symbol.size() == 1) {
		switch (symbol[0]) {
		case '=':
			return OverloadableOperator::Assign;
		case '+':
			return OverloadableOperator::Plus;
		case '-':
			return OverloadableOperator::Minus;
		case '*':
			return OverloadableOperator::Multiply;
		case '/':
			return OverloadableOperator::Divide;
		case '%':
			return OverloadableOperator::Modulo;
		case '&':
			return OverloadableOperator::BitwiseAnd;
		case '|':
			return OverloadableOperator::BitwiseOr;
		case '^':
			return OverloadableOperator::BitwiseXor;
		case '~':
			return OverloadableOperator::BitwiseNot;
		case '<':
			return OverloadableOperator::Less;
		case '>':
			return OverloadableOperator::Greater;
		case '!':
			return OverloadableOperator::LogicalNot;
		case ',':
			return OverloadableOperator::Comma;
		default:
			return OverloadableOperator::None;
		}
	}
	// Two-character operators — switch on first char, then check second
	if (symbol.size() == 2) {
		switch (symbol[0]) {
		case '=':
			return (symbol[1] == '=') ? OverloadableOperator::Equal : OverloadableOperator::None;
		case '!':
			return (symbol[1] == '=') ? OverloadableOperator::NotEqual : OverloadableOperator::None;
		case '<':
			return (symbol[1] == '=')	? OverloadableOperator::LessEqual
				   : (symbol[1] == '<') ? OverloadableOperator::LeftShift
										: OverloadableOperator::None;
		case '>':
			return (symbol[1] == '=')	? OverloadableOperator::GreaterEqual
				   : (symbol[1] == '>') ? OverloadableOperator::RightShift
										: OverloadableOperator::None;
		case '+':
			return (symbol[1] == '=')	? OverloadableOperator::PlusAssign
				   : (symbol[1] == '+') ? OverloadableOperator::Increment
										: OverloadableOperator::None;
		case '-':
			return (symbol[1] == '=')	? OverloadableOperator::MinusAssign
				   : (symbol[1] == '-') ? OverloadableOperator::Decrement
				   : (symbol[1] == '>') ? OverloadableOperator::Arrow
										: OverloadableOperator::None;
		case '*':
			return (symbol[1] == '=') ? OverloadableOperator::MultiplyAssign : OverloadableOperator::None;
		case '/':
			return (symbol[1] == '=') ? OverloadableOperator::DivideAssign : OverloadableOperator::None;
		case '%':
			return (symbol[1] == '=') ? OverloadableOperator::ModuloAssign : OverloadableOperator::None;
		case '&':
			return (symbol[1] == '=')	? OverloadableOperator::AndAssign
				   : (symbol[1] == '&') ? OverloadableOperator::LogicalAnd
										: OverloadableOperator::None;
		case '|':
			return (symbol[1] == '=')	? OverloadableOperator::OrAssign
				   : (symbol[1] == '|') ? OverloadableOperator::LogicalOr
										: OverloadableOperator::None;
		case '^':
			return (symbol[1] == '=') ? OverloadableOperator::XorAssign : OverloadableOperator::None;
		case '(':
			return (symbol[1] == ')') ? OverloadableOperator::Call : OverloadableOperator::None;
		case '[':
			return (symbol[1] == ']') ? OverloadableOperator::Subscript : OverloadableOperator::None;
		default:
			return OverloadableOperator::None;
		}
	}
	// Three-character operators
	if (symbol == "<=>")
		return OverloadableOperator::Spaceship;
	if (symbol == "<<=")
		return OverloadableOperator::LeftShiftAssign;
	if (symbol == ">>=")
		return OverloadableOperator::RightShiftAssign;
	if (symbol == "->*")
		return OverloadableOperator::ArrowStar;
	// Keyword operators
	if (symbol == "new")
		return OverloadableOperator::New;
	if (symbol == "delete")
		return OverloadableOperator::Delete;
	if (symbol == "new[]")
		return OverloadableOperator::NewArray;
	if (symbol == "delete[]")
		return OverloadableOperator::DeleteArray;
	return OverloadableOperator::None;
}

inline OverloadableOperator overloadableOperatorFromFunctionName(std::string_view function_name) {
	static constexpr std::string_view kOperatorPrefix = "operator";
	if (!function_name.starts_with(kOperatorPrefix)) {
		return OverloadableOperator::None;
	}
	std::string_view suffix = function_name.substr(kOperatorPrefix.size());
	if (!suffix.empty() && suffix[0] == ' ') {
		suffix.remove_prefix(1);
	}
	return stringToOverloadableOperator(suffix);
}

inline std::string_view overloadableOperatorToString(OverloadableOperator op) {
	switch (op) {
	case OverloadableOperator::None:
		return "";
	case OverloadableOperator::Assign:
		return "=";
	case OverloadableOperator::CopyAssign:
		return "=";
	case OverloadableOperator::MoveAssign:
		return "=";
	case OverloadableOperator::Plus:
		return "+";
	case OverloadableOperator::Minus:
		return "-";
	case OverloadableOperator::Multiply:
		return "*";
	case OverloadableOperator::Divide:
		return "/";
	case OverloadableOperator::Modulo:
		return "%";
	case OverloadableOperator::PlusAssign:
		return "+=";
	case OverloadableOperator::MinusAssign:
		return "-=";
	case OverloadableOperator::MultiplyAssign:
		return "*=";
	case OverloadableOperator::DivideAssign:
		return "/=";
	case OverloadableOperator::ModuloAssign:
		return "%=";
	case OverloadableOperator::BitwiseAnd:
		return "&";
	case OverloadableOperator::BitwiseOr:
		return "|";
	case OverloadableOperator::BitwiseXor:
		return "^";
	case OverloadableOperator::BitwiseNot:
		return "~";
	case OverloadableOperator::LeftShift:
		return "<<";
	case OverloadableOperator::RightShift:
		return ">>";
	case OverloadableOperator::AndAssign:
		return "&=";
	case OverloadableOperator::OrAssign:
		return "|=";
	case OverloadableOperator::XorAssign:
		return "^=";
	case OverloadableOperator::LeftShiftAssign:
		return "<<=";
	case OverloadableOperator::RightShiftAssign:
		return ">>=";
	case OverloadableOperator::Equal:
		return "==";
	case OverloadableOperator::NotEqual:
		return "!=";
	case OverloadableOperator::Less:
		return "<";
	case OverloadableOperator::Greater:
		return ">";
	case OverloadableOperator::LessEqual:
		return "<=";
	case OverloadableOperator::GreaterEqual:
		return ">=";
	case OverloadableOperator::Spaceship:
		return "<=>";
	case OverloadableOperator::LogicalNot:
		return "!";
	case OverloadableOperator::LogicalAnd:
		return "&&";
	case OverloadableOperator::LogicalOr:
		return "||";
	case OverloadableOperator::Increment:
		return "++";
	case OverloadableOperator::Decrement:
		return "--";
	case OverloadableOperator::Arrow:
		return "->";
	case OverloadableOperator::ArrowStar:
		return "->*";
	case OverloadableOperator::Subscript:
		return "[]";
	case OverloadableOperator::Call:
		return "()";
	case OverloadableOperator::Comma:
		return ",";
	case OverloadableOperator::New:
		return "new";
	case OverloadableOperator::Delete:
		return "delete";
	case OverloadableOperator::NewArray:
		return "new[]";
	case OverloadableOperator::DeleteArray:
		return "delete[]";
	default:
		assert(false && "Unhandled OverloadableOperator value");
		return "";
	}
}

// Target data model - controls the size of 'long' and 'wchar_t' types
// Windows uses LLP64: long is 32-bit, wchar_t is 16-bit unsigned
// Linux/Unix uses LP64: long is 64-bit, wchar_t is 32-bit signed
enum class TargetDataModel {
	LLP64,	   // Windows x64: long = 32 bits, wchar_t = 16 bits unsigned (COFF)
	LP64		 // Linux/Unix x64: long = 64 bits, wchar_t = 32 bits signed (ELF)
};

// Global data model setting - set by main.cpp based on target platform
// Default is platform-dependent
extern TargetDataModel g_target_data_model;

// TypeCategory — the canonical type classification enum for FlashCpp.
// Explicit values 0-23 match historical numbering so that numeric values
// printed in debug logs remain consistent.  TypeAlias (24) represents
// resolved type aliases, distinct from UserDefined (23).
enum class TypeCategory : uint8_t {
	Invalid = 0,
	Void = 1,
	Bool = 2,
	Char = 3,
	UnsignedChar = 4,
	WChar = 5,
	Char8 = 6,
	Char16 = 7,
	Char32 = 8,
	Short = 9,
	UnsignedShort = 10,
	Int = 11,
	UnsignedInt = 12,
	Long = 13,
	UnsignedLong = 14,
	LongLong = 15,
	UnsignedLongLong = 16,
	Float = 17,
	Double = 18,
	LongDouble = 19,
	FunctionPointer = 20,
	MemberFunctionPointer = 21,
	MemberObjectPointer = 22,
	UserDefined = 23,  // opaque/unresolved user type or builtin alias (e.g. __builtin_va_list)
	TypeAlias = 24,	// explicit typedef/using alias
	Auto = 25,
	DeclTypeAuto = 26,
	Function = 27,
	Struct = 28,
	Enum = 29,
	Nullptr = 30,
	Template = 31,
};

// --- TypeCategory classification helpers ---
// Helpers needed by TypeIndex itself are declared first so that TypeIndex
// methods can delegate to them instead of duplicating the switch logic.

// TypeCategory overload for isPlaceholderAutoType.
inline bool isPlaceholderAutoType(TypeCategory cat) {
	return cat == TypeCategory::Auto || cat == TypeCategory::DeclTypeAuto;
}

// True for primitive scalar types (no gTypeInfo lookup needed for identity).
constexpr bool is_primitive_type(TypeCategory cat) {
	switch (cat) {
	case TypeCategory::Void:
	case TypeCategory::Bool:
	case TypeCategory::Char:
	case TypeCategory::UnsignedChar:
	case TypeCategory::WChar:
	case TypeCategory::Char8:
	case TypeCategory::Char16:
	case TypeCategory::Char32:
	case TypeCategory::Short:
	case TypeCategory::UnsignedShort:
	case TypeCategory::Int:
	case TypeCategory::UnsignedInt:
	case TypeCategory::Long:
	case TypeCategory::UnsignedLong:
	case TypeCategory::LongLong:
	case TypeCategory::UnsignedLongLong:
	case TypeCategory::Float:
	case TypeCategory::Double:
	case TypeCategory::LongDouble:
	case TypeCategory::Nullptr:
		return true;
	default:
		return false;
	}
}

// True for Struct or UserDefined (opaque) — types that may carry struct-like
// metadata (StructTypeInfo).  TypeAlias is intentionally excluded: an alias
// to a primitive (e.g., `using MyInt = int`) is NOT struct-like.  Callers
// that need to handle aliases should resolve through getTypeInfo() first.
// Intentionally excludes Enum and Template placeholders.
constexpr bool is_struct_type(TypeCategory cat) {
	return cat == TypeCategory::Struct || cat == TypeCategory::UserDefined;
}

// True for any type that requires a gTypeInfo entry for identity
// (Struct, Enum, UserDefined, TypeAlias).  Mirrors needs_type_index(Type).
constexpr bool needs_type_index(TypeCategory cat) {
	return cat == TypeCategory::Struct || cat == TypeCategory::Enum || cat == TypeCategory::UserDefined || cat == TypeCategory::TypeAlias;
}

// Strong wrapper for type indices into getTypeInfo(TypeIndex{}).
// Explicit construction prevents accidental int/size_t → TypeIndex implicit
// conversion at write sites; read sites use .value explicitly.
//
// Milestone 7 (Option D Step 1): `category_` now caches the TypeCategory of the
// referenced type so that classification queries (isStruct, isEnum, …) do not
// require a gTypeInfo lookup.  During the migration period, TypeIndex values
// constructed via the legacy 1-arg ctor leave category_ = TypeCategory::Invalid;
// only values returned by add* functions and initialize_native_types carry a
// correct category.  Comparison operators use only `.index_` for backward
// compatibility (category is a cached hint, not an identity field).
struct TypeIndex {
	uint32_t index_ = 0;
	TypeCategory category_ = TypeCategory::Invalid;

	// Non-explicit default ctor: fully-null TypeIndex.
	constexpr TypeIndex() noexcept = default;
	// Explicit single-arg ctor (legacy): category stays Invalid.
	constexpr explicit TypeIndex(size_t v) noexcept
		: index_(static_cast<uint32_t>(v)), category_(TypeCategory::Invalid) {}
	// Two-arg ctor (preferred going forward): sets both index and category.
	constexpr TypeIndex(size_t v, TypeCategory cat) noexcept
		: index_(static_cast<uint32_t>(v)), category_(cat) {}

	// Public accessor (read-only raw index).
	constexpr uint32_t index() const noexcept { return index_; }

	// Factory: build a TypeIndex that carries both the gTypeInfo slot from `idx` and
	// a resolved TypeCategory derived from `t` (or the existing category in `idx` if it
	// is already valid).  Preferred over two-step TypeIndex{n} + setCategory() patterns.

	// Increment operators for loop variables (index only).
	TypeIndex& operator++() noexcept {
		++index_;
		return *this;
	}
	TypeIndex operator++(int) noexcept {
		TypeIndex tmp = *this;
		++index_;
		return tmp;
	}

	// Comparison operators use only `.index_` (the category is a cache, not an
	// identity field) so that legacy TypeIndex{n} still matches stored values.
	constexpr bool operator==(const TypeIndex& other) const noexcept { return index_ == other.index_; }
	constexpr bool operator!=(const TypeIndex& other) const noexcept { return index_ != other.index_; }
	constexpr auto operator<=>(const TypeIndex& other) const noexcept { return index_ <=> other.index_; }

	// True when the index is non-zero (i.e., refers to a real type entry).
	// Semantics unchanged from before Milestone 7.
	constexpr bool is_valid() const noexcept { return index_ > 0; }

	// True when both index and category are their zero/Invalid defaults.
	constexpr bool isNull() const noexcept {
		return index_ == 0 && category_ == TypeCategory::Invalid;
	}

	// --- Category accessors (no gTypeInfo lookup required) ---
	// These return meaningful results only when the TypeIndex was created via an
	// add*() function or initialize_native_types().  Legacy TypeIndex{n} values
	// always return TypeCategory::Invalid / false from these helpers.
	constexpr TypeCategory category() const noexcept { return category_; }
	// Mutates only the category cache (not the index).  Use to stamp the correct
	// TypeCategory onto a TypeIndex that was built with the legacy 1-arg ctor.
	constexpr void setCategory(TypeCategory cat) noexcept { category_ = cat; }
	// Returns a new TypeIndex with the same index but a different category.
	constexpr TypeIndex withCategory(TypeCategory cat) const noexcept { return TypeIndex{index_, cat}; }

	constexpr bool isStruct() const noexcept { return category_ == TypeCategory::Struct; }
	constexpr bool isEnum() const noexcept { return category_ == TypeCategory::Enum; }
	constexpr bool isTypeAlias() const noexcept { return category_ == TypeCategory::TypeAlias; }
	constexpr bool isFunction() const noexcept { return category_ == TypeCategory::Function; }
	constexpr bool isTemplatePlaceholder() const noexcept { return category_ == TypeCategory::Template; }

	// Delegates to is_struct_type(TypeCategory) — see its comment for semantics.
	constexpr bool isStructLike() const noexcept { return is_struct_type(category_); }
	// Delegates to needs_type_index(TypeCategory) — see its comment for semantics.
	constexpr bool needsTypeIndex() const noexcept { return ::needs_type_index(category_); }
	// Delegates to is_primitive_type(TypeCategory) — see its comment for semantics.
	constexpr bool isPrimitive() const noexcept { return is_primitive_type(category_); }
};

namespace std {
template <>
struct hash<TypeIndex> {
	// Hash only `.index_` so that hash(a) == hash(b) whenever a == b
	// (operator== compares only `.index_`).
	size_t operator()(TypeIndex idx) const noexcept { return std::hash<uint32_t>{}(idx.index_); }
};
template <>
struct formatter<TypeIndex, char> : formatter<uint32_t, char> {
	auto format(const TypeIndex& idx, format_context& ctx) const {
		return formatter<uint32_t, char>::format(idx.index_, ctx);
	}
};
}  // namespace std
inline std::ostream& operator<<(std::ostream& os, const TypeIndex& idx) {
	return os << idx.index_;
}

// --- TypeCategory classification helpers continued (Milestone 7 Step 2) ---
// is_primitive_type, is_struct_type, and needs_type_index are defined before
// TypeIndex (above) so that TypeIndex methods can delegate to them.

// True for all builtin types that have a valid get_type_size_bits() answer.
// Includes Nullptr (nullptr_t is fundamental per C++20 [basic.fundamental]/13).
// Excludes Auto and DeclTypeAuto.
constexpr bool is_builtin_type(TypeCategory cat) {
	switch (cat) {
	case TypeCategory::Void:
	case TypeCategory::Bool:
	case TypeCategory::Char:
	case TypeCategory::UnsignedChar:
	case TypeCategory::WChar:
	case TypeCategory::Char8:
	case TypeCategory::Char16:
	case TypeCategory::Char32:
	case TypeCategory::Short:
	case TypeCategory::UnsignedShort:
	case TypeCategory::Int:
	case TypeCategory::UnsignedInt:
	case TypeCategory::Long:
	case TypeCategory::UnsignedLong:
	case TypeCategory::LongLong:
	case TypeCategory::UnsignedLongLong:
	case TypeCategory::Float:
	case TypeCategory::Double:
	case TypeCategory::LongDouble:
	case TypeCategory::FunctionPointer:
	case TypeCategory::MemberFunctionPointer:
	case TypeCategory::MemberObjectPointer:
	case TypeCategory::Nullptr:
		return true;
	default:
		return false;
	}
}

constexpr bool isArithmeticType(TypeCategory cat) {
	switch (cat) {
	case TypeCategory::Bool:
	case TypeCategory::Char:
	case TypeCategory::UnsignedChar:
	case TypeCategory::WChar:
	case TypeCategory::Char8:
	case TypeCategory::Char16:
	case TypeCategory::Char32:
	case TypeCategory::Short:
	case TypeCategory::UnsignedShort:
	case TypeCategory::Int:
	case TypeCategory::UnsignedInt:
	case TypeCategory::Long:
	case TypeCategory::UnsignedLong:
	case TypeCategory::LongLong:
	case TypeCategory::UnsignedLongLong:
	case TypeCategory::Float:
	case TypeCategory::Double:
	case TypeCategory::LongDouble:
		return true;
	default:
		return false;
	}
}

constexpr bool isFundamentalType(TypeCategory cat) {
	return cat == TypeCategory::Void || cat == TypeCategory::Nullptr || isArithmeticType(cat);
}

constexpr bool isIntegralType(TypeCategory cat) {
	switch (cat) {
	case TypeCategory::Bool:
	case TypeCategory::Char:
	case TypeCategory::UnsignedChar:
	case TypeCategory::WChar:
	case TypeCategory::Char8:
	case TypeCategory::Char16:
	case TypeCategory::Char32:
	case TypeCategory::Short:
	case TypeCategory::UnsignedShort:
	case TypeCategory::Int:
	case TypeCategory::UnsignedInt:
	case TypeCategory::Long:
	case TypeCategory::UnsignedLong:
	case TypeCategory::LongLong:
	case TypeCategory::UnsignedLongLong:
		return true;
	default:
		return false;
	}
}

constexpr bool isFloatingPointType(TypeCategory cat) {
	return cat == TypeCategory::Float || cat == TypeCategory::Double || cat == TypeCategory::LongDouble;
}

// TypeCategory overloads for the Type-based helpers in AstNodeTypes.cpp.
// These allow call sites to operate directly on TypeCategory.
constexpr bool is_integer_type(TypeCategory cat) {
	switch (cat) {
	case TypeCategory::Char:
	case TypeCategory::UnsignedChar:
	case TypeCategory::WChar:
	case TypeCategory::Char8:
	case TypeCategory::Char16:
	case TypeCategory::Char32:
	case TypeCategory::Short:
	case TypeCategory::UnsignedShort:
	case TypeCategory::Int:
	case TypeCategory::UnsignedInt:
	case TypeCategory::Long:
	case TypeCategory::UnsignedLong:
	case TypeCategory::LongLong:
	case TypeCategory::UnsignedLongLong:
		return true;
	default:
		return false;
	}
}

constexpr bool is_bool_type(TypeCategory cat) {
	return cat == TypeCategory::Bool;
}

// WChar signedness is target-dependent: unsigned on Windows (LLP64), signed on Linux (LP64).
// Char8/Char16/Char32 are always unsigned per C++20.
inline bool is_unsigned_integer_type(TypeCategory cat) {
	switch (cat) {
	case TypeCategory::UnsignedChar:
	case TypeCategory::UnsignedShort:
	case TypeCategory::UnsignedInt:
	case TypeCategory::UnsignedLong:
	case TypeCategory::UnsignedLongLong:
	case TypeCategory::Char8:
	case TypeCategory::Char16:
	case TypeCategory::Char32:
		return true;
	case TypeCategory::WChar:
		return g_target_data_model == TargetDataModel::LLP64;
	default:
		return false;
	}
}

constexpr bool is_floating_point_type(TypeCategory cat) {
	return isFloatingPointType(cat);
}

inline bool is_signed_integer_type(TypeCategory cat) {
	// Note: plain char is treated as signed, matching the most common implementations.
	// WChar is target-dependent: signed on Linux (LP64), unsigned on Windows (LLP64).
	switch (cat) {
	case TypeCategory::Char:
	case TypeCategory::Short:
	case TypeCategory::Int:
	case TypeCategory::Long:
	case TypeCategory::LongLong:
		return true;
	case TypeCategory::WChar:
		return g_target_data_model != TargetDataModel::LLP64;
	default:
		return false;
	}
}

constexpr bool is_standard_arithmetic_type(TypeCategory cat) {
	return is_integer_type(cat) || is_floating_point_type(cat) || is_bool_type(cat);
}

// TypeCategory overload for isUnsignedIntegralType — mirrors the Type-based version above.
inline bool isUnsignedIntegralType(TypeCategory cat) {
	return is_unsigned_integer_type(cat);
}

// Helper to calculate alignment from size in bytes
// Standard alignment rules: min(size, 8) for most platforms, with special case for long double
inline size_t calculate_alignment_from_size(size_t size_in_bytes, TypeCategory cat) {
	// Special case for long double on x86-64: often has 16-byte alignment
	if (cat == TypeCategory::LongDouble) {
		return 16;
	}
	// Standard alignment: same as size, up to 8 bytes
	return (size_in_bytes < 8) ? size_in_bytes : 8;
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

	ASTNode original_member_node;			  // authoritative source declaration
	StringHandle template_owner_name;		  // e.g. integral_constant
	StringHandle instantiated_owner_name;	  // e.g. integral_constant$hash

	StringHandle original_lookup_name;		   // parsed spelling: operator value_type / operator T
	StringHandle instantiated_lookup_name;	   // canonical spelling: operator int / operator float (0 = same as original)

	OverloadableOperator operator_kind = OverloadableOperator::None;	 // valid when operator overload
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
	SaveHandle body_start;					   // Handle to saved position at '{'
	SaveHandle initializer_list_start;		   // Handle to saved position at ':' for ctor initializer list
	size_t struct_type_index;				  // Type index (0 during template definition)
	bool has_initializer_list;				   // True if constructor has an initializer list
	InlineVector<StringHandle, 4> template_param_names; // Template parameter names
};

/// Helper function to get the C++ name string for a type category.
/// Returns the string used in C++ source code (e.g., "int", "unsigned long")
/// Returns empty string_view for non-primitive types
inline std::string_view getTypeName(TypeCategory cat) {
	switch (cat) {
	case TypeCategory::Int:
		return "int";
	case TypeCategory::UnsignedInt:
		return "unsigned int";
	case TypeCategory::Long:
		return "long";
	case TypeCategory::UnsignedLong:
		return "unsigned long";
	case TypeCategory::LongLong:
		return "long long";
	case TypeCategory::UnsignedLongLong:
		return "unsigned long long";
	case TypeCategory::Short:
		return "short";
	case TypeCategory::UnsignedShort:
		return "unsigned short";
	case TypeCategory::Char:
		return "char";
	case TypeCategory::UnsignedChar:
		return "unsigned char";
	case TypeCategory::WChar:
		return "wchar_t";
	case TypeCategory::Char8:
		return "char8_t";
	case TypeCategory::Char16:
		return "char16_t";
	case TypeCategory::Char32:
		return "char32_t";
	case TypeCategory::Bool:
		return "bool";
	case TypeCategory::Float:
		return "float";
	case TypeCategory::Double:
		return "double";
	case TypeCategory::LongDouble:
		return "long double";
	case TypeCategory::Void:
		return "void";
	default:
		return "";
	}
}

/// Helper function to determine if a type category is signed (for MOVSX vs MOVZX)
inline bool isSignedType(TypeCategory cat) {
	switch (cat) {
	case TypeCategory::Char:
	case TypeCategory::Short:
	case TypeCategory::Int:
	case TypeCategory::Long:
	case TypeCategory::LongLong:
		return true;
	case TypeCategory::WChar:
		return g_target_data_model != TargetDataModel::LLP64;
	default:
		return false;
	}
}

enum class Linkage : uint8_t {
	None,		   // Default C++ linkage (with name mangling)
	C,			  // C linkage (no name mangling)
	CPlusPlus,	   // Explicit C++ linkage
	DllImport,	   // __declspec(dllimport) - symbol imported from DLL
	DllExport,	   // __declspec(dllexport) - symbol exported from DLL
};

// Calling conventions (primarily for x86, but tracked for compatibility and diagnostics)
enum class CallingConvention : uint8_t {
	Default,		 // Platform default (x64: Microsoft x64, x86: __cdecl)
	Cdecl,		   // __cdecl - caller cleans stack, supports variadic
	Stdcall,		 // __stdcall - callee cleans stack, no variadic
	Fastcall,		  // __fastcall - first args in registers
	Vectorcall,		// __vectorcall - optimized for SIMD
	Thiscall,		  // __thiscall - C++ member functions (this in register)
	Clrcall,		 // __clrcall - .NET/CLI interop
};

// Access specifier for struct/class members
enum class AccessSpecifier {
	Public,
	Protected,
	Private
};

// Friend declaration types
enum class FriendKind {
	Function,	  // friend void func();
	Class,		   // friend class ClassName;
	MemberFunction, // friend void Class::func();
	TemplateClass  // template<typename T1, typename T2> friend struct pair;
};

// Base class specifier for inheritance
struct BaseClassSpecifier {
	std::string_view name;		   // Base class name
	TypeIndex type_index;		  // Index into gTypeInfo for base class type
	AccessSpecifier access;		// Inheritance access (public/protected/private)
	bool is_virtual;			 // True for virtual inheritance (Phase 3)
	size_t offset;			   // Offset of base subobject in derived class
	bool is_deferred;			  // True for template parameters (resolved at instantiation)

	BaseClassSpecifier(std::string_view n, TypeIndex tidx, AccessSpecifier acc, bool virt = false, size_t off = 0, bool deferred = false)
		: name(n), type_index(tidx), access(acc), is_virtual(virt), offset(off), is_deferred(deferred) {}
};

// Deferred base class specifier for decltype bases in templates
// These are resolved during template instantiation
struct DeferredBaseClassSpecifier {
	ASTNode decltype_expression;	 // The parsed decltype expression
	AccessSpecifier access;		// Inheritance access (public/protected/private)
	bool is_virtual;				 // True for virtual inheritance

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
	TypeIndex return_type_index{};
	std::vector<TypeIndex> parameter_type_indices;
	Linkage linkage = Linkage::None;			 // C vs C++ linkage
	std::optional<std::string> class_name;	   // For member function pointers
	bool is_const = false;					   // For const member functions
	bool is_volatile = false;				  // For volatile member functions

	// Accessor helpers
	TypeCategory returnType() const { return return_type_index.category(); }
};

// Deferred static_assert information - stored during template definition, evaluated during instantiation
struct DeferredStaticAssert {
	ASTNode condition_expr;	// The condition expression to evaluate
	StringHandle message;	  // The assertion message (interned in StringTable for concatenated literals)

	DeferredStaticAssert(ASTNode expr, StringHandle msg)
		: condition_expr(expr), message(msg) {}
};

// Struct member information
struct StructMember {
	StringHandle name;
	TypeIndex type_index;	  // Index into gTypeInfo for complex types (structs, etc.); category encodes Type
	size_t offset;		   // Offset in bytes from start of struct
	size_t size;			 // Size in bytes
	std::optional<size_t> bitfield_width; // Width in bits for bitfield members
	size_t bitfield_bit_offset = 0; // Bit offset within the storage unit for bitfield members
	size_t referenced_size_bits; // Size of the referenced value in bits (for references)
	size_t alignment;		  // Alignment requirement
	AccessSpecifier access; // Access level (public/protected/private)
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None;  // None, LValueReference (&), or RValueReference (&&)
	std::optional<ASTNode> default_initializer;	// C++11 default member initializer
	bool is_array;		   // True if member is an array
	std::vector<size_t> array_dimensions;  // Dimensions for multidimensional arrays
	int pointer_depth;	   // Pointer indirection level (e.g., int* = 1, int** = 2)
	std::optional<FunctionSignature> function_signature;	 // For FunctionPointer members: stores return type and parameter types

	// Convenience helpers for common checks
	bool is_reference() const { return reference_qualifier != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return reference_qualifier == ReferenceQualifier::RValueReference; }
	TypeCategory memberType() const { return type_index.category(); }

	StructMember(StringHandle n, TypeIndex tidx, size_t off, size_t sz, size_t align,
				 AccessSpecifier acc,
				 std::optional<ASTNode> init,
				 ReferenceQualifier ref_qual,
				 size_t ref_size_bits,
				 bool is_arr,
				 std::vector<size_t> arr_dims,
				 int ptr_depth,
				 std::optional<size_t> bf_width)
		: name(n), type_index(tidx), offset(off), size(sz),
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
	bool is_constructor;	 // True if this is a constructor
	bool is_destructor;		// True if this is a destructor
	OverloadableOperator operator_kind; // None for non-operators; non-None implies operator overload
	TypeIndex conversion_target_type;	 // Canonical target type for conversion operators; invalid otherwise

	// Virtual function support (Phase 2)
	bool is_virtual = false;		 // True if declared with 'virtual' keyword
	bool is_pure_virtual = false;	  // True if pure virtual (= 0)
	bool is_override = false;		  // True if declared with 'override' keyword
	bool is_final = false;		   // True if declared with 'final' keyword
	int vtable_index = -1;		   // Index in vtable, -1 if not virtual

	// CV qualifiers for member functions (Phase 4)
	CVQualifier cv_qualifier = CVQualifier::None;

	// noexcept tracking for type traits
	bool is_noexcept = false;		  // True if declared noexcept (e.g., void foo() noexcept)

	// Convenience accessors
	bool is_operator_overload() const { return operator_kind != OverloadableOperator::None; }
	bool is_conversion_operator() const { return conversion_target_type.is_valid(); }
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
	const void* vtable;				// Pointer to type_info vtable (usually null in our case)
	const void* spare;			   // Reserved/spare pointer (unused)
	char name[1];					// Variable-length mangled name (null-terminated)
};

// ??_R1 - Base Class Descriptor
struct MSVCBaseClassDescriptor {
	const MSVCTypeDescriptor* type_descriptor;  // Pointer to base class type descriptor (??_R0)
	uint32_t num_contained_bases;	  // Number of nested base classes
	int32_t mdisp;				   // Member displacement (offset in class)
	int32_t pdisp;				   // Vbtable displacement (-1 if not virtual base)
	int32_t vdisp;				   // Displacement inside vbtable (0 if not virtual base)
	uint32_t attributes;			 // Flags (virtual, ambiguous, etc.)
};

// ??_R2 - Base Class Array (array of pointers to ??_R1)
struct MSVCBaseClassArray {
	const MSVCBaseClassDescriptor* base_class_descriptors[1]; // Variable-length array
};

// ??_R3 - Class Hierarchy Descriptor
struct MSVCClassHierarchyDescriptor {
	uint32_t signature;				// Always 0
	uint32_t attributes;			 // Bit flags (multiple inheritance, virtual inheritance, etc.)
	uint32_t num_base_classes;	   // Number of base classes (including self)
	const MSVCBaseClassArray* base_class_array;	// Pointer to base class array (??_R2)
};

// ??_R4 - Complete Object Locator (referenced by vtable)
struct MSVCCompleteObjectLocator {
	uint32_t signature;				// 0 for 32-bit, 1 for 64-bit
	uint32_t offset;				 // Offset of this vtable in the complete class
	uint32_t cd_offset;				// Constructor displacement offset
	const MSVCTypeDescriptor* type_descriptor;		   // Pointer to type descriptor (??_R0)
	const MSVCClassHierarchyDescriptor* hierarchy;	   // Pointer to class hierarchy (??_R3)
};

// Itanium C++ ABI RTTI structures - standard format for Linux/Unix systems
// These structures match the Itanium C++ ABI specification for RTTI

// Base class info structure for __vmi_class_type_info
struct ItaniumBaseClassTypeInfo {
	const void* base_type;	   // Pointer to base class type_info (__class_type_info*)
	int64_t offset_flags;		  // Combined offset and flags

	// Flags in offset_flags:
	// bit 0: __virtual_mask (0x1) - base class is virtual
	// bit 1: __public_mask (0x2) - base class is public
	// bits 8+: offset of base class in derived class (signed)
};

// __class_type_info - Type info for classes without base classes
struct ItaniumClassTypeInfo {
	const void* vtable;			// Pointer to vtable for __class_type_info
	const char* name;			  // Mangled type name (e.g., "3Foo" for class Foo)
};

// __si_class_type_info - Type info for classes with single, public, non-virtual base
struct ItaniumSIClassTypeInfo {
	const void* vtable;			// Pointer to vtable for __si_class_type_info
	const char* name;			  // Mangled type name
	const void* base_type;	   // Pointer to base class type_info (__class_type_info*)
};

// __vmi_class_type_info - Type info for classes with multiple or virtual bases
struct ItaniumVMIClassTypeInfo {
	const void* vtable;			// Pointer to vtable for __vmi_class_type_info
	const char* name;			  // Mangled type name
	uint32_t flags;				// Inheritance flags
	uint32_t base_count;		 // Number of direct base classes
	ItaniumBaseClassTypeInfo base_info[1];  // Variable-length array of base class info

	// Flags:
	// __non_diamond_repeat_mask = 0x1 - has repeated bases (but not diamond)
	// __diamond_shaped_mask = 0x2     - has diamond-shaped inheritance
};

// Legacy RTTITypeInfo for compatibility with existing code
// This will hold references to both MSVC and Itanium structures
struct RTTITypeInfo {
	const char* type_name;		   // Mangled type name
	const char* demangled_name;		// Human-readable type name
	size_t num_bases;				  // Number of base classes
	const RTTITypeInfo** base_types; // Array of pointers to base class type_info

	// MSVC RTTI structures
	MSVCCompleteObjectLocator* col;			// ??_R4 - Complete Object Locator
	MSVCClassHierarchyDescriptor* chd;	   // ??_R3 - Class Hierarchy Descriptor
	MSVCBaseClassArray* bca;				 // ??_R2 - Base Class Array
	std::vector<MSVCBaseClassDescriptor*> base_descriptors;	// ??_R1 - Base Class Descriptors
	MSVCTypeDescriptor* type_descriptor;	 // ??_R0 - Type Descriptor

	// Itanium C++ ABI RTTI structures
	void* itanium_type_info;		 // Pointer to __class_type_info, __si_class_type_info, or __vmi_class_type_info
	enum class ItaniumTypeInfoKind {
		None,
		ClassTypeInfo,	   // __class_type_info (no bases)
		SIClassTypeInfo,	 // __si_class_type_info (single inheritance)
		VMIClassTypeInfo	 // __vmi_class_type_info (multiple/virtual inheritance)
	} itanium_kind;

	RTTITypeInfo(const char* mangled, const char* demangled, size_t num_base = 0)
		: type_name(mangled), demangled_name(demangled), num_bases(num_base), base_types(nullptr),
		  col(nullptr), chd(nullptr), bca(nullptr), type_descriptor(nullptr),
		  itanium_type_info(nullptr), itanium_kind(ItaniumTypeInfoKind::None) {}

	// Check if this type is derived from another type (for dynamic_cast)
	bool isDerivedFrom(const RTTITypeInfo* base) const {
		if (this == base)
			return true;

		for (size_t i = 0; i < num_bases; ++i) {
			if (base_types[i] && base_types[i]->isDerivedFrom(base)) {
				return true;
			}
		}
		return false;
	}
};

// Pre-materialized initializer for static-storage members.
// Populated during or right after template instantiation so that codegen
// and constexpr evaluation can consume pre-packed bytes directly instead
// of re-evaluating retained AST.
struct NormalizedInitializer {
	enum class Kind : uint8_t {
		Uninitialized,					// No initializer or not yet classified
		ConstantBytes,					// Fully evaluated compile-time constant bytes
		Relocation,						// Requires a data relocation (address-of a symbol)
		NormalizedAst,					// AST was normalized but still requires late emission
		DynamicInitializationRequired	// Requires runtime initialization
	};

	Kind kind = Kind::Uninitialized;
	std::vector<char> constant_bytes;	// Pre-packed byte representation (for ConstantBytes)
	StringHandle reloc_target;			// Symbol name for data relocation (for Relocation)

	bool isConstant() const { return kind == Kind::ConstantBytes && !constant_bytes.empty(); }
};

// Static member information
struct StructStaticMember {
	StringHandle name;
	TypeIndex type_index;	  // Index into gTypeInfo for complex types; category encodes Type
	size_t size;			 // Size in bytes
	size_t alignment;		  // Alignment requirement
	AccessSpecifier access; // Access level (public/protected/private)
	std::optional<ASTNode> initializer;	// Optional initializer expression
	CVQualifier cv_qualifier = CVQualifier::None;  // CV qualifiers (const, volatile)
	ReferenceQualifier reference_qualifier = ReferenceQualifier::None;  // None, LValueReference (&), or RValueReference (&&)
	int pointer_depth = 0;  // Pointer indirection level (e.g., int* = 1, int** = 2)

	// Pre-materialized initializer (Phase C).
	// When populated, codegen prefers this over re-evaluating the raw AST initializer.
	std::optional<NormalizedInitializer> normalized_init;

	// Convenience helpers for common checks
	bool is_const() const { return hasCVQualifier(cv_qualifier, CVQualifier::Const); }
	bool is_reference() const { return reference_qualifier != ReferenceQualifier::None; }
	bool is_rvalue_reference() const { return reference_qualifier == ReferenceQualifier::RValueReference; }
	TypeCategory memberType() const { return type_index.category(); }

	StructStaticMember(StringHandle n, TypeIndex tidx, size_t sz, size_t align, AccessSpecifier acc = AccessSpecifier::Public,
					   std::optional<ASTNode> init = std::nullopt, CVQualifier cv_qual = CVQualifier::None,
					   ReferenceQualifier ref_qual = ReferenceQualifier::None, int ptr_depth = 0)
		: name(n), type_index(tidx), size(sz), alignment(align), access(acc),
		  initializer(init), cv_qualifier(cv_qual), reference_qualifier(ref_qual), pointer_depth(ptr_depth) {}

	StringHandle getName() const {
		return name;
	}
};
