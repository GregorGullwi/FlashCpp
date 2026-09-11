#pragma once

#include "CompileError.h"
#include "FrontendIds.h"
#include "StringTable.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

// Context-local publication of primary class- and function-template identity for
// boundary 3A. TemplateDeclId is keyed by OwnerId + spelling + primary kind for
// redeclaration merge; the spelling is a lookup key only and does not
// participate in canonical TypeId equality (that uses TemplateDeclId +
// parameter index). OwnerId may be a namespace-mapped owner (namespace/global
// primaries) or a class-owned owner from ownerIdFromClassEntity (member class
// primaries under published enclosing classes). Function primaries use a
// distinct kind so they cannot share slots with class primaries. Overloaded
// free function templates are fail-closed: blocked and unpublished rather than
// silently merged under OwnerId+name alone.
class TemplateDeclTable {
public:
	enum class PrimaryKind : uint8_t {
		Class = 0,
		Function = 1,
	};

	TemplateDeclId publishPrimaryClassTemplate(OwnerId owner, StringHandle name) {
		return publishPrimary(owner, name, PrimaryKind::Class);
	}

	std::optional<TemplateDeclId> findPrimaryClassTemplate(OwnerId owner, StringHandle name) const {
		return findPrimary(owner, name, PrimaryKind::Class);
	}

	// Non-overloaded free function templates only. Returns nullopt when the
	// (owner, name) pair is blocked as overloaded.
	std::optional<TemplateDeclId> tryPublishPrimaryFunctionTemplate(OwnerId owner, StringHandle name) {
		if (!owner) {
			throw InternalError("template decl: invalid OwnerId");
		}
		if (!name.isValid()) {
			throw InternalError("template decl: invalid template name");
		}
		const Key key{owner.value, name, PrimaryKind::Function};
		if (blocked_function_keys_.contains(key)) {
			return std::nullopt;
		}
		const auto existing = ids_by_key_.find(key);
		if (existing != ids_by_key_.end()) {
			return existing->second;
		}
		const uint32_t raw = static_cast<uint32_t>(ids_by_key_.size() + 1u);
		if (raw == 0) {
			throw InternalError("template decl: TemplateDeclId overflow");
		}
		const TemplateDeclId id{raw};
		ids_by_key_.emplace(key, id);
		return id;
	}

	std::optional<TemplateDeclId> findPrimaryFunctionTemplate(OwnerId owner, StringHandle name) const {
		return findPrimary(owner, name, PrimaryKind::Function);
	}

	// Forget any published function TemplateDeclId for this key and refuse
	// further OwnerId+name merges until a signature-aware key exists.
	void noteOverloadedPrimaryFunctionTemplate(OwnerId owner, StringHandle name) {
		if (!owner || !name.isValid()) {
			return;
		}
		const Key key{owner.value, name, PrimaryKind::Function};
		ids_by_key_.erase(key);
		blocked_function_keys_.insert(key);
	}

	bool isPrimaryFunctionTemplateBlocked(OwnerId owner, StringHandle name) const {
		if (!owner || !name.isValid()) {
			return false;
		}
		return blocked_function_keys_.contains(Key{owner.value, name, PrimaryKind::Function});
	}

	size_t size() const {
		return ids_by_key_.size();
	}

private:
	struct Key {
		uint32_t owner_value = 0;
		StringHandle name;
		PrimaryKind kind = PrimaryKind::Class;

		friend bool operator==(Key lhs, Key rhs) {
			return lhs.owner_value == rhs.owner_value && lhs.name == rhs.name && lhs.kind == rhs.kind;
		}
	};

	struct KeyHash {
		size_t operator()(Key key) const {
			return (static_cast<size_t>(key.owner_value) * 1315423911u) ^
				key.name.hash() ^
				(static_cast<size_t>(key.kind) * 2654435761u);
		}
	};

	TemplateDeclId publishPrimary(OwnerId owner, StringHandle name, PrimaryKind kind) {
		if (!owner) {
			throw InternalError("template decl: invalid OwnerId");
		}
		if (!name.isValid()) {
			throw InternalError("template decl: invalid template name");
		}
		const Key key{owner.value, name, kind};
		const auto existing = ids_by_key_.find(key);
		if (existing != ids_by_key_.end()) {
			return existing->second;
		}
		const uint32_t raw = static_cast<uint32_t>(ids_by_key_.size() + 1u);
		if (raw == 0) {
			throw InternalError("template decl: TemplateDeclId overflow");
		}
		const TemplateDeclId id{raw};
		ids_by_key_.emplace(key, id);
		return id;
	}

	std::optional<TemplateDeclId> findPrimary(OwnerId owner, StringHandle name, PrimaryKind kind) const {
		if (!owner || !name.isValid()) {
			return std::nullopt;
		}
		const auto existing = ids_by_key_.find(Key{owner.value, name, kind});
		if (existing == ids_by_key_.end()) {
			return std::nullopt;
		}
		return existing->second;
	}

	std::unordered_map<Key, TemplateDeclId, KeyHash> ids_by_key_;
	std::unordered_set<Key, KeyHash> blocked_function_keys_;
};
