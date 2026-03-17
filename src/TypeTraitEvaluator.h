// TypeTraitEvaluator.h - Shared type trait evaluation logic
// This file provides a unified interface for evaluating type traits
// at both compile time (Parser.cpp) and code generation time (IrGenerator.h)

#pragma once

#include "AstNodeTypes.h"
#include "SymbolTable.h"
#include <optional>

// Result type for type trait evaluation
struct TypeTraitResult {
	bool success;
	bool value;

	static TypeTraitResult success_true() { return {true, true}; }
	static TypeTraitResult success_false() { return {true, false}; }
	static TypeTraitResult failure() { return {false, false}; }
};

// Shared helper functions for type checking in an isolated namespace
// to avoid conflicts with similar functions in other headers
namespace TypeTraitEval {

bool isArithmeticType(Type type);
bool isFundamentalType(Type type);
bool isScalarType(Type type, bool is_reference, size_t pointer_depth);
bool isIntegral(Type type);
bool isFloatingPoint(Type type);
bool isSigned(Type type);
bool isUnsigned(Type type);

} // namespace TypeTraitEval

// Shared helper: determine whether a struct/class is nothrow-destructible.
// Per C++20 [except.spec]/7 and [class.dtor]/3, an implicit/defaulted destructor
// is noexcept unless any direct base class or non-static data member type has a
// noexcept(false) destructor (recursively).  Explicit user-defined destructors
// use the is_noexcept() flag that was eagerly evaluated at parse time.
bool isStructNothrowDestructible(const StructTypeInfo* struct_info);

// Shared helper: determine whether a pseudo-destructor call expression is noexcept.
// Resolves the object's type via symbol lookup (handles template specializations)
// and falls back to gTypesByName lookup by type name token for non-template types.
// Scalar pseudo-destructor calls are always noexcept (no-ops).
bool isPseudoDestructorCallNoexcept(const PseudoDestructorCallNode& pseudo_dtor, SymbolTable& symbols);

// Main type trait evaluation functions
TypeTraitResult evaluateTypeTrait(
	TypeTraitKind kind,
	Type base_type,
	TypeIndex type_idx,
	bool is_reference,
	bool is_rvalue_reference,
	bool is_lvalue_reference,
	size_t pointer_depth,
	CVQualifier cv_qualifier,
	bool is_array,
	std::optional<size_t> array_size,
	const TypeInfo* type_info,
	const StructTypeInfo* struct_info
);

TypeTraitResult evaluateTypeTrait(
	TypeTraitKind kind,
	const TypeSpecifierNode& type_spec,
	const TypeInfo* type_info,
	const StructTypeInfo* struct_info
);
