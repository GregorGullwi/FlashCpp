#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "AstNodeTypes_DeclNodes.h"

namespace AggregateElements {

struct Element {
	TypeIndex type_index;
	bool is_array = false;
	std::span<const size_t> dimensions;
	const StructTypeInfo* struct_info = nullptr;
	bool is_aggregate = false;
	bool has_elements = false;
	bool is_root_element = false;
	std::optional<size_t> root_base_index;
	std::optional<size_t> root_member_index;
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

class ClauseCursor {
public:
	explicit ClauseCursor(const StructTypeInfo& root_struct_info) {
		frames_.push_back(Frame{
			FrameKind::Struct,
			&root_struct_info,
			TypeIndex{},
			{},
			0,
			0,
			false,
			std::nullopt,
			std::nullopt});
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
				FrameKind::Array,
				nullptr,
				element->type_index,
				element->dimensions,
				0,
				element->dimensions.empty() ? 0 : element->dimensions[0],
				true,
				element->root_base_index,
				element->root_member_index});
			return;
		}
		if (element->is_aggregate && element->struct_info != nullptr) {
			frames_.push_back(Frame{
				FrameKind::Struct,
				element->struct_info,
				TypeIndex{},
				{},
				0,
				0,
				true,
				element->root_base_index,
				element->root_member_index});
		}
	}

private:
	enum class FrameKind : uint8_t {
		Struct,
		Array,
	};

	struct Frame {
		FrameKind kind;
		const StructTypeInfo* struct_info;
		TypeIndex element_type;
		std::span<const size_t> dimensions;
		size_t next_index;
		size_t extent;
		bool advances_parent;
		std::optional<size_t> root_base_index;
		std::optional<size_t> root_member_index;
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
		element.root_base_index = frame.root_base_index;
		element.root_member_index = frame.root_member_index;
		if (frame.next_index < base_count) {
			const BaseClassSpecifier& base = frame.struct_info->base_classes[frame.next_index];
			element.type_index = base.type_index;
			element.struct_info = tryGetStructTypeInfo(base.type_index);
			element.is_aggregate = element.struct_info != nullptr && element.struct_info->isAggregate();
			element.has_elements = element.is_aggregate && hasAggregateElements(*element.struct_info);
			if (!element.root_base_index.has_value()) {
				element.root_base_index = frame.next_index;
			}
			return element;
		}

		const StructMember& member = frame.struct_info->members[frame.next_index - base_count];
		element.type_index = member.type_index;
		element.is_array = member.is_array;
		element.dimensions = member.array_dimensions;
		const bool embeds_object = !member.is_array && member.pointer_depth == 0 && !member.is_reference();
		element.struct_info = (member.is_array || embeds_object) ? tryGetStructTypeInfo(member.type_index) : nullptr;
		element.is_aggregate = member.is_array || (element.struct_info != nullptr && element.struct_info->isAggregate());
		if (member.is_array) {
			element.has_elements = countArrayElements(member.array_dimensions) > 0;
		} else {
			element.has_elements = element.is_aggregate && hasAggregateElements(*element.struct_info);
		}
		if (element.is_root_element) {
			element.root_member_index = frame.next_index - base_count;
		}
		return element;
	}

	Element currentArrayElement(const Frame& frame) const {
		Element element;
		element.type_index = frame.element_type;
		element.root_base_index = frame.root_base_index;
		element.root_member_index = frame.root_member_index;
		if (frame.dimensions.size() <= 1) {
			element.struct_info = tryGetStructTypeInfo(frame.element_type);
			element.is_aggregate = element.struct_info != nullptr && element.struct_info->isAggregate();
			element.has_elements = element.is_aggregate && hasAggregateElements(*element.struct_info);
			return element;
		}

		element.is_array = true;
		element.dimensions = frame.dimensions.subspan(1);
		element.struct_info = tryGetStructTypeInfo(frame.element_type);
		element.is_aggregate = true;
		element.has_elements = true;
		return element;
	}

	std::vector<Frame> frames_;
};

} // namespace AggregateElements
