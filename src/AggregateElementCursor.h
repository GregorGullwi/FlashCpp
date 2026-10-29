#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "AstNodeTypes_DeclNodes.h"

namespace AggregateElements {

enum class RootElementKind : uint8_t {
	Base,
	Member,
};

struct RootElementIdentity {
	RootElementKind kind;
	size_t index;
};

struct Element {
	TypeSpecifierNode declared_type;
	std::span<const size_t> dimensions;
	const StructTypeInfo* struct_info = nullptr;
	std::optional<RootElementIdentity> root_element;
	TypeIndex type_index;
	bool is_array = false;
	bool is_aggregate = false;
	bool has_elements = false;
	bool is_root_element = false;
};

inline bool isAggregateElement(const StructMember& member) {
	return !member.isUnnamedOrZeroWidthBitfield();
}

inline bool hasAggregateElements(const StructTypeInfo& struct_info) {
	if (!struct_info.base_classes.empty()) {
		return true;
	}
	for (const StructMember& member : struct_info.members) {
		if (isAggregateElement(member)) {
			return true;
		}
	}
	return false;
}

inline size_t countArrayElements(std::span<const size_t> dimensions) {
	size_t element_count = 1;
	for (const size_t dimension : dimensions) {
		element_count *= dimension;
	}
	return element_count;
}

inline bool isArrayObjectType(const TypeSpecifierNode& type) {
	if (type.has_ordered_declarator() && !type.declarator_components().empty()) {
		const DeclaratorComponentKind outer_kind = type.declarator_components().front().kind;
		return outer_kind == DeclaratorComponentKind::Array || outer_kind == DeclaratorComponentKind::UnknownBoundArray;
	}
	return type.is_array();
}

inline TypeSpecifierNode arrayElementType(const TypeSpecifierNode& type) {
	TypeSpecifierNode element_type = type;
	if (element_type.has_ordered_declarator() && !element_type.declarator_components().empty()) {
		const DeclaratorComponentKind outer_kind = element_type.declarator_components().front().kind;
		if (outer_kind == DeclaratorComponentKind::Array || outer_kind == DeclaratorComponentKind::UnknownBoundArray) {
			element_type.remove_outermost_ordered_declarator_component();
			return element_type;
		}
	}
	if (element_type.is_array() && !element_type.array_dimensions().empty()) {
		if (element_type.array_dimensions().size() > 1) {
			std::vector<size_t> remaining_dimensions(
				element_type.array_dimensions().begin() + 1,
				element_type.array_dimensions().end());
			element_type.set_array_dimensions(remaining_dimensions);
		} else {
			element_type.set_array(false);
		}
	}
	return element_type;
}

inline TypeSpecifierNode makeTypeSpecifier(const Element& element) {
	return element.declared_type;
}

class ClauseCursor {
public:
	explicit ClauseCursor(const StructTypeInfo& root_struct_info) {
		frames_.push_back(Frame{
			TypeSpecifierNode{},
			&root_struct_info,
			std::nullopt,
			0,
			0,
			false,
			FrameKind::Struct});
	}

	std::optional<Element> current() {
		normalize();
		if (frames_.empty()) {
			return std::nullopt;
		}
		return frames_.back().kind == FrameKind::Struct ? currentStructElement(frames_.back()) : currentArrayElement(frames_.back());
	}

	void consumeCurrent() {
		normalize();
		if (!frames_.empty()) {
			++frames_.back().next_index;
		}
	}

	void descendCurrent() {
		const std::optional<Element> element = current();
		if (!element.has_value()) {
			return;
		}
		if (element->is_array) {
			frames_.push_back(Frame{
				element->declared_type,
				nullptr,
				element->root_element,
				0,
				element->dimensions.empty() ? 0 : element->dimensions[0],
				true,
				FrameKind::Array});
			return;
		}
		if (element->is_aggregate && element->struct_info != nullptr) {
			frames_.push_back(Frame{
				TypeSpecifierNode{},
				element->struct_info,
				element->root_element,
				0,
				0,
				true,
				FrameKind::Struct});
		}
	}

private:
	enum class FrameKind : uint8_t {
		Struct,
		Array,
	};

	struct Frame {
		TypeSpecifierNode array_declared_type;
		const StructTypeInfo* struct_info;
		std::optional<RootElementIdentity> root_element;
		size_t next_index;
		size_t extent;
		bool advances_parent;
		FrameKind kind;
	};

	void normalize() {
		while (!frames_.empty()) {
			Frame& frame = frames_.back();
			if (frame.kind == FrameKind::Struct) {
				const size_t base_count = frame.struct_info->base_classes.size();
				const size_t element_end = base_count + frame.struct_info->members.size();
				while (frame.next_index < element_end && frame.next_index >= base_count &&
					   !isAggregateElement(frame.struct_info->members[frame.next_index - base_count])) {
					++frame.next_index;
				}
				if (frame.next_index < element_end) {
					return;
				}
			} else if (frame.next_index < frame.extent) {
				return;
			}

			const bool advances_parent = frame.advances_parent;
			frames_.pop_back();
			if (advances_parent && !frames_.empty()) {
				++frames_.back().next_index;
			}
		}
	}

	Element currentStructElement(const Frame& frame) const {
		const size_t base_count = frame.struct_info->base_classes.size();
		Element element;
		element.is_root_element = !frame.advances_parent;
		element.root_element = frame.root_element;
		if (frame.next_index < base_count) {
			const BaseClassSpecifier& base = frame.struct_info->base_classes[frame.next_index];
			element.type_index = base.type_index;
			element.struct_info = tryGetStructTypeInfo(base.type_index);
			const int base_size_in_bits = element.struct_info != nullptr
				? static_cast<int>(element.struct_info->sizeInBits().value)
				: get_type_size_bits(base.type_index.category());
			element.declared_type = TypeSpecifierNode(base.type_index, base_size_in_bits, Token(), CVQualifier::None, ReferenceQualifier::None);
			element.is_aggregate = element.struct_info != nullptr && element.struct_info->isAggregate();
			element.has_elements = element.is_aggregate && hasAggregateElements(*element.struct_info);
			if (element.is_root_element && !element.root_element.has_value()) {
				element.root_element = RootElementIdentity{RootElementKind::Base, frame.next_index};
			}
			return element;
		}

		const StructMember& member = frame.struct_info->members[frame.next_index - base_count];
		element.declared_type = typeSpecifierFromStructMemberProjection(member);
		element.type_index = element.declared_type.type_index();
		const bool reference = element.declared_type.is_reference();
		element.is_array = !reference && (member.is_array || isArrayObjectType(element.declared_type));
		element.dimensions = element.declared_type.array_dimensions();
		const bool pointer_like = element.declared_type.pointer_depth() > 0 || reference;
		const bool embeds_object = !element.is_array && !pointer_like;
		element.struct_info = (element.is_array || embeds_object) ? tryGetStructTypeInfo(element.type_index) : nullptr;
		element.is_aggregate = element.is_array || (!pointer_like && element.struct_info != nullptr && element.struct_info->isAggregate());
		if (element.is_array) {
			element.has_elements = !element.dimensions.empty() ? countArrayElements(element.dimensions) > 0 : true;
		} else {
			element.has_elements = element.is_aggregate && hasAggregateElements(*element.struct_info);
		}
		if (element.is_root_element && !element.root_element.has_value()) {
			element.root_element = RootElementIdentity{RootElementKind::Member, frame.next_index - base_count};
		}
		return element;
	}

	Element currentArrayElement(const Frame& frame) const {
		Element element;
		element.declared_type = arrayElementType(frame.array_declared_type);
		element.type_index = element.declared_type.type_index();
		element.is_array = isArrayObjectType(element.declared_type);
		element.dimensions = element.declared_type.array_dimensions();
		element.root_element = frame.root_element;
		const bool pointer_like = element.declared_type.pointer_depth() > 0 || element.declared_type.is_reference();
		element.struct_info = (pointer_like || element.is_array) ? nullptr : tryGetStructTypeInfo(element.type_index);
		element.is_aggregate = element.is_array || (!pointer_like && element.struct_info != nullptr && element.struct_info->isAggregate());
		element.has_elements = element.is_array ? true : (element.is_aggregate && hasAggregateElements(*element.struct_info));
		return element;
	}

	std::vector<Frame> frames_;
};

} // namespace AggregateElements
