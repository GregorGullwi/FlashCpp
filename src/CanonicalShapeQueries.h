#pragma once

#include <limits>
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

// C++20 [basic.type.qualifier]/1: the cv-qualification of an array is that of
// its element type, so the qualification of an object type is read through its
// array levels. References are not part of the object type: [expr.ref] adds
// the receiver's cv to the member object, not to a reference the member spells.
inline CVQualifier canonicalTopObjectCv(const CanonicalTypeTable& table, TypeId type) {
	CVQualifier qualifiers = CVQualifier::None;
	type = shapeWithoutReference(table, type);
	while (true) {
		const CanonicalTypeNode node = table.node(type);
		if (node.kind == CanonicalTypeKind::Qualified) {
			qualifiers |= node.qualifiers;
			type = node.child;
			continue;
		}
		if (node.kind != CanonicalTypeKind::Array) {
			return qualifiers;
		}
		type = node.child;
	}
}

// One canonical answer for pointer presence, pointer depth, and per-level cv.
// Every consumer that needs two of those three facts reads this shape instead of
// running separate walks, so presence, depth, and cv cannot disagree.
//
// Ordering is outermost-pointer first, matching the canonical Pointer chain.
// The pointee kind is one exclusive state: a pointer-to-member is pointer-like
// for storage and declarators but is not a canonical Pointer, so `depth` stays
// zero, and a callable pointee is one canonical Pointer level regardless of the
// compatibility category that spelled it.
inline constexpr size_t kCanonicalPointerShapeCapacity = 8;

// The level buffer bound is what keeps depth representable; if the buffer grows
// past the depth type, `++shape.depth` would wrap instead of reaching the
// overflow check that throws.
static_assert(kCanonicalPointerShapeCapacity <= std::numeric_limits<uint8_t>::max(), "pointer shape depth must fit the level counter");

enum class CanonicalPointeeKind : uint8_t {
	None,
	Function,
	MemberPointer,
};

// A by-value query result: 10 bytes, so it stays cheap to return and copy.
struct CanonicalPointerShape {
	uint8_t depth = 0;
	CanonicalPointeeKind pointee = CanonicalPointeeKind::None;
	CVQualifier levels[kCanonicalPointerShapeCapacity] = {};

	bool isPointer() const {
		return depth != 0;
	}
};

inline CanonicalPointerShape canonicalPointerShape(const CanonicalTypeTable& table, TypeId type) {
	CanonicalPointerShape shape;
	type = shapeWithoutReference(table, type);
	while (true) {
		CVQualifier cv = CVQualifier::None;
		CanonicalTypeNode node = table.node(type);
		while (node.kind == CanonicalTypeKind::Qualified) {
			cv |= node.qualifiers;
			type = node.child;
			node = table.node(type);
		}
		if (node.kind != CanonicalTypeKind::Pointer) {
			if (node.kind == CanonicalTypeKind::MemberObjectPointer || node.kind == CanonicalTypeKind::MemberFunctionPointer) {
				shape.pointee = CanonicalPointeeKind::MemberPointer;
			} else if (shape.depth != 0 && node.kind == CanonicalTypeKind::Function) {
				shape.pointee = CanonicalPointeeKind::Function;
			}
			return shape;
		}
		if (shape.depth == kCanonicalPointerShapeCapacity) {
			throw InternalError("canonical pointer shape exceeds its level buffer");
		}
		shape.levels[shape.depth] = cv;
		++shape.depth;
		type = node.child;
	}
}

inline bool canonicalIsPointer(const CanonicalTypeTable& table, TypeId type) {
	return canonicalPointerShape(table, type).isPointer();
}

inline std::optional<TypeId> canonicalPointee(const CanonicalTypeTable& table, TypeId type) {
	const CanonicalTypeNode node = table.node(shapeAfterTopCvAndReference(table, type));
	if (node.kind != CanonicalTypeKind::Pointer) {
		return std::nullopt;
	}
	return node.child;
}

// Complete object size of a canonical type, including array extents, record
// layout, and one pointer width. This is the single size read for consumers
// that must turn a canonical element (or pointee) into a byte stride; it is
// defined by SemanticAnalysis.cpp so this low-level header does not pull in the
// declarator adapter.
std::optional<size_t> tryGetCanonicalObjectSizeBytes(const CanonicalTypeTable& table, TypeId type);

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
	return canonicalPointerShape(table, type).depth;
}

inline void canonicalCopyPointerLevelCvs(const CanonicalTypeTable& table, TypeId type, std::span<CVQualifier> qualifiers) {
	const CanonicalPointerShape shape = canonicalPointerShape(table, type);
	if (qualifiers.size() < shape.depth) {
		throw InternalError("canonical pointer cv span is too small");
	}
	for (size_t index = 0; index < shape.depth; ++index) {
		qualifiers[index] = shape.levels[index];
	}
}

// How much cv the declarator walk below ignores. TopLevel strips only the
// qualification on the outermost type; EveryLevel strips cv on every pointer,
// array element, and member-pointer level.
enum class CanonicalCvScope : uint8_t {
	TopLevel,
	EveryLevel,
};

inline bool canonicalSameIgnoringCvImpl(const CanonicalTypeTable& table, TypeId left, TypeId right, CanonicalCvScope scope) {
	left = shapeWithoutReference(table, left);
	right = shapeWithoutReference(table, right);
	while (true) {
		if (scope == CanonicalCvScope::EveryLevel) {
			left = shapeWithoutTopCv(table, left);
			right = shapeWithoutTopCv(table, right);
		}
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
		if (scope == CanonicalCvScope::EveryLevel) {
			const bool left_derived = left_node.kind == CanonicalTypeKind::Pointer || left_node.kind == CanonicalTypeKind::MemberObjectPointer ||
				left_node.kind == CanonicalTypeKind::MemberFunctionPointer;
			const bool right_derived = right_node.kind == CanonicalTypeKind::Pointer || right_node.kind == CanonicalTypeKind::MemberObjectPointer ||
				right_node.kind == CanonicalTypeKind::MemberFunctionPointer;
			if (left_derived || right_derived) {
				// A member pointer stores its owner TypeId in array_extent, so the
				// owner is part of this component rather than its child.
				if (left_node.kind != right_node.kind || left_node.array_extent != right_node.array_extent) {
					return false;
				}
				left = left_node.child;
				right = right_node.child;
				continue;
			}
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

inline bool canonicalSameIgnoringTopCvRef(const CanonicalTypeTable& table, TypeId left, TypeId right) {
	return canonicalSameIgnoringCvImpl(table, left, right, CanonicalCvScope::TopLevel);
}

// The operand rule of [expr.const.cast]/3: a const_cast target and source must
// be similar object-pointer or pointer-to-data-member types with identical
// pointer/array/member-pointer components, and they may differ in
// cv-qualification at every level. [conv.qual]/2 similarity would also merge a
// known array bound with an unknown one, but /3 requires each Pi to be
// identical, so extents stay exact here. References are not part of a
// qualification decomposition, so they are stripped; that also covers the
// reference form of the cast, which /4 derives from the pointer case.
//
// This is deliberately stronger than identity equality for cv (two types with
// per-level cv differences are similar but not identical) and deliberately
// weaker than the [conv.qual]/3 qualification conversion, which may only add cv
// under the outer-level const propagation rule.
inline bool canonicalSimilarIgnoringCvAndRef(const CanonicalTypeTable& table, TypeId left, TypeId right) {
	return canonicalSameIgnoringCvImpl(table, left, right, CanonicalCvScope::EveryLevel);
}
