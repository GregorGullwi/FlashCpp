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

struct TemplateDeclId {
	uint32_t value = 0;
	constexpr TemplateDeclId() = default;
	explicit constexpr TemplateDeclId(uint32_t raw_value) : value(raw_value) {}
	TemplateDeclId(const void* pointer_identity) = delete;
	explicit constexpr operator bool() const { return value != 0; }
	friend constexpr bool operator==(TemplateDeclId, TemplateDeclId) = default;
};

static_assert(sizeof(ScopeId) == 4);
static_assert(sizeof(OwnerId) == 4);
static_assert(sizeof(DeclId) == 4);
static_assert(sizeof(EntityId) == 4);
static_assert(sizeof(ExprId) == 4);
static_assert(sizeof(TypeId) == 4);
static_assert(sizeof(TemplateDeclId) == 4);
