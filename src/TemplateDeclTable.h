#pragma once

#include "CompileError.h"
#include "FrontendIds.h"
#include "StringTable.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

class ASTNode;

// Context-local publication of primary class-, function-, and alias-template
// identity for boundary 3A. TemplateDeclId is keyed by OwnerId + spelling +
// primary kind (+ signature index for function primaries) for redeclaration
// merge; the spelling is a lookup key only and does not participate in
// canonical TypeId equality (that uses TemplateDeclId + parameter index).
// OwnerId may be a namespace-mapped owner (namespace/global primaries), a
// class-owned owner from ownerIdFromClassEntity, or a template-owned owner
// from ownerIdFromTemplateDecl (direct member class primaries under a
// published class template). Function primaries use a distinct kind so they
// cannot share slots with class primaries. Distinct free function-template
// overloads use distinct signature indices; matching shapes reuse the same
// TemplateDeclId.
class TemplateDeclTable {
public:
	enum class PrimaryKind : uint8_t {
		Class = 0,
		Function = 1,
		Alias = 2,
		Variable = 3,
	};

	TemplateDeclId publishPrimaryClassTemplate(OwnerId owner, StringHandle name) {
		return publishPrimary(owner, name, PrimaryKind::Class, 0u);
	}

	std::optional<TemplateDeclId> findPrimaryClassTemplate(OwnerId owner, StringHandle name) const {
		return findPrimary(owner, name, PrimaryKind::Class, 0u);
	}

	// Member alias-template primaries publish under the same OwnerId
	// derivation as member class templates; there are no alias overloads, so
	// the signature index is always 0.
	TemplateDeclId publishPrimaryAliasTemplate(OwnerId owner, StringHandle name) {
		return publishPrimary(owner, name, PrimaryKind::Alias, 0u);
	}

	std::optional<TemplateDeclId> findPrimaryAliasTemplate(OwnerId owner, StringHandle name) const {
		return findPrimary(owner, name, PrimaryKind::Alias, 0u);
	}

	// Member variable-template primaries publish under the same OwnerId
	// derivation as member class templates; there are no variable overloads,
	// so the signature index is always 0.
	TemplateDeclId publishPrimaryVariableTemplate(OwnerId owner, StringHandle name) {
		return publishPrimary(owner, name, PrimaryKind::Variable, 0u);
	}

	std::optional<TemplateDeclId> findPrimaryVariableTemplate(OwnerId owner, StringHandle name) const {
		return findPrimary(owner, name, PrimaryKind::Variable, 0u);
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

	// Anchor the syntax node of a published primary class template under its
	// TemplateDeclId so identity-based resolution (owner EntityId + member
	// name) can yield the instantiable pattern without a spelling lookup.
	// A later attach replaces an earlier one, matching the registry's
	// forward-to-definition replace. Attaching for an unpublished id is an
	// internal error.
	void attachPrimaryClassTemplatePattern(TemplateDeclId id, ASTNode pattern) {
		if (!id || !hasPrimary(id)) {
			throw InternalError("template decl: attach pattern for unpublished TemplateDeclId");
		}
		primary_class_patterns_.insert_or_assign(id.value, pattern);
	}

	std::optional<ASTNode> primaryClassTemplatePattern(TemplateDeclId id) const {
		const auto found = primary_class_patterns_.find(id.value);
		if (found == primary_class_patterns_.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	// Anchor the syntax node of a published primary alias template under its
	// TemplateDeclId so identity-based resolution yields the alias node
	// without a spelling lookup. Same replace and fail-closed rules as the
	// class-pattern anchor.
	void attachPrimaryAliasPattern(TemplateDeclId id, ASTNode pattern) {
		if (!id || !hasPrimary(id)) {
			throw InternalError("template decl: attach alias pattern for unpublished TemplateDeclId");
		}
		primary_alias_patterns_.insert_or_assign(id.value, pattern);
	}

	std::optional<ASTNode> primaryAliasPattern(TemplateDeclId id) const {
		const auto found = primary_alias_patterns_.find(id.value);
		if (found == primary_alias_patterns_.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	// Anchor the syntax node of a published primary variable template under
	// its TemplateDeclId so identity-based resolution yields the variable
	// node without a spelling lookup. Same replace and fail-closed rules as
	// the class-pattern anchor.
	void attachPrimaryVariablePattern(TemplateDeclId id, ASTNode pattern) {
		if (!id || !hasPrimary(id)) {
			throw InternalError("template decl: attach variable pattern for unpublished TemplateDeclId");
		}
		primary_variable_patterns_.insert_or_assign(id.value, pattern);
	}

	std::optional<ASTNode> primaryVariablePattern(TemplateDeclId id) const {
		const auto found = primary_variable_patterns_.find(id.value);
		if (found == primary_variable_patterns_.end()) {
			return std::nullopt;
		}
		return found->second;
	}

	// The legacy instance-name bridge needs an owner-derived disambiguator only
	// when two member class-template primaries share the same simple spelling.
	// Namespace and template-owned primaries retain their existing spelling path.
	bool hasConflictingClassOwnedPrimaryClassTemplate(
		StringHandle name,
		TemplateDeclId primary) const {
		return hasConflictingClassOwnedPrimary(PrimaryKind::Class, name, primary);
	}

	// Same test for member variable-template primaries feeding the
	// variable-template instance-key stem.
	bool hasConflictingClassOwnedPrimaryVariableTemplate(
		StringHandle name,
		TemplateDeclId primary) const {
		return hasConflictingClassOwnedPrimary(PrimaryKind::Variable, name, primary);
	}

private:
	bool hasConflictingClassOwnedPrimary(
		PrimaryKind kind,
		StringHandle name,
		TemplateDeclId primary) const {
		for (const auto& [key, candidate] : ids_by_key_) {
			if (key.name == name &&
				key.kind == kind &&
				isClassOwnedOwnerId(OwnerId{key.owner_value}) &&
				candidate != primary) {
				return true;
			}
		}
		return false;
	}

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
		if (kind != PrimaryKind::Function && signature_index != 0u) {
			throw InternalError("template decl: non-function primary signature index must be 0");
		}
		const Key key{owner.value, name, kind, signature_index};
		const auto existing = ids_by_key_.find(key);
		if (existing != ids_by_key_.end()) {
			return existing->second;
		}
		if (ids_by_key_.size() >= kOwnerIdPayloadMask) {
			throw InternalError("template decl: TemplateDeclId overflow");
		}
		const uint32_t raw = static_cast<uint32_t>(ids_by_key_.size() + 1u);
		const TemplateDeclId id{raw};
		ids_by_key_.emplace(key, id);
		published_ids_.insert(raw);
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

	bool hasPrimary(TemplateDeclId id) const {
		if (!id) {
			return false;
		}
		const auto found = published_ids_.find(id.value);
		return found != published_ids_.end();
	}

	std::unordered_map<Key, TemplateDeclId, KeyHash> ids_by_key_;
	std::unordered_set<uint32_t> published_ids_;
	std::unordered_map<uint32_t, ASTNode> primary_class_patterns_;
	std::unordered_map<uint32_t, ASTNode> primary_alias_patterns_;
	std::unordered_map<uint32_t, ASTNode> primary_variable_patterns_;
};
