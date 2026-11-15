#pragma once

#include <cstdint>

#include "AstNodeTypes_DeclNodes.h"
#include "InlineVector.h"
#include "StructInfoSet.h"

enum class StructWalkAction : uint8_t {
	Continue,
	Stop,
	SkipSubtree,
};

enum class StructWalkOutcome : uint8_t {
	Completed,
	Stopped,
	InvalidChild,
};

enum class StructWalkFlags : uint16_t {
	None = 0,
	FollowMembers = 1u << 0,
	SkipInvalidChildren = 1u << 1,
	ManualChildEdges = 1u << 2,
};

constexpr StructWalkFlags operator|(StructWalkFlags lhs, StructWalkFlags rhs) {
	return static_cast<StructWalkFlags>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
}

constexpr bool hasStructWalkFlag(StructWalkFlags flags, StructWalkFlags flag) {
	return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

struct StructWalkOptions {
	StructWalkFlags flags;
};

inline const StructTypeInfo* structInfoFromWalkTypeIndex(TypeIndex type_index) {
	const TypeInfo* type_info = tryGetTypeInfo(type_index);
	return type_info == nullptr ? nullptr : type_info->getStructInfo();
}

inline bool structHasDeferredBase(const StructTypeInfo& info) {
	for (const BaseClassSpecifier& base : info.base_classes) {
		if (base.is_deferred) {
			return true;
		}
	}
	return false;
}

// The visited set keys on resolved StructTypeInfo* identity.
class StructWalkQueue {
public:
	StructWalkQueue(TemplateVector<const StructTypeInfo*, 8>& pending, StructWalkFlags flags)
		: pending_(pending), flags_(flags) {}

	bool enqueue(const StructTypeInfo* info) {
		if (info == nullptr) {
			if (hasStructWalkFlag(flags_, StructWalkFlags::SkipInvalidChildren)) {
				return true;
			}
			invalid_child_ = true;
			return false;
		}
		pending_.push_back(info);
		return true;
	}

	bool enqueue(TypeIndex type_index) {
		return enqueue(structInfoFromWalkTypeIndex(type_index));
	}

	bool invalid() const {
		return invalid_child_;
	}

private:
	TemplateVector<const StructTypeInfo*, 8>& pending_;
	StructWalkFlags flags_;
	bool invalid_child_ = false;
};

template <typename Visitor>
StructWalkOutcome walkStructSubobjects(const StructTypeInfo* root, StructWalkOptions options, Visitor&& visitor) {
	if (root == nullptr) {
		return StructWalkOutcome::InvalidChild;
	}
	TemplateVector<const StructTypeInfo*, 8> pending;
	pending.push_back(root);
	StructInfoSet visited;
	StructWalkQueue queue(pending, options.flags);
	while (!pending.empty()) {
		const StructTypeInfo* current = pending.back();
		pending.pop_back();
		if (current == nullptr) {
			if (hasStructWalkFlag(options.flags, StructWalkFlags::SkipInvalidChildren)) {
				continue;
			}
			return StructWalkOutcome::InvalidChild;
		}
		if (!visited.insert(current)) {
			continue;
		}
		const StructWalkAction action = visitor(*current, queue);
		if (queue.invalid()) {
			return StructWalkOutcome::InvalidChild;
		}
		if (action == StructWalkAction::Stop) {
			return StructWalkOutcome::Stopped;
		}
		if (action == StructWalkAction::SkipSubtree ||
			hasStructWalkFlag(options.flags, StructWalkFlags::ManualChildEdges)) {
			continue;
		}
		for (const BaseClassSpecifier& base : current->base_classes) {
			if (base.is_deferred) {
				continue;
			}
			queue.enqueue(base.type_index);
			if (queue.invalid()) {
				return StructWalkOutcome::InvalidChild;
			}
		}
		if (hasStructWalkFlag(options.flags, StructWalkFlags::FollowMembers)) {
			for (const StructMember& member : current->members) {
				if (member.pointer_depth > 0 || member.is_reference() || !is_struct_type(member.type_index.category())) {
					continue;
				}
				queue.enqueue(member.type_index);
				if (queue.invalid()) {
					return StructWalkOutcome::InvalidChild;
				}
			}
		}
	}
	return StructWalkOutcome::Completed;
}
