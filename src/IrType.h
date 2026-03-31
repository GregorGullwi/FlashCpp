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
	Integer,				// Deliberately coarse: width stays in size_in_bits, signedness in is_signed
	Float,				  // 32-bit IEEE 754
	Double,				 // 64-bit IEEE 754
	LongDouble,				// 80-bit x87 extended precision
	Struct,				 // Runtime family only; exact layout/ABI still comes from type_index + size metadata
	FunctionPointer,
	MemberFunctionPointer,  // Kept distinct until member-pointer ABI/layout is lowered more explicitly
	MemberObjectPointer,	 // Kept distinct until member-pointer ABI/layout is lowered more explicitly
	Nullptr,				// Transitional null-pointer family; can disappear once lowered earlier to concrete zero/pointer form
};

// ============================================================================
// toIrType — Convert a TypeCategory to its runtime IrType representation.
//
// This is a pure mapping function.  It does NOT erase enum identity globally;
// it only answers "what runtime representation should IR operations use?"
//
// Precondition: the input TypeCategory must be a concrete type that is valid
//   in IR.  Categories like Auto, Template, Function, and Invalid must be
//   resolved before this function is called.  Hitting those cases is a
//   compiler bug.
// ============================================================================
IrType toIrType(TypeCategory cat);

// TypeIndex overload — uses the cached category carried by the TypeIndex.
// For TypeAlias, resolves through getTypeInfo() to the concrete type so that
// aliases to primitives (e.g., `using MyInt = int`) map to IrType::Integer
// rather than the defensive IrType::Struct fallback in toIrType(TypeCategory).
inline IrType toIrType(TypeIndex type_index) {
	TypeCategory cat = type_index.category();
	if (cat == TypeCategory::TypeAlias) {
		if (const TypeInfo* ti = tryGetTypeInfo(type_index)) {
			cat = ti->category();
		}
	}
	return toIrType(cat);
}

// TypeInfo overload — delegates directly to the TypeCategory embedded in type_index_.
inline IrType toIrType(const TypeInfo& ti) { return toIrType(ti.category()); }

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
/// Enum, UserDefined, and TypeAlias.  Use this when deciding whether to
/// propagate type_index through ExprResult / TypedValue rather than
/// hard-coding the multi-way disjunction at each call site.
inline bool carriesSemanticTypeIndex(TypeCategory cat) { return needs_type_index(cat); }

// ============================================================================
// Formatting support for IrType (for FLASH_LOG_FORMAT and std::format)
// ============================================================================
std::string_view irTypeName(IrType t);

template <>
struct std::formatter<IrType, char> : std::formatter<std::string_view, char> {
	auto format(IrType t, std::format_context& ctx) const {
		return std::formatter<std::string_view, char>::format(irTypeName(t), ctx);
	}
};

inline std::ostream& operator<<(std::ostream& os, IrType t) {
	return os << irTypeName(t);
}
