#pragma once

#include <cstdint>

// Strong semantic identities for architecture boundary 1. Numeric handles only;
// never construct from raw pointers or arena addresses.

struct ScopeId {
	uint32_t value = 0;
	constexpr ScopeId() = default;
	explicit constexpr ScopeId(uint32_t raw_value) : value(raw_value) {}
	ScopeId(const void* pointer_identity) = delete;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(ScopeId, ScopeId) = default;
};

// Stable semantic owner for declaration publication. For namespace-targeted
// declarations this maps to NamespaceRegistry identity (not spelling).
struct OwnerId {
	uint32_t value = 0;
	constexpr OwnerId() = default;
	explicit constexpr OwnerId(uint32_t raw_value) : value(raw_value) {}
	OwnerId(const void* pointer_identity) = delete;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(OwnerId, OwnerId) = default;
};

struct DeclId {
	uint32_t value = 0;
	constexpr DeclId() = default;
	explicit constexpr DeclId(uint32_t raw_value) : value(raw_value) {}
	DeclId(const void* pointer_identity) = delete;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(DeclId, DeclId) = default;
};

struct EntityId {
	uint32_t value = 0;
	constexpr EntityId() = default;
	explicit constexpr EntityId(uint32_t raw_value) : value(raw_value) {}
	EntityId(const void* pointer_identity) = delete;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(EntityId, EntityId) = default;
};

struct ExprId {
	uint32_t value = 0;
	constexpr ExprId() = default;
	explicit constexpr ExprId(uint32_t raw_value) : value(raw_value) {}
	ExprId(const void* pointer_identity) = delete;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(ExprId, ExprId) = default;
};

struct TypeId {
	uint32_t value = 0;
	constexpr TypeId() = default;
	explicit constexpr TypeId(uint32_t raw_value) : value(raw_value) {}
	TypeId(const void* pointer_identity) = delete;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(TypeId, TypeId) = default;
};

// Opaque compatibility identity used by the declaration publication bridge.
// This remains distinct from canonical TypeId until boundary 3A replaces it.
struct TelemetryTypeId {
	uint32_t value = 0;
	constexpr TelemetryTypeId() = default;
	explicit constexpr TelemetryTypeId(uint32_t raw_value) : value(raw_value) {}
	TelemetryTypeId(const void* pointer_identity) = delete;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(TelemetryTypeId, TelemetryTypeId) = default;
};

struct TemplateDeclId {
	uint32_t value = 0;
	constexpr TemplateDeclId() = default;
	explicit constexpr TemplateDeclId(uint32_t raw_value) : value(raw_value) {}
	TemplateDeclId(const void* pointer_identity) = delete;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(TemplateDeclId, TemplateDeclId) = default;
};

// OwnerId carries a compact tagged semantic owner. Namespace owners occupy the
// untagged range; local scopes, class entities, and primary class templates use
// disjoint tags. The payload is a semantic ID, never a spelling or address.
inline constexpr uint32_t kOwnerIdKindMask = 0xC0000000u;
inline constexpr uint32_t kLocalScopeOwnerIdTag = 0x40000000u;
inline constexpr uint32_t kClassOwnerIdTag = 0x80000000u;
inline constexpr uint32_t kTemplateOwnerIdTag = 0xC0000000u;
inline constexpr uint32_t kOwnerIdPayloadMask = 0x3FFFFFFFu;

inline constexpr OwnerId ownerIdFromLocalScope(ScopeId scope_id) {
	if (!scope_id || (scope_id.value & ~kOwnerIdPayloadMask) != 0u) {
		return OwnerId{};
	}
	return OwnerId{scope_id.value | kLocalScopeOwnerIdTag};
}

inline constexpr OwnerId ownerIdFromClassEntity(EntityId enclosing) {
	if (!enclosing || (enclosing.value & ~kOwnerIdPayloadMask) != 0u) {
		return OwnerId{};
	}
	return OwnerId{enclosing.value | kClassOwnerIdTag};
}

inline constexpr OwnerId ownerIdFromTemplateDecl(TemplateDeclId enclosing) {
	if (!enclosing || (enclosing.value & ~kOwnerIdPayloadMask) != 0u) {
		return OwnerId{};
	}
	return OwnerId{enclosing.value | kTemplateOwnerIdTag};
}

inline constexpr bool isClassOwnedOwnerId(OwnerId owner_id) {
	return owner_id && (owner_id.value & kOwnerIdKindMask) == kClassOwnerIdTag;
}

inline constexpr bool isLocalScopeOwnedOwnerId(OwnerId owner_id) {
	return owner_id && (owner_id.value & kOwnerIdKindMask) == kLocalScopeOwnerIdTag;
}

inline constexpr bool isTemplateOwnedOwnerId(OwnerId owner_id) {
	return owner_id && (owner_id.value & kOwnerIdKindMask) == kTemplateOwnerIdTag;
}

inline constexpr bool isClassOrTemplateOwnedOwnerId(OwnerId owner_id) {
	return isClassOwnedOwnerId(owner_id) || isTemplateOwnedOwnerId(owner_id);
}

inline constexpr ScopeId localScopeFromOwnerId(OwnerId owner_id) {
	if (!isLocalScopeOwnedOwnerId(owner_id)) {
		return ScopeId{};
	}
	return ScopeId{owner_id.value & kOwnerIdPayloadMask};
}

inline constexpr EntityId classEntityFromOwnerId(OwnerId owner_id) {
	if (!isClassOwnedOwnerId(owner_id)) {
		return EntityId{};
	}
	return EntityId{owner_id.value & kOwnerIdPayloadMask};
}

inline constexpr TemplateDeclId templateDeclFromOwnerId(OwnerId owner_id) {
	if (!isTemplateOwnedOwnerId(owner_id)) {
		return TemplateDeclId{};
	}
	return TemplateDeclId{owner_id.value & kOwnerIdPayloadMask};
}

static_assert(sizeof(ScopeId) == 4);
static_assert(sizeof(OwnerId) == 4);
static_assert(sizeof(DeclId) == 4);
static_assert(sizeof(EntityId) == 4);
static_assert(sizeof(ExprId) == 4);
static_assert(sizeof(TypeId) == 4);
static_assert(sizeof(TelemetryTypeId) == 4);
static_assert(sizeof(TemplateDeclId) == 4);
static_assert(isLocalScopeOwnedOwnerId(ownerIdFromLocalScope(ScopeId{1})));
static_assert(isClassOwnedOwnerId(ownerIdFromClassEntity(EntityId{1})));
static_assert(isTemplateOwnedOwnerId(ownerIdFromTemplateDecl(TemplateDeclId{1})));
static_assert(isClassOrTemplateOwnedOwnerId(ownerIdFromTemplateDecl(TemplateDeclId{1})));
static_assert(localScopeFromOwnerId(ownerIdFromLocalScope(ScopeId{7})) == ScopeId{7});
static_assert(classEntityFromOwnerId(ownerIdFromClassEntity(EntityId{7})) == EntityId{7});
static_assert(templateDeclFromOwnerId(ownerIdFromTemplateDecl(TemplateDeclId{7})) == TemplateDeclId{7});
static_assert(ownerIdFromClassEntity(EntityId{1}) != ownerIdFromTemplateDecl(TemplateDeclId{1}));
