#pragma once

#include "CompileError.h"
#include "FrontendIds.h"
#include "StringTable.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

// Context-local publication of primary class- and function-template identity for
// boundary 3A. TemplateDeclId is keyed by OwnerId + spelling + primary kind
// (+ signature index for function primaries) for redeclaration merge; the
// spelling is a lookup key only and does not participate in canonical TypeId
// equality (that uses TemplateDeclId + parameter index). OwnerId may be a
// namespace-mapped owner (namespace/global primaries) or a class-owned owner
// from ownerIdFromClassEntity (member class primaries under published enclosing
// classes). Function primaries use a distinct kind so they cannot share slots
// with class primaries. Distinct free function-template overloads use distinct
// signature indices; matching shapes reuse the same index / TemplateDeclId.
class TemplateDeclTable {
public:
	enum class PrimaryKind : uint8_t {
		Class = 0,
		Function = 1,
	};

	TemplateDeclId publishPrimaryClassTemplate(OwnerId owner, StringHandle name) {
		return publishPrimary(owner, name, PrimaryKind::Class, 0u);
	}

	std::optional<TemplateDeclId> findPrimaryClassTemplate(OwnerId owner, StringHandle name) const {
		return findPrimary(owner, name, PrimaryKind::Class, 0u);
	}

	// signature_index discriminates free function-template overloads under the
	// same OwnerId + name. Callers assign indices by structural signature shape.
	TemplateDeclId publishPrimaryFunctionTemplate(
		OwnerId owner,
		StringHandle name,
		uint32_t signature_index) {
		return publishPrimary(owner, name, PrimaryKind::Function, signature_index);
	}

	std::optional<TemplateDeclId> findPrimaryFunctionTemplate(
		OwnerId owner,
		StringHandle name,
		uint32_t signature_index) const {
		return findPrimary(owner, name, PrimaryKind::Function, signature_index);
	}

	// Next unused signature index for OwnerId + name function primaries.
	uint32_t nextFunctionSignatureIndex(OwnerId owner, StringHandle name) const {
		if (!owner || !name.isValid()) {
			return 0u;
		}
		uint32_t count = 0;
		for (const auto& [key, id] : ids_by_key_) {
			(void)id;
			if (key.owner_value == owner.value &&
				key.name == name &&
				key.kind == PrimaryKind::Function) {
				++count;
			}
		}
		return count;
	}

	size_t size() const {
		return ids_by_key_.size();
	}

private:
	struct Key {
		uint32_t owner_value = 0;
		StringHandle name;
		PrimaryKind kind = PrimaryKind::Class;
		uint32_t signature_index = 0;

		friend bool operator==(Key lhs, Key rhs) {
			return lhs.owner_value == rhs.owner_value &&
				lhs.name == rhs.name &&
				lhs.kind == rhs.kind &&
				lhs.signature_index == rhs.signature_index;
		}
	};

	struct KeyHash {
		size_t operator()(Key key) const {
			return (static_cast<size_t>(key.owner_value) * 1315423911u) ^
				key.name.hash() ^
				(static_cast<size_t>(key.kind) * 2654435761u) ^
				(static_cast<size_t>(key.signature_index) * 40503u);
		}
	};

	TemplateDeclId publishPrimary(
		OwnerId owner,
		StringHandle name,
		PrimaryKind kind,
		uint32_t signature_index) {
		if (!owner) {
			throw InternalError("template decl: invalid OwnerId");
		}
		if (!name.isValid()) {
			throw InternalError("template decl: invalid template name");
		}
		if (kind == PrimaryKind::Class && signature_index != 0u) {
			throw InternalError("template decl: class primary signature index must be 0");
		}
		const Key key{owner.value, name, kind, signature_index};
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

	std::optional<TemplateDeclId> findPrimary(
		OwnerId owner,
		StringHandle name,
		PrimaryKind kind,
		uint32_t signature_index) const {
		if (!owner || !name.isValid()) {
			return std::nullopt;
		}
		const auto existing =
			ids_by_key_.find(Key{owner.value, name, kind, signature_index});
		if (existing == ids_by_key_.end()) {
			return std::nullopt;
		}
		return existing->second;
	}

	std::unordered_map<Key, TemplateDeclId, KeyHash> ids_by_key_;
};
