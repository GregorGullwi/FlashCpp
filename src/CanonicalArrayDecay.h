#pragma once

#include <limits>
#include <optional>

#include "CanonicalShapeQueries.h"

// One subscript or array-to-pointer step.
//
// C++20 [conv.array]/1 converts an array lvalue to a pointer to its first
// element, and [expr.sub]/1 selects one element of an array or one pointee of a
// pointer. Both operations select the same object, so a consumer that needs the
// selected object's type or stride reads it from here instead of from the
// expression result. The result type can already be the decayed pointer (for
// example the sema slot of `grid[1]` where grid is int[2][3]), and that pointer
// width is not the row stride.
//
// The element is the canonical array element or pointer pointee. `operand_type`
// is the inspected canonical type, so a consumer can project the decayed shape
// from the same identity this step was derived from.
struct CanonicalArrayDecayStep {
	TypeId operand_type{};
	TypeId element_type{};
	int element_size_bits = 0;
	bool operand_is_array = false;
};

inline std::optional<CanonicalArrayDecayStep> canonicalArrayDecayStep(const CanonicalTypeTable& table, TypeId type) {
	const TypeId operand_type = shapeAfterTopCvAndReference(table, type);
	const CanonicalTypeNode operand_node = table.node(operand_type);
	std::optional<TypeId> element_type;
	bool operand_is_array = false;
	if (operand_node.kind == CanonicalTypeKind::Array) {
		element_type = canonicalArrayElement(table, operand_type);
		operand_is_array = true;
	} else if (operand_node.kind == CanonicalTypeKind::Pointer) {
		element_type = canonicalPointee(table, operand_type);
	}
	if (!element_type.has_value()) {
		return std::nullopt;
	}
	const std::optional<size_t> element_size_bytes = tryGetCanonicalObjectSizeBytes(table, *element_type);
	const CanonicalTypeKind element_kind = table.node(shapeAfterTopCvAndReference(table, *element_type)).kind;
	const bool concrete_element = element_kind == CanonicalTypeKind::Builtin || element_kind == CanonicalTypeKind::Pointer ||
		element_kind == CanonicalTypeKind::Record || element_kind == CanonicalTypeKind::Enum;
	if (!element_size_bytes.has_value() || *element_size_bytes == 0) {
		// A concrete element with no object size is a publication defect; refusing
		// keeps a wrong stride from reaching codegen. A dependent or otherwise
		// unimportable element keeps the existing fallback.
		if (operand_is_array && concrete_element) {
			throw InternalError("canonical array decay step has no element object size");
		}
		return std::nullopt;
	}
	if (*element_size_bytes > static_cast<size_t>(std::numeric_limits<int>::max()) / 8) {
		if (operand_is_array && concrete_element) {
			throw InternalError("canonical array decay step element size exceeds the IR range");
		}
		return std::nullopt;
	}
	CanonicalArrayDecayStep step;
	step.operand_type = type;
	step.element_type = *element_type;
	step.element_size_bits = static_cast<int>(*element_size_bytes * 8);
	step.operand_is_array = operand_is_array;
	return step;
}
