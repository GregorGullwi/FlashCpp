#pragma once

#include "CompileError.h"
#include "FrontendIds.h"
#include "StringTable.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

// Context-local publication of template declaration identity for boundary 3A.
// TemplateDeclId is keyed by owner, declaration category, and spelling for the
// currently published template families. The spelling is a lookup key only and
// does not participate in canonical TypeId equality (that uses TemplateDeclId
// plus parameter index).
class TemplateDeclTable {
public:
	TemplateDeclId publishPrimaryClassTemplate(OwnerId owner, StringHandle name) {
		return publishTemplate(TemplateDeclKind::PrimaryClass, owner, name);
	}

	TemplateDeclId publishFunctionTemplate(OwnerId owner, StringHandle name) {
		return publishTemplate(TemplateDeclKind::Function, owner, name);
	}

	std::optional<TemplateDeclId> findPrimaryClassTemplate(OwnerId owner, StringHandle name) const {
		return findTemplate(TemplateDeclKind::PrimaryClass, owner, name);
	}

	std::optional<TemplateDeclId> findFunctionTemplate(OwnerId owner, StringHandle name) const {
		return findTemplate(TemplateDeclKind::Function, owner, name);
	}

	size_t size() const {
		return ids_by_key_.size();
	}

private:
	enum class TemplateDeclKind : uint8_t {
		PrimaryClass,
		Function,
	};

	TemplateDeclId publishTemplate(TemplateDeclKind kind, OwnerId owner, StringHandle name) {
		if (!owner) {
			throw InternalError("template decl: invalid OwnerId");
		}
		if (!name.isValid()) {
			throw InternalError("template decl: invalid template name");
		}
		const Key key{owner.value, kind, name};
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

	std::optional<TemplateDeclId> findTemplate(
		TemplateDeclKind kind,
		OwnerId owner,
		StringHandle name) const {
		if (!owner || !name.isValid()) {
			return std::nullopt;
		}
		const auto existing = ids_by_key_.find(Key{owner.value, kind, name});
		if (existing == ids_by_key_.end()) {
			return std::nullopt;
		}
		return existing->second;
	}

	struct Key {
		uint32_t owner_value = 0;
		TemplateDeclKind kind = TemplateDeclKind::PrimaryClass;
		StringHandle name;

		friend bool operator==(Key lhs, Key rhs) {
			return lhs.owner_value == rhs.owner_value && lhs.kind == rhs.kind && lhs.name == rhs.name;
		}
	};

	struct KeyHash {
		size_t operator()(Key key) const {
			return ((static_cast<size_t>(key.owner_value) * 1315423911u) ^
				(static_cast<size_t>(key.kind) * 2654435761u)) ^ key.name.hash();
		}
	};

	std::unordered_map<Key, TemplateDeclId, KeyHash> ids_by_key_;
};
