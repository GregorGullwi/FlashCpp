#pragma once

#include "ChunkedAnyVector.h"
#include "CanonicalTypes.h"
#include "FrontendIds.h"
#include "NamespaceRegistry.h"
#include "ScopeRecord.h"
#include "StringTable.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

class ASTNode;
struct CanonicalTypeImport;
class FunctionDeclarationNode;
class PublicationTransaction;
class SymbolTable;
class TypeSpecifierNode;

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
// linkage in one translation unit. Opaque TelemetryTypeId values are caller-supplied
// compatibility keys until canonical types land in boundary 3A.
// signature_id identifies the parameter-type-list for overload identity;
// return_type_id must agree across redeclarations of the same entity.
// lexical_scope_id records declaration location; canonical entity identity uses
// OwnerId resolved from SymbolTable publication metadata.

enum class DeclKind : uint8_t {
	Function = 0,
	Count,
};

inline std::string_view declKindLabel(DeclKind kind) {
	switch (kind) {
	case DeclKind::Function:
		return "function";
	case DeclKind::Count:
		break;
	}
	return "unknown";
}

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
	TelemetryTypeId signature_id;
	TelemetryTypeId return_type_id;
	LanguageLinkage language_linkage;
	bool is_definition;
	bool is_inline;
	bool is_constexpr;
};

enum class FunctionDeclForm : uint8_t {
	Declaration = 0,
	Definition = 1,
	InlineDeclaration = 2,
	ConstexprDeclaration = 3,
	ConstexprDefinition = 4,
};

inline FunctionDeclRequest makeFunctionDeclRequest(
	ScopeId lexical_scope_id,
	StringHandle name,
	TelemetryTypeId signature_id,
	TelemetryTypeId return_type_id,
	FunctionDeclForm form,
	LanguageLinkage language_linkage) {
	FunctionDeclRequest request{};
	request.lexical_scope_id = lexical_scope_id;
	request.name = name;
	request.signature_id = signature_id;
	request.return_type_id = return_type_id;
	request.language_linkage = language_linkage;
	switch (form) {
	case FunctionDeclForm::Declaration:
		break;
	case FunctionDeclForm::Definition:
		request.is_definition = true;
		break;
	case FunctionDeclForm::InlineDeclaration:
		request.is_inline = true;
		break;
	case FunctionDeclForm::ConstexprDeclaration:
		request.is_constexpr = true;
		break;
	case FunctionDeclForm::ConstexprDefinition:
		request.is_definition = true;
		request.is_constexpr = true;
		break;
	}
	return request;
}

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
	TelemetryTypeId signature_id;
	TelemetryTypeId return_type_id;
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
	TelemetryTypeId signature_id;
	TelemetryTypeId return_type_id;
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
		TelemetryTypeId signature_id,
		TelemetryTypeId return_type_id,
		uint8_t flags);

public:
	PreparedFunctionPublication() = delete;
	PreparedFunctionPublication(const PreparedFunctionPublication&) = delete;
	PreparedFunctionPublication& operator=(const PreparedFunctionPublication&) = delete;
	PreparedFunctionPublication(PreparedFunctionPublication&& other) noexcept;
	PreparedFunctionPublication& operator=(PreparedFunctionPublication&& other) noexcept;

	bool isRejected() const {
		return status_ == PublishStatus::Rejected;
	}

	PublishResult rejection() const {
		return PublishResult{status_, DeclId{}, entity_id_};
	}

private:
	void consume();

	PublishStatus status_ = PublishStatus::Rejected;
	EntityId entity_id_;
	ScopeId lexical_scope_id_;
	OwnerId owner_id_;
	StringHandle name_;
	TelemetryTypeId signature_id_;
	TelemetryTypeId return_type_id_;
	uint8_t flags_ = 0;
	uint8_t consumed_ = 0;
};

class DeclarationBuilder {
	friend class PublicationTransaction;

public:
	static constexpr uint32_t kDeclarationArenaChunkSize = 64;
	static constexpr uint32_t kEntityArenaChunkSize = 64;

	DeclarationBuilder(CanonicalTypeTable& types, SemanticArenaAccounting& accounting);
	~DeclarationBuilder();

	DeclarationBuilder(const DeclarationBuilder&) = delete;
	DeclarationBuilder& operator=(const DeclarationBuilder&) = delete;
	DeclarationBuilder(DeclarationBuilder&&) = delete;
	DeclarationBuilder& operator=(DeclarationBuilder&&) = delete;

	PublishResult publishFunction(const FunctionDeclRequest& request, const SymbolTable& symbol_table);

	PreparedFunctionPublication prepareFunctionPublication(
		const FunctionDeclRequest& request,
		const SymbolTable& symbol_table) const;

	PublishResult commitFunctionPublication(
		PreparedFunctionPublication& prepared,
		PublicationTransaction& transaction);

	// Supported families use canonical equality; the explicitly unmigrated
	// families retain matches_signature until 3A replaces them. Telemetry only.
	TelemetryTypeId internDeclaratorType(const TypeSpecifierNode& type_spec);
	TelemetryTypeId internParameterListSignature(
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

	std::size_t telemetryDeclaratorInternCount() const;
	uint64_t canonicalDeclaratorRequests() const { return canonical_declarator_requests_; }
	uint64_t unmigratedDeclaratorRequests() const { return unmigrated_declarator_requests_; }

	std::size_t telemetryParameterListInternCount() const {
		return parameter_list_ids_.size();
	}

	std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> declarationKindCounts() const;

	std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> entityKindCounts() const;

	uint64_t declarationArenaUsedBytes() const {
		return declarations_.usedBytes();
	}

	uint64_t declarationArenaReservedBytes() const {
		return declarations_.reservedBytes();
	}

	uint64_t entityArenaUsedBytes() const {
		return entities_.usedBytes();
	}

	uint64_t entityArenaReservedBytes() const {
		return entities_.reservedBytes();
	}

	uint64_t peakSemanticArenaUsedBytes() const {
		return peak_used_bytes_;
	}

	uint64_t peakSemanticArenaReservedBytes() const {
		return peak_reserved_bytes_;
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
	TelemetryTypeId internDeclaratorTypeImport(
		const TypeSpecifierNode& type_spec,
		CanonicalTypeImport imported);
	TelemetryTypeId internFunctionParameterType(const TypeSpecifierNode& type_spec);
	void noteSemanticArenaPeaks();

	ChunkedVector<DeclarationRecord, kDeclarationArenaChunkSize> declarations_;
	ChunkedVector<EntityRecord, kEntityArenaChunkSize> entities_;
	std::unordered_map<EntityLookupKey, EntityId, EntityLookupKeyHash> entity_by_key_;
	CanonicalTypeTable& canonical_types_;
	SemanticArenaAccounting& accounting_;
	std::unordered_map<uint32_t, TelemetryTypeId> canonical_declarator_ids_;
	std::vector<uint32_t> canonical_declarator_order_;
	std::vector<TypeSpecifierNode> unmigrated_declarator_types_;
	std::vector<TelemetryTypeId> unmigrated_declarator_ids_;
	uint64_t canonical_declarator_requests_ = 0;
	uint64_t unmigrated_declarator_requests_ = 0;
	PublicationTransaction* active_transaction_ = nullptr;
	std::unordered_map<ParameterListKey, TelemetryTypeId, ParameterListKeyHash> parameter_list_ids_;
	uint64_t peak_used_bytes_ = 0;
	uint64_t peak_reserved_bytes_ = 0;
};

struct DeclarationBuilderCheckpoint {
	std::size_t declaration_count = 0;
	std::size_t entity_count = 0;
	std::size_t declarator_type_count = 0;
	std::size_t canonical_declarator_count = 0;
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

	void commit();
	void rollback() noexcept;

private:
	void noteEntityMutation(EntityId entity_id, const EntityRecord& previous);
	void noteEntityLookupInsert(const DeclarationBuilder::EntityLookupKey& key);
	void noteParameterListInsert(const DeclarationBuilder::ParameterListKey& key);

	DeclarationBuilder& builder_;
	CanonicalTypeTransaction canonical_transaction_;
	PublicationTransaction* parent_;
	DeclarationBuilderCheckpoint checkpoint_;
	std::vector<EntityUndo> entity_undos_;
	std::vector<DeclarationBuilder::EntityLookupKey> inserted_entity_lookups_;
	std::vector<DeclarationBuilder::ParameterListKey> inserted_parameter_lists_;
	bool committed_ = false;
	bool rolled_back_ = false;
};

FunctionDeclRequest buildFreeFunctionDeclRequest(
	DeclarationBuilder& builder,
	PublicationTransaction& transaction,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition);

bool shouldPublishParserFreeFunction(const FunctionDeclarationNode& func_decl, ScopeType scope_type);

// Build request, prepare once, and commit through a publication transaction.
// SymbolTable insert must already have succeeded. SymbolTable remains lookup
// authority when prepare rejects after insert.
PublishResult commitParserFreeFunctionPublication(
	DeclarationBuilder& builder,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition,
	const SymbolTable& symbol_table);
