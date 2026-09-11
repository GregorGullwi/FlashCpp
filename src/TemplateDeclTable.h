#pragma once

#include "CompileError.h"
#include "FrontendIds.h"
#include "StringTable.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

// Context-local publication of primary class-template identity for boundary 3A.
// TemplateDeclId is keyed by OwnerId + spelling for redeclaration merge; the
// spelling is a lookup key only and does not participate in canonical TypeId
// equality (that uses TemplateDeclId + parameter index). OwnerId may be a
// namespace-mapped owner (namespace/global primaries) or a class-owned owner
// from ownerIdFromClassEntity (member primaries under published enclosing
// classes).
class TemplateDeclTable {
public:
	TemplateDeclId publishPrimaryClassTemplate(OwnerId owner, StringHandle name) {
		if (!owner) {
			throw InternalError("template decl: invalid OwnerId");
		}
		if (!name.isValid()) {
			throw InternalError("template decl: invalid template name");
		}
		const Key key{owner.value, name};
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

	std::optional<TemplateDeclId> findPrimaryClassTemplate(OwnerId owner, StringHandle name) const {
		if (!owner || !name.isValid()) {
			return std::nullopt;
		}
		const auto existing = ids_by_key_.find(Key{owner.value, name});
		if (existing == ids_by_key_.end()) {
			return std::nullopt;
		}
		return existing->second;
	}

	size_t size() const {
		return ids_by_key_.size();
	}

private:
	struct Key {
		uint32_t owner_value = 0;
		StringHandle name;

		friend bool operator==(Key lhs, Key rhs) {
			return lhs.owner_value == rhs.owner_value && lhs.name == rhs.name;
		}
	};

	struct KeyHash {
		size_t operator()(Key key) const {
			return (static_cast<size_t>(key.owner_value) * 1315423911u) ^ key.name.hash();
		}
	};

	std::unordered_map<Key, TemplateDeclId, KeyHash> ids_by_key_;
};
