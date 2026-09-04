#pragma once

#include "ArenaDomains.h"
#include "ChunkedString.h"
#include "CanonicalTypes.h"
#include "CompileError.h"
#include "DeclarationBuilder.h"
#include "FrontendIds.h"
#include "InlineVector.h"
#include "Log.h"
#include "ScopeRecord.h"
#include "StringTable.h"
#include "SyntaxAstTelemetry.h"

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

class SymbolTable;

// A frontend probe covers both scratch storage and semantic registries. Nested
// publication commits remain provisional until the outer frontend probe commits.
class FrontendScratchTransaction {
public:
	FrontendScratchTransaction(MonotonicScratchArena& arena, ScratchProbeRegistry& registry, DeclarationBuilder& builder)
		: scratch_(arena, registry), publication_(builder) {}
	FrontendScratchTransaction(const FrontendScratchTransaction&) = delete;
	FrontendScratchTransaction& operator=(const FrontendScratchTransaction&) = delete;
	void commit() { publication_.commit(); scratch_.commit(); }
	void rollback() { publication_.rollback(); scratch_.rollback(); }
	MonotonicScratchArena& arena() { return scratch_.arena(); }
	ScratchProbeRegistry& registry() { return scratch_.registry(); }
private:
	ScratchTransaction scratch_;
	PublicationTransaction publication_;
};

// Per-translation-unit front-end shell. Active-context lookup uses a thread-local
// stack so concurrent compilations do not share state; each FrontendContext owns
// its own arenas and domain stats.
class FrontendContext {
	friend class SymbolTable;

public:
	// Policy headroom until production probes provide workload measurements.
	static constexpr uint64_t kScratchByteLimit = 64ULL * 1024 * 1024;

	FrontendContext() {
		seedGlobalScopeRecord();
		pushActive(this);
		refreshScratchDomainStats();
	}

	~FrontendContext() {
		releasePersistentScopePublicationTables();
		popActive(this);
	}

	FrontendContext(const FrontendContext&) = delete;
	FrontendContext& operator=(const FrontendContext&) = delete;
	FrontendContext(FrontendContext&&) = delete;
	FrontendContext& operator=(FrontendContext&&) = delete;

	static FrontendContext* active() {
		std::vector<FrontendContext*>& stack = activeContextStack();
		if (stack.empty()) {
			return nullptr;
		}
		return stack.back();
	}

	FrontendScratchTransaction beginScratchTransaction() {
		return FrontendScratchTransaction(scratch_arena_, scratch_registry_, declaration_builder_);
	}

	DiagnosticEngine& diagnostics() {
		return diagnostics_;
	}

	MonotonicScratchArena& scratchArena() {
		return scratch_arena_;
	}

	const MonotonicScratchArena& scratchArena() const {
		return scratch_arena_;
	}

	ScratchProbeRegistry& scratchRegistry() {
		return scratch_registry_;
	}

	const ScratchProbeRegistry& scratchRegistry() const {
		return scratch_registry_;
	}

	void recordDomainBytes(AllocationDomain domain, uint64_t current_bytes, uint64_t peak_bytes) {
		const std::size_t index = static_cast<std::size_t>(domain);
		domain_stats_[index].current_bytes = current_bytes;
		domain_stats_[index].peak_bytes = peak_bytes;
	}

	void recordIrDomainStats(uint64_t used_bytes, uint64_t reserved_bytes) {
		applyDomainBytes(
			domain_stats_[static_cast<std::size_t>(AllocationDomain::Ir)],
			used_bytes,
			reserved_bytes);
	}

	DomainByteStats domainStats(AllocationDomain domain) const {
		return domain_stats_[static_cast<std::size_t>(domain)];
	}

	const std::array<uint64_t, static_cast<std::size_t>(SyntaxAstFamily::Count)>& syntaxAstFamilyCounts() const {
		return syntax_family_counts_;
	}

	const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)>& declarationKindCounts() const {
		return declaration_kind_counts_;
	}

	const std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)>& entityKindCounts() const {
		return entity_kind_counts_;
	}

	std::size_t inlineVectorSpillCount() const {
		return FlashCpp::inlineVectorSpillCount();
	}

	const std::array<uint64_t, static_cast<std::size_t>(FlashCpp::InlineVectorSpillFamily::Count)>&
	inlineVectorSpillFamilyCounts() const {
		return FlashCpp::inlineVectorSpillFamilyCounts();
	}

	ScopeId currentScopeId() const {
		return current_scope_id_;
	}

	std::size_t scopeCount() const {
		return scope_records_.size();
	}

	std::size_t scopeRecordCount() const {
		return scope_records_.size();
	}

	uint64_t scopeArenaUsedBytes() const {
		return scope_records_.usedBytes();
	}

	uint64_t scopeArenaReservedBytes() const {
		return scope_records_.reservedBytes();
	}

	const ScopeRecord* findScopeRecord(ScopeId scope_id) const {
		if (!scope_id || scope_id.value > scope_records_.size()) {
			return nullptr;
		}
		const ScopeRecord& record = scope_records_[scope_id.value - 1];
		if (record.id != scope_id) {
			return nullptr;
		}
		return &record;
	}

	const ScopeRecord& scopeRecord(ScopeId scope_id) const {
		const ScopeRecord* record = findScopeRecord(scope_id);
		if (record == nullptr) {
			throw InternalError("FrontendContext: ScopeId does not match a persistent scope record");
		}
		return *record;
	}

	void recordPersistentScopeEnter(
		ScopeId id,
		ScopeId parent_id,
		ScopeType scope_type,
		uint32_t depth,
		NamespaceHandle namespace_handle) {
		if (!id || id.value != static_cast<uint32_t>(scope_records_.size() + 1u)) {
			throw InternalError("FrontendContext: persistent ScopeId diverged from the scope arena");
		}
		if (!parent_id || findScopeRecord(parent_id) == nullptr) {
			throw InternalError("FrontendContext: published scope parent is not in the arena");
		}
		ScopeRecord record{};
		record.id = id;
		record.parent_id = parent_id;
		record.depth = depth;
		record.namespace_handle = namespace_handle;
		record.scope_type = scope_type;
		record.reserved = 0;
		scope_records_.push_back(record);
		current_scope_id_ = id;
	}

	void setPersistentScopeCursor(ScopeId current_scope_id) {
		if (findScopeRecord(current_scope_id) == nullptr) {
			throw InternalError("FrontendContext: persistent scope cursor is not in the arena");
		}
		current_scope_id_ = current_scope_id;
	}

	void resetPersistentScopes() {
		while (scope_records_.size() > 1) {
			scope_records_.pop_back();
		}
		if (scope_records_.empty()) {
			seedGlobalScopeRecord();
			return;
		}
		scope_records_[0] = makeGlobalScopeRecord();
		current_scope_id_ = ScopeId{1};
	}

	CanonicalTypeTable& canonicalTypes() {
		return canonical_types_;
	}

	const CanonicalTypeTable& canonicalTypes() const {
		return canonical_types_;
	}

	DeclarationBuilder& declarationBuilder() {
		return declaration_builder_;
	}

	const DeclarationBuilder& declarationBuilder() const {
		return declaration_builder_;
	}

	std::size_t declarationCount() const {
		return declaration_builder_.declarationCount();
	}

	std::size_t entityCount() const {
		return declaration_builder_.entityCount();
	}

	void refreshScratchDomainStats() {
		const std::size_t scratch_index = static_cast<std::size_t>(AllocationDomain::Scratch);
		domain_stats_[scratch_index].current_bytes = scratch_arena_.currentBytes();
		domain_stats_[scratch_index].peak_bytes = scratch_arena_.peakBytes();
		domain_stats_[scratch_index].reserved_bytes = scratch_arena_.reservedBytes();
		domain_stats_[scratch_index].peak_reserved_bytes = scratch_arena_.peakReservedBytes();
	}

	void refreshSemanticDomainStats() {
		domain_stats_[static_cast<std::size_t>(AllocationDomain::Semantic)] = semantic_accounting_.snapshot();
	}

	void refreshSyntaxDomainStats() {
		applyDomainBytes(
			domain_stats_[static_cast<std::size_t>(AllocationDomain::Syntax)],
			gChunkedAnyStorage.usedBytes(),
			gChunkedAnyStorage.reservedBytes());
	}

	void refreshSyntaxAstFamilyCounts() {
		syntax_family_counts_ = countSyntaxAstFamilies(gChunkedAnyStorage);
	}

	void refreshSemanticDeclKindCounts() {
		declaration_kind_counts_ = declaration_builder_.declarationKindCounts();
		entity_kind_counts_ = declaration_builder_.entityKindCounts();
	}

	static uint64_t stringTableEntryCount() {
		return StringTable::getInternedCount();
	}

	static uint64_t stringTableSpellingBytes() {
		return gChunkedStringAllocator.allocatedBytes();
	}

	void printArenaTelemetry() {
		refreshScratchDomainStats();
		refreshSemanticDomainStats();
		refreshSyntaxDomainStats();
		refreshSyntaxAstFamilyCounts();
		refreshSemanticDeclKindCounts();
		FLASH_LOG(General, Info, "Canonical declarator requests: ", declaration_builder_.canonicalDeclaratorRequests());
		FLASH_LOG(General, Info, "Unmigrated declarator requests: ", declaration_builder_.unmigratedDeclaratorRequests());
		const char* domain_names[] = {"syntax", "semantic", "scratch", "ir"};
		FLASH_LOG(General, Info, "\nFrontendContext arena telemetry:");
		for (std::size_t index = 0; index < domain_stats_.size(); ++index) {
			const DomainByteStats& stats = domain_stats_[index];
			FLASH_LOG(General, Info,
					  "  ",
					  domain_names[index],
					  " domain used bytes (current/peak): ",
					  stats.current_bytes,
					  "/",
					  stats.peak_bytes);
			if (stats.reserved_bytes != 0 || stats.peak_reserved_bytes != 0) {
				FLASH_LOG(General, Info,
						  "  ",
						  domain_names[index],
						  " domain reserved bytes (current/peak): ",
						  stats.reserved_bytes,
						  "/",
						  stats.peak_reserved_bytes);
			}
		}
		FLASH_LOG(General, Info,
				  "  scratch discarded bytes: ",
				  scratch_arena_.discardedBytes());
		FLASH_LOG(General, Info,
				  "  scratch allocation byte limit: ",
				  scratch_arena_.byteLimit());
		FLASH_LOG(General, Info,
				  "  scratch registry committed/live: ",
				  scratch_registry_.committedCount(),
				  "/",
				  scratch_registry_.liveCount());
		FLASH_LOG(General, Info,
				  "  string-table entries/spelling bytes: ",
				  stringTableEntryCount(),
				  "/",
				  stringTableSpellingBytes());
		FLASH_LOG(General, Info,
				  "  InlineVector spill events (selected families): ",
				  FlashCpp::inlineVectorSpillCount());
		for (std::size_t index = 0; index < inlineVectorSpillFamilyCounts().size(); ++index) {
			const uint64_t count = inlineVectorSpillFamilyCounts()[index];
			if (count == 0) {
				continue;
			}
			FLASH_LOG(General, Info,
					  "  InlineVector spill family ",
					  FlashCpp::inlineVectorSpillFamilyLabel(
						  static_cast<FlashCpp::InlineVectorSpillFamily>(index)),
					  " events: ",
					  count);
		}
		FLASH_LOG(General, Info,
				  "  persistent scopes (count/current ScopeId): ",
				  scope_records_.size(),
				  "/",
				  current_scope_id_.value);
		FLASH_LOG(General, Info,
				  "  scope arena used/reserved bytes: ",
				  scope_records_.usedBytes(),
				  "/",
				  scope_records_.reservedBytes());
		FLASH_LOG(General, Info,
				  "  declarations/entities: ",
				  declaration_builder_.declarationCount(),
				  "/",
				  declaration_builder_.entityCount());
		FLASH_LOG(General, Info,
				  "  declarator intern entries: ",
				  declaration_builder_.telemetryDeclaratorInternCount());
		FLASH_LOG(General, Info,
				  "  parameter-list intern entries: ",
				  declaration_builder_.telemetryParameterListInternCount());
		for (std::size_t index = 0; index < declaration_kind_counts_.size(); ++index) {
			const uint64_t count = declaration_kind_counts_[index];
			if (count == 0) {
				continue;
			}
			FLASH_LOG(General, Info,
					  "  semantic declaration kind ",
					  declKindLabel(static_cast<DeclKind>(index)),
					  " records: ",
					  count);
		}
		for (std::size_t index = 0; index < entity_kind_counts_.size(); ++index) {
			const uint64_t count = entity_kind_counts_[index];
			if (count == 0) {
				continue;
			}
			FLASH_LOG(General, Info,
					  "  semantic entity kind ",
					  declKindLabel(static_cast<DeclKind>(index)),
					  " records: ",
					  count);
		}
		FLASH_LOG(General, Info,
				  "  declaration arena used/reserved bytes: ",
				  declaration_builder_.declarationArenaUsedBytes(),
				  "/",
				  declaration_builder_.declarationArenaReservedBytes());
		FLASH_LOG(General, Info,
				  "  entity arena used/reserved bytes: ",
				  declaration_builder_.entityArenaUsedBytes(),
				  "/",
				  declaration_builder_.entityArenaReservedBytes());
		FLASH_LOG(General, Info,
				  "  syntax objects: ",
				  gChunkedAnyStorage.size());
		for (std::size_t index = 0; index < syntax_family_counts_.size(); ++index) {
			const uint64_t count = syntax_family_counts_[index];
			if (count == 0) {
				continue;
			}
			FLASH_LOG(General, Info,
					  "  syntax family ",
					  syntaxAstFamilyLabel(static_cast<SyntaxAstFamily>(index)),
					  " objects: ",
					  count);
		}
	}

private:
	static void applyDomainBytes(DomainByteStats& stats, uint64_t used, uint64_t reserved) {
		stats.current_bytes = used;
		if (used > stats.peak_bytes) {
			stats.peak_bytes = used;
		}
		stats.reserved_bytes = reserved;
		if (reserved > stats.peak_reserved_bytes) {
			stats.peak_reserved_bytes = reserved;
		}
	}

	static std::vector<FrontendContext*>& activeContextStack() {
		thread_local std::vector<FrontendContext*> stack;
		return stack;
	}

	static void pushActive(FrontendContext* context) {
		activeContextStack().push_back(context);
	}

	static void popActive(FrontendContext* context) {
		std::vector<FrontendContext*>& stack = activeContextStack();
		if (!stack.empty() && stack.back() == context) {
			stack.pop_back();
		}
	}

	void seedGlobalScopeRecord() {
		if (!scope_records_.empty()) {
			throw InternalError("FrontendContext: scope arena already contains records");
		}
		scope_records_.push_back(makeGlobalScopeRecord());
		current_scope_id_ = ScopeId{1};
	}

	void registerPersistentScopePublicationTable(SymbolTable& table);
	void releasePersistentScopePublicationTables();

	std::array<DomainByteStats, 4> domain_stats_{};
	std::array<uint64_t, static_cast<std::size_t>(SyntaxAstFamily::Count)> syntax_family_counts_{};
	std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> declaration_kind_counts_{};
	std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> entity_kind_counts_{};
	// Scratch limit diagnostics are owned here; legacy diagnostics stay in CompileContext.
	DiagnosticEngine diagnostics_;
	MonotonicScratchArena scratch_arena_{diagnostics_, kScratchByteLimit};
	ScratchProbeRegistry scratch_registry_;
	SemanticArenaAccounting semantic_accounting_;
	CanonicalTypeTable canonical_types_{semantic_accounting_};
	DeclarationBuilder declaration_builder_{canonical_types_, semantic_accounting_};
	ChunkedVector<ScopeRecord, kScopeArenaChunkSize> scope_records_;
	ScopeId current_scope_id_{1};
	std::vector<SymbolTable*> persistent_scope_publication_tables_;
};

static_assert(!std::is_copy_constructible_v<FrontendContext>);
static_assert(!std::is_move_constructible_v<FrontendContext>);

inline FrontendContext* frontendContext() {
	return FrontendContext::active();
}

inline FrontendContext& requireFrontendContext() {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		throw InternalError("operation requires an active FrontendContext");
	}
	return *context;
}
