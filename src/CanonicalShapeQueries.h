#pragma once

#include <optional>
#include <span>

#include "CanonicalTypes.h"

// Reference stripping plus the top-level cv/array walk. The existing helpers live in
// OverloadResolution.h, which includes this adapter layer and cannot be included here.
inline TypeId shapeWithoutReference(const CanonicalTypeTable& table, TypeId type) {
	while (true) {
		const CanonicalTypeNode node = table.node(type);
		if (node.kind != CanonicalTypeKind::LValueReference && node.kind != CanonicalTypeKind::RValueReference) {
			return type;
		}
		type = node.child;
	}
}

inline TypeId shapeWithoutTopCv(const CanonicalTypeTable& table, TypeId type) {
	while (table.node(type).kind == CanonicalTypeKind::Qualified) {
		type = table.node(type).child;
	}
	return type;
}

inline TypeId shapeAfterTopCvAndReference(const CanonicalTypeTable& table, TypeId type) {
	return shapeWithoutTopCv(table, shapeWithoutReference(table, type));
}

inline bool canonicalIsPointer(const CanonicalTypeTable& table, TypeId type) {
	return table.node(shapeAfterTopCvAndReference(table, type)).kind == CanonicalTypeKind::Pointer;
}

inline std::optional<TypeId> canonicalPointee(const CanonicalTypeTable& table, TypeId type) {
	const CanonicalTypeNode node = table.node(shapeAfterTopCvAndReference(table, type));
	if (node.kind != CanonicalTypeKind::Pointer) {
		return std::nullopt;
	}
	return node.child;
}

inline std::optional<TypeId> canonicalArrayElement(const CanonicalTypeTable& table, TypeId type) {
	const CanonicalTypeNode node = table.node(shapeAfterTopCvAndReference(table, type));
	if (node.kind != CanonicalTypeKind::Array) {
		return std::nullopt;
	}
	return node.child;
}

// Unknown bounds are stored as extent 0. A non-array has a count of 0, so an unknown bound stays distinct.
inline size_t canonicalArrayExtentCount(const CanonicalTypeTable& table, TypeId type) {
	type = shapeAfterTopCvAndReference(table, type);
	size_t count = 0;
	while (table.node(type).kind == CanonicalTypeKind::Array) {
		const CanonicalTypeNode node = table.node(type);
		++count;
		if (!node.child) {
			break;
		}
		type = node.child;
		while (table.node(type).kind == CanonicalTypeKind::Qualified && table.node(type).child) {
			type = table.node(type).child;
		}
	}
	return count;
}

inline void canonicalCopyArrayExtents(const CanonicalTypeTable& table, TypeId type, std::span<size_t> extents) {
	const size_t count = canonicalArrayExtentCount(table, type);
	if (extents.size() < count) {
		throw InternalError("canonical array extent span is too small");
	}
	type = shapeAfterTopCvAndReference(table, type);
	size_t index = 0;
	while (table.node(type).kind == CanonicalTypeKind::Array) {
		const CanonicalTypeNode node = table.node(type);
		extents[index] = hasCanonicalTypeNodeFlag(node.flags, CanonicalTypeNodeFlags::KnownArrayBound) ? static_cast<size_t>(node.array_extent) : 0;
		++index;
		if (!node.child) {
			break;
		}
		type = node.child;
		while (table.node(type).kind == CanonicalTypeKind::Qualified && table.node(type).child) {
			type = table.node(type).child;
		}
	}
}

inline std::optional<size_t> canonicalArrayElementCount(const CanonicalTypeTable& table, TypeId type) {
	type = shapeAfterTopCvAndReference(table, type);
	const CanonicalTypeNode node = table.node(type);
	if (node.kind != CanonicalTypeKind::Array) {
		return std::nullopt;
	}
	if (!hasCanonicalTypeNodeFlag(node.flags, CanonicalTypeNodeFlags::KnownArrayBound)) {
		return 0;
	}
	return static_cast<size_t>(node.array_extent);
}

inline size_t canonicalPointerLevelCount(const CanonicalTypeTable& table, TypeId type) {
	type = shapeWithoutReference(table, type);
	size_t count = 0;
	while (true) {
		while (table.node(type).kind == CanonicalTypeKind::Qualified) {
			type = table.node(type).child;
		}
		if (table.node(type).kind != CanonicalTypeKind::Pointer) {
			return count;
		}
		++count;
		type = table.node(type).child;
	}
}

inline void canonicalCopyPointerLevelCvs(const CanonicalTypeTable& table, TypeId type, std::span<CVQualifier> qualifiers) {
	const size_t count = canonicalPointerLevelCount(table, type);
	if (qualifiers.size() < count) {
		throw InternalError("canonical pointer cv span is too small");
	}
	type = shapeWithoutReference(table, type);
	size_t index = 0;
	while (true) {
		CanonicalTypeNode node = table.node(type);
		CVQualifier cv = CVQualifier::None;
		while (node.kind == CanonicalTypeKind::Qualified) {
			cv |= node.qualifiers;
			type = node.child;
			node = table.node(type);
		}
		if (node.kind != CanonicalTypeKind::Pointer) {
			return;
		}
		qualifiers[index] = cv;
		++index;
		type = node.child;
	}
}

inline bool canonicalSameIgnoringTopCvRef(const CanonicalTypeTable& table, TypeId left, TypeId right) {
	left = shapeWithoutReference(table, left);
	right = shapeWithoutReference(table, right);
	while (true) {
		const CanonicalTypeNode left_node = table.node(left);
		const CanonicalTypeNode right_node = table.node(right);
		if (left_node.kind == CanonicalTypeKind::Array || right_node.kind == CanonicalTypeKind::Array) {
			if (left_node.kind != CanonicalTypeKind::Array || right_node.kind != CanonicalTypeKind::Array || left_node.flags != right_node.flags ||
				left_node.array_extent != right_node.array_extent) {
				return false;
			}
			left = left_node.child;
			right = right_node.child;
			continue;
		}
		if (left_node.kind == CanonicalTypeKind::Qualified) {
			left = left_node.child;
		}
		if (right_node.kind == CanonicalTypeKind::Qualified) {
			right = right_node.child;
		}
		return left == right;
	}
}
