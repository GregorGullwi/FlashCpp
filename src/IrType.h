#pragma once

#include <cassert>
#include <format>
#include <ostream>
#include <string_view>

#include "AstNodeTypes_DeclNodes.h"
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
	Integer,                // Deliberately coarse: width stays in size_in_bits, signedness in is_signed
	Float,                  // 32-bit IEEE 754
	Double,                 // 64-bit IEEE 754
	LongDouble,             // 80-bit x87 extended precision
	Struct,                 // Runtime family only; exact layout/ABI still comes from type_index + size metadata
	FunctionPointer,
	MemberFunctionPointer,  // Kept distinct until member-pointer ABI/layout is lowered more explicitly
	MemberObjectPointer,    // Kept distinct until member-pointer ABI/layout is lowered more explicitly
	Nullptr,                // Transitional null-pointer family; can disappear once lowered earlier to concrete zero/pointer form
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
IrType toIrType(Type semantic_type);

// TypeCategory overload — delegates via categoryToType().
// For TypeCategory::TypeAlias, this returns IrType::Struct (Type::UserDefined).
// Callers that need alias-resolved IR type should use toIrType(TypeInfo) below.
inline IrType toIrType(TypeCategory cat) { return toIrType(categoryToType(cat)); }

// TypeInfo overload — uses resolvedType() for correct alias resolution.
// For primitive-aliased TypeInfo entries, this returns IrType::Integer instead
// of IrType::Struct that a naive category() lookup would give.
inline IrType toIrType(const TypeInfo& ti) { return toIrType(ti.resolvedType()); }

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
inline bool isIrFloatingPointType(IrType t) { return t == IrType::Float || t == IrType::Double || t == IrType::LongDouble; }

/// True for types that need struct-level ABI handling (size/layout via type_index).
inline bool isIrStructType(IrType t) { return t == IrType::Struct; }

/// True for types that represent pointer-like values in IR
/// (function pointers, member pointers, nullptr).
inline bool isIrPointerLikeType(IrType t) { return t == IrType::FunctionPointer || t == IrType::MemberFunctionPointer || t == IrType::MemberObjectPointer || t == IrType::Nullptr; }

// ============================================================================
// Semantic-layer classification helpers
//
// These operate on the semantic Type enum and are used by codegen paths that
// need to decide whether a value carries a meaningful type_index (for struct
// layout, enum identity, or typedef resolution).  Unlike the IrType helpers
// above, they do NOT erase semantic identity — they just centralise the
// recurring "Struct || Enum || UserDefined" pattern into a single name.
// ============================================================================

/// True for types that carry a meaningful type_index in IR metadata: Struct,
/// Enum, and UserDefined.  Use this when deciding whether to propagate
/// type_index through ExprResult / TypedValue rather than hard-coding the
/// three-way disjunction at each call site.
inline bool carriesSemanticTypeIndex(Type t) { return needs_type_index(t); }
/// TypeCategory overload — delegates to the Type version via categoryToType.
inline bool carriesSemanticTypeIndex(TypeCategory cat) { return carriesSemanticTypeIndex(categoryToType(cat)); }

// ============================================================================
// Formatting support for IrType (for FLASH_LOG_FORMAT and std::format)
// ============================================================================
std::string_view irTypeName(IrType t);

template<>
struct std::formatter<IrType, char> : std::formatter<std::string_view, char> {
	auto format(IrType t, std::format_context& ctx) const {
		return std::formatter<std::string_view, char>::format(irTypeName(t), ctx);
	}
};

inline std::ostream& operator<<(std::ostream& os, IrType t) {
	return os << irTypeName(t);
}
