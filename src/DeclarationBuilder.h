#pragma once

#include "ChunkedAnyVector.h"
#include "FrontendIds.h"
#include "NamespaceRegistry.h"
#include "StringTable.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <vector>

class ASTNode;
class FunctionDeclarationNode;
class PublicationTransaction;
class SymbolTable;
class TypeSpecifierNode;

enum class ScopeType;

// Map NamespaceRegistry identity onto OwnerId. Index 0 (global) becomes OwnerId{1};
// invalid handles remain OwnerId{}. Spelling is never part of this identity.
inline OwnerId ownerIdFromNamespaceHandle(NamespaceHandle handle) {
	if (!handle.isValid()) {
		return OwnerId{};
	}
	return OwnerId{static_cast<uint32_t>(handle.index) + 1u};
}

inline NamespaceHandle namespaceHandleFromOwnerId(OwnerId owner_id) {
	if (!owner_id) {
		return NamespaceHandle{NamespaceHandle::INVALID_HANDLE};
	}
	return NamespaceHandle{static_cast<uint16_t>(owner_id.value - 1u)};
}

// Front-end declaration/entity publisher for architecture boundary 1.
// Domain for this slice: namespace-targeted free functions with C++ language
// linkage in one translation unit. Opaque TypeId values are caller-supplied
// compatibility keys until canonical types land in boundary 3A.
// signature_id identifies the parameter-type-list for overload identity;
// return_type_id must agree across redeclarations of the same entity.
// lexical_scope_id records declaration location; canonical entity identity uses
// OwnerId resolved from SymbolTable publication metadata.

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
	ScopeId lexical_scope_id;
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
	ScopeId lexical_scope_id;
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
	OwnerId owner_id;
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

class PreparedFunctionPublication {
	friend class DeclarationBuilder;

	PreparedFunctionPublication(
		PublishStatus status,
		EntityId entity_id,
		ScopeId lexical_scope_id,
		OwnerId owner_id,
		StringHandle name,
		TypeId signature_id,
		TypeId return_type_id,
		uint8_t flags);

public:
	PreparedFunctionPublication() = delete;
	PreparedFunctionPublication(const PreparedFunctionPublication&) = default;
	PreparedFunctionPublication& operator=(const PreparedFunctionPublication&) = default;

	bool isRejected() const {
		return status_ == PublishStatus::Rejected;
	}

	PublishResult rejection() const {
		return PublishResult{status_, DeclId{}, entity_id_};
	}

private:
	PublishStatus status_ = PublishStatus::Rejected;
	EntityId entity_id_;
	ScopeId lexical_scope_id_;
	OwnerId owner_id_;
	StringHandle name_;
	TypeId signature_id_;
	TypeId return_type_id_;
	uint8_t flags_ = 0;
};

class DeclarationBuilder {
	friend class PublicationTransaction;

public:
	static constexpr uint32_t kDeclarationArenaChunkSize = 64;
	static constexpr uint32_t kEntityArenaChunkSize = 64;

	DeclarationBuilder() = default;
	~DeclarationBuilder() = default;

	DeclarationBuilder(const DeclarationBuilder&) = delete;
	DeclarationBuilder& operator=(const DeclarationBuilder&) = delete;
	DeclarationBuilder(DeclarationBuilder&&) = delete;
	DeclarationBuilder& operator=(DeclarationBuilder&&) = delete;

	PublishResult publishFunction(const FunctionDeclRequest& request, const SymbolTable& symbol_table);

	PreparedFunctionPublication prepareFunctionPublication(
		const FunctionDeclRequest& request,
		const SymbolTable& symbol_table) const;

	PublishResult commitFunctionPublication(
		const PreparedFunctionPublication& prepared,
		PublicationTransaction& transaction);

	// Telemetry-only opaque keys until architecture boundary 3A canonical types.
	// Uses TypeSpecifierNode::matches_signature(), which does not compare nested
	// FunctionSignature payloads; do not use for merge authority.
	TypeId internDeclaratorType(const TypeSpecifierNode& type_spec);
	TypeId internParameterListSignature(
		std::span<const ASTNode> parameter_nodes,
		bool is_variadic,
		PublicationTransaction* transaction);

	const DeclarationRecord& declaration(DeclId decl_id) const;
	const EntityRecord& entity(EntityId entity_id) const;

	std::size_t declarationCount() const {
		return declarations_.size();
	}

	std::size_t entityCount() const {
		return entities_.size();
	}

	std::size_t telemetryDeclaratorInternCount() const {
		return declarator_type_canon_.size();
	}

	std::size_t telemetryParameterListInternCount() const {
		return parameter_list_ids_.size();
	}

private:
	struct EntityLookupKey {
		uint32_t owner = 0;
		uint32_t name_handle = 0;
		uint32_t signature = 0;

		friend bool operator==(const EntityLookupKey&, const EntityLookupKey&) = default;
	};

	struct EntityLookupKeyHash {
		std::size_t operator()(const EntityLookupKey& key) const {
			std::size_t hash = static_cast<std::size_t>(key.owner);
			hash ^= static_cast<std::size_t>(key.name_handle) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
			hash ^= static_cast<std::size_t>(key.signature) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	struct PublicationTarget {
		ScopeId lexical_scope_id;
		OwnerId owner_id;
	};

	struct ParameterListKey {
		std::vector<uint32_t> param_type_ids;
		bool is_variadic = false;

		friend bool operator==(const ParameterListKey&, const ParameterListKey&) = default;
	};

	struct ParameterListKeyHash {
		std::size_t operator()(const ParameterListKey& key) const {
			std::size_t hash = key.is_variadic ? 1u : 0u;
			for (uint32_t type_id : key.param_type_ids) {
				hash ^= static_cast<std::size_t>(type_id) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
			}
			return hash;
		}
	};

	static PublishResult makeRejected(EntityId existing_entity);
	static uint8_t requestFlags(const FunctionDeclRequest& request);
	static bool hasFlag(uint8_t flags, uint8_t bit);
	static bool isPublishableScopeType(ScopeType scope_type);
	static std::optional<PublicationTarget> resolvePublicationTarget(
		const SymbolTable& symbol_table,
		ScopeId lexical_scope_id);
	bool isValidRequest(const FunctionDeclRequest& request) const;
	DeclId allocateDeclaration(DeclarationRecord record);
	EntityId allocateEntity(EntityRecord record);

	ChunkedVector<DeclarationRecord, kDeclarationArenaChunkSize> declarations_;
	ChunkedVector<EntityRecord, kEntityArenaChunkSize> entities_;
	std::unordered_map<EntityLookupKey, EntityId, EntityLookupKeyHash> entity_by_key_;
	std::vector<TypeSpecifierNode> declarator_type_canon_;
	std::unordered_map<ParameterListKey, TypeId, ParameterListKeyHash> parameter_list_ids_;
	std::uint32_t active_publication_transactions_ = 0;
};

struct DeclarationBuilderCheckpoint {
	std::size_t declaration_count = 0;
	std::size_t entity_count = 0;
	std::size_t declarator_type_count = 0;
};

struct EntityUndo {
	EntityId id;
	EntityRecord previous;
};

class PublicationTransaction {
	friend class DeclarationBuilder;

public:
	explicit PublicationTransaction(DeclarationBuilder& builder);
	~PublicationTransaction() noexcept;

	PublicationTransaction(const PublicationTransaction&) = delete;
	PublicationTransaction& operator=(const PublicationTransaction&) = delete;

	void commit() noexcept;
	void rollback() noexcept;

private:
	void noteEntityMutation(EntityId entity_id, const EntityRecord& previous);
	void noteEntityLookupInsert(const DeclarationBuilder::EntityLookupKey& key);
	void noteParameterListInsert(const DeclarationBuilder::ParameterListKey& key);

	DeclarationBuilder& builder_;
	DeclarationBuilderCheckpoint checkpoint_;
	std::optional<EntityUndo> entity_undo_;
	std::optional<DeclarationBuilder::EntityLookupKey> inserted_entity_lookup_;
	std::vector<DeclarationBuilder::ParameterListKey> inserted_parameter_lists_;
	bool committed_ = false;
	bool rolled_back_ = false;
	bool registered_ = false;
};

FunctionDeclRequest buildFreeFunctionDeclRequest(
	DeclarationBuilder& builder,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition);

FunctionDeclRequest buildFreeFunctionDeclRequest(
	DeclarationBuilder& builder,
	PublicationTransaction& transaction,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition);

bool shouldPublishParserFreeFunction(const FunctionDeclarationNode& func_decl, ScopeType scope_type);

// Shadow publication for migration telemetry. SymbolTable remains lookup authority.
PublishResult publishParserFreeFunction(
	DeclarationBuilder& builder,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition,
	const SymbolTable& symbol_table);

// Build request, prepare once, and commit through a publication transaction.
// SymbolTable insert must already have succeeded. SymbolTable remains lookup
// authority when prepare rejects after insert.
PublishResult commitParserFreeFunctionPublication(
	DeclarationBuilder& builder,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition,
	const SymbolTable& symbol_table);
