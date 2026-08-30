#pragma once

#include "ChunkedAnyVector.h"
#include "FrontendIds.h"
#include "StringTable.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>

// Front-end declaration/entity publisher for architecture boundary 1.
// Domain for this slice: namespace-targeted free functions with C++ language
// linkage in one translation unit. Opaque TypeId values are caller-supplied
// compatibility keys until canonical types land in boundary 3A.
// signature_id identifies the parameter-type-list for overload identity;
// return_type_id must agree across redeclarations of the same entity.

enum class DeclKind : uint8_t {
	Function = 0,
};

enum class LanguageLinkage : uint8_t {
	CPlusPlus = 0,
};

enum class PublishStatus : uint8_t {
	Created = 0,
	MergedRedeclaration = 1,
	Rejected = 2,
};

struct FunctionDeclRequest {
	ScopeId target_scope_id;
	StringHandle name;
	TypeId signature_id;
	TypeId return_type_id;
	LanguageLinkage language_linkage;
	bool is_definition;
	bool is_inline;
	bool is_constexpr;
};

struct PublishResult {
	PublishStatus status;
	DeclId decl_id;
	EntityId entity_id;
};

struct DeclarationRecord {
	DeclId id;
	EntityId entity_id;
	DeclId previous_decl_id;
	ScopeId scope_id;
	StringHandle name;
	TypeId signature_id;
	TypeId return_type_id;
	uint8_t kind;
	uint8_t language_linkage;
	uint8_t flags;
	uint8_t reserved;
};

struct EntityRecord {
	EntityId id;
	DeclId first_decl_id;
	DeclId latest_decl_id;
	ScopeId scope_id;
	StringHandle name;
	TypeId signature_id;
	TypeId return_type_id;
	uint8_t kind;
	uint8_t language_linkage;
	uint8_t flags;
	uint8_t reserved;
};

static_assert(sizeof(StringHandle) == 4);
static_assert(sizeof(DeclarationRecord) == 32);
static_assert(sizeof(EntityRecord) == 32);
static_assert(std::is_trivially_copyable_v<DeclarationRecord>);
static_assert(std::is_trivially_copyable_v<EntityRecord>);
static_assert(std::is_standard_layout_v<DeclarationRecord>);
static_assert(std::is_standard_layout_v<EntityRecord>);

namespace DeclarationFlags {
inline constexpr uint8_t IsDefinition = 1u << 0;
inline constexpr uint8_t IsInline = 1u << 1;
inline constexpr uint8_t IsConstexpr = 1u << 2;
}

class DeclarationBuilder {
public:
	static constexpr uint32_t kDeclarationArenaChunkSize = 64;
	static constexpr uint32_t kEntityArenaChunkSize = 64;

	DeclarationBuilder() = default;
	~DeclarationBuilder() = default;

	DeclarationBuilder(const DeclarationBuilder&) = delete;
	DeclarationBuilder& operator=(const DeclarationBuilder&) = delete;
	DeclarationBuilder(DeclarationBuilder&&) = delete;
	DeclarationBuilder& operator=(DeclarationBuilder&&) = delete;

	PublishResult publishFunction(const FunctionDeclRequest& request);

	const DeclarationRecord& declaration(DeclId decl_id) const;
	const EntityRecord& entity(EntityId entity_id) const;

	std::size_t declarationCount() const {
		return declarations_.size();
	}

	std::size_t entityCount() const {
		return entities_.size();
	}

private:
	struct EntityLookupKey {
		uint32_t scope = 0;
		uint32_t name_handle = 0;
		uint32_t signature = 0;

		friend bool operator==(const EntityLookupKey&, const EntityLookupKey&) = default;
	};

	struct EntityLookupKeyHash {
		std::size_t operator()(const EntityLookupKey& key) const {
			std::size_t hash = static_cast<std::size_t>(key.scope);
			hash ^= static_cast<std::size_t>(key.name_handle) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
			hash ^= static_cast<std::size_t>(key.signature) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	static PublishResult makeRejected(EntityId existing_entity);
	static uint8_t requestFlags(const FunctionDeclRequest& request);
	static bool hasFlag(uint8_t flags, uint8_t bit);
	bool isValidRequest(const FunctionDeclRequest& request) const;
	DeclId allocateDeclaration(DeclarationRecord record);
	EntityId allocateEntity(EntityRecord record);

	ChunkedVector<DeclarationRecord, kDeclarationArenaChunkSize> declarations_;
	ChunkedVector<EntityRecord, kEntityArenaChunkSize> entities_;
	std::unordered_map<EntityLookupKey, EntityId, EntityLookupKeyHash> entity_by_key_;
};
