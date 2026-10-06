// Canonical type-trait classification. This is the boundary-3A answer to a
// `[meta.unary.prop]` question, kept in its own header because the shared trait
// evaluator and the lazy-constraint evaluator both need it and they sit on
// opposite sides of the `SymbolTable.h` -> `TemplateRegistry.h` include edge.
// Pulling `TypeTraitEvaluator.h` from the template side closes that cycle, so
// the classification API lives here instead, below both of them.
#pragma once

#include "CanonicalTypeAdapter.h"

#include <optional>

// Result of one canonical type-trait evaluation. `success` false means the
// trait could not be decided at all, which is distinct from a decided `false`.
struct TypeTraitResult {
	bool success;
	bool value;

	static TypeTraitResult success_true() { return {true, true}; }
	static TypeTraitResult success_false() { return {true, false}; }
	static TypeTraitResult failure() { return {false, false}; }
};

// Evaluates unary type-property traits from canonical type structure and
// published nominal record facts: references, pointers, arrays, functions,
// member pointers, enums, builtins, class/union identity, and record properties.
// No TypeInfo, TypeIndex, pointer level, array dimension, or spelling is read.
// An empty result allows compatibility evaluation for an unmigrated projectable
// operand; unsupported non-projectable operands fail.
std::optional<TypeTraitResult> tryEvaluateCanonicalStructuralTrait(
	TypeTraitKind kind,
	const TypeSpecifierNode& type_spec);

// Evaluates `__is_same` from canonical `TypeId` identity. An empty result allows
// compatibility evaluation for an unmigrated or dependent operand; a malformed
// operand fails.
std::optional<TypeTraitResult> tryEvaluateCanonicalSameTrait(
	const TypeSpecifierNode& lhs,
	const TypeSpecifierNode& rhs);

// Whether evaluateConstructibility falls back to the sema compatibility answer
// when the canonical table cannot decide. The lazy constraint path asks for
// None so an undecidable operand keeps its unknown constraint answer; the
// folded path asks for Sema so an unmigrated operand keeps its sema answer.
enum class ConstructibilityFallback : uint8_t {
	None,
	Sema,
};

// Evaluates a __is_constructible, __is_trivially_constructible, or
// __is_nothrow_constructible query from canonical identity. A zero-argument
// query is the default-construction question answered from the published
// construction fact; an argument-bearing query resolves a constructor for a
// record target and applies the implicit conversion rules for a reference or
// scalar target. An empty result is possible only for
// ConstructibilityFallback::None and lets the caller keep its own answer.
std::optional<TypeTraitResult> evaluateConstructibility(
	TypeTraitKind kind,
	const TypeSpecifierNode& target,
	std::span<const TypeSpecifierNode> arguments,
	ConstructibilityFallback fallback);
