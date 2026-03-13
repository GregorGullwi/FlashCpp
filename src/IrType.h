#pragma once

#include "AstNodeTypes_TypeSystem.h"

// ============================================================================
// IrType — Runtime representation types for IR/codegen.
//
// Design intent:
//   This is the *only* type enum that IR op structs and backend code should use
//   for runtime decisions (register selection, ABI classification, instruction
//   emission).  It deliberately omits semantic-only variants like Enum,
//   UserDefined, Auto, and Template so that codegen code physically cannot
//   branch on them.
//
//   Enum identity, typedef aliases, and template parameters are preserved in
//   the AST / semantic layer (Type enum) and are still available for overload
//   resolution, mangling, type traits, and diagnostics.
//
//   See docs/2026-03-12_ENUM_IR_LOWERING_PLAN.md for the full design rationale.
// ============================================================================
enum class IrType : int_fast16_t {
	Void,
	Integer,                // All integer types, enums, bool — distinguished by size_in_bits + is_signed
	Float,                  // 32-bit IEEE 754
	Double,                 // 64-bit IEEE 754
	LongDouble,             // 80-bit x87 extended precision
	Struct,                 // Needs type_index for size/layout, ABI classification
	FunctionPointer,
	MemberFunctionPointer,
	MemberObjectPointer,
	Nullptr,
};

// ============================================================================
// toIrType — Convert a semantic Type to its runtime IrType representation.
//
// This is a pure mapping function.  It does NOT erase enum identity globally;
// it only answers "what runtime representation should IR operations use?"
//
// Precondition: the input Type must be a concrete type that is valid in IR.
//   Types like Auto, Template, Function, and Invalid must be resolved before
//   this function is called.  Hitting those cases is a compiler bug.
// ============================================================================
inline IrType toIrType(Type semantic_type) {
	switch (semantic_type) {
		// All integer-like types map to IrType::Integer
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
			return IrType::Integer;

		// Enums lower to Integer — size/signedness come from EnumTypeInfo
		case Type::Enum:
			return IrType::Integer;

		// Floating point types
		case Type::Float:
			return IrType::Float;
		case Type::Double:
			return IrType::Double;
		case Type::LongDouble:
			return IrType::LongDouble;

		// Aggregate / user-defined types
		case Type::Struct:
		case Type::UserDefined:
			return IrType::Struct;

		// Pointer-like types
		case Type::FunctionPointer:
			return IrType::FunctionPointer;
		case Type::MemberFunctionPointer:
			return IrType::MemberFunctionPointer;
		case Type::MemberObjectPointer:
			return IrType::MemberObjectPointer;

		// Special types
		case Type::Nullptr:
			return IrType::Nullptr;
		case Type::Void:
			return IrType::Void;

		// These must not reach IR — they must be resolved before codegen.
		// During the transition period (Phase 0-1), we tolerate these and
		// map them to a safe default.  A future phase will add assertions
		// once all semantic-only types are resolved before IR construction.
		case Type::Auto:
			return IrType::Void;
		case Type::Template:
			return IrType::Void;
		case Type::Function:
			return IrType::FunctionPointer;
		case Type::Invalid:
			return IrType::Void;
	}
	assert(false && "Unknown Type value in toIrType");
	return IrType::Void;
}

// ============================================================================
// IrType classification helpers
//
// These replace the semantic-layer helpers (is_integer_type, is_floating_point_type,
// etc.) for backend code.  They operate on IrType and are exhaustive — no
// `Type::Enum` special cases needed.
// ============================================================================

/// True for IrType::Integer (covers all integral types including enums and bool).
inline bool isIrIntegerType(IrType t) { return t == IrType::Integer; }

/// True for IrType::Float, Double, or LongDouble.
inline bool isIrFloatingPointType(IrType t) {
	return t == IrType::Float || t == IrType::Double || t == IrType::LongDouble;
}

/// True for types that need struct-level ABI handling (size/layout via type_index).
inline bool isIrStructType(IrType t) { return t == IrType::Struct; }

/// True for types that represent pointer-like values in IR
/// (function pointers, member pointers, nullptr).
inline bool isIrPointerLikeType(IrType t) {
	return t == IrType::FunctionPointer || t == IrType::MemberFunctionPointer ||
	       t == IrType::MemberObjectPointer || t == IrType::Nullptr;
}

// ============================================================================
// Formatting support for IrType (for FLASH_LOG_FORMAT and std::format)
// ============================================================================
inline std::string_view irTypeName(IrType t) {
	switch (t) {
		case IrType::Void:                   return "Void";
		case IrType::Integer:                return "Integer";
		case IrType::Float:                  return "Float";
		case IrType::Double:                 return "Double";
		case IrType::LongDouble:             return "LongDouble";
		case IrType::Struct:                 return "Struct";
		case IrType::FunctionPointer:        return "FunctionPointer";
		case IrType::MemberFunctionPointer:  return "MemberFunctionPointer";
		case IrType::MemberObjectPointer:    return "MemberObjectPointer";
		case IrType::Nullptr:                return "Nullptr";
	}
	return "Unknown";
}

template<>
struct std::formatter<IrType, char> : std::formatter<std::string_view, char> {
	auto format(IrType t, std::format_context& ctx) const {
		return std::formatter<std::string_view, char>::format(irTypeName(t), ctx);
	}
};

inline std::ostream& operator<<(std::ostream& os, IrType t) {
	return os << irTypeName(t);
}
