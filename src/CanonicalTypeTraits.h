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

// Evaluates the unary [meta.unary.prop] traits whose answer is a function of the
// canonical type's structural shape: references, pointers, arrays, functions,
// member pointers, enums, builtins, and the arithmetic/scalar/fundamental/
// object/compound groupings derived from them. The canonical node is the only
// authority; no TypeInfo, TypeIndex, pointer level, or array dimension is read.
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
