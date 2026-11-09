#pragma once

#include "ArenaDomains.h"
#include "ChunkedString.h"
#include "CanonicalTypes.h"
#include "CompileError.h"
#include "DeclarationBuilder.h"
#include "DependentExpressionTable.h"
#include "FrontendIds.h"
#include "InlineVector.h"
#include "Log.h"
#include "ScopeRecord.h"
#include "StringTable.h"
#include "SyntaxAstTelemetry.h"
#include "TemplateDeclTable.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

class SymbolTable;
class Parser;
class FrontendContext;

// Type IDs share the semantic pass lifetime of expression SemanticSlots. The
// symbol is an emission key, not declaration identity. Declaring TypeId selects
// the declaration bridge; no reference into the growable record arena escapes.
struct BinaryOperatorCallTarget {
	StringHandle symbol;
	CanonicalTypeId return_type;
	std::array<CanonicalTypeId, 2> parameter_types{};
	// Provisional AST selections and synthesized range comparisons share the
	// same emission bridge. A nonzero declaring type selects the member arm.
	union {
		const StructMemberFunction* member_overload = nullptr;
		const FunctionDeclarationNode* free_function_overload;
	};
	TypeId declaring_type{};
};

static_assert(sizeof(BinaryOperatorCallTarget) == 32);

struct LegacyQualifiedIdentifierRewrite {
	QualifiedIdentifierNode* node = nullptr;
	std::optional<QualifiedIdentifierNode> snapshot;
};

struct LegacyOuterTemplateBindingRewrite {
	void* owner = nullptr;
	TemplateParamNameVector* names = nullptr;
	TemplateArgInfoVector* args = nullptr;
	const TemplateEnvironmentSnapshotNode** snapshot_node = nullptr;
	std::optional<TemplateParamNameVector> names_snapshot;
	std::optional<TemplateArgInfoVector> args_snapshot;
	const TemplateEnvironmentSnapshotNode* snapshot_node_value = nullptr;
};

struct LegacyStructDeclarationRewrite {
	StructDeclarationNode* owner = nullptr;
	std::optional<StructDeclarationNode> snapshot;
};

struct LegacyFunctionDeclarationRewrite {
	FunctionDeclarationNode* owner = nullptr;
	std::optional<FunctionDeclarationJournalSnapshot> snapshot;
};

struct LegacyConstructorDeclarationRewrite {
	ConstructorDeclarationNode* owner = nullptr;
	std::optional<ConstructorDeclarationNode> snapshot;
};

struct LegacyDestructorDeclarationRewrite {
	DestructorDeclarationNode* owner = nullptr;
	std::optional<DestructorDeclarationNode> snapshot;
};

struct LegacyDeclarationNodeRewrite {
	DeclarationNode* owner = nullptr;
	std::optional<DeclarationNode> snapshot;
};

struct LegacyEnumDeclarationRewrite {
	EnumDeclarationNode* owner = nullptr;
	std::optional<EnumDeclarationNode> snapshot;
};

struct LegacyTypedefDeclarationRewrite {
	TypedefDeclarationNode* owner = nullptr;
	ScopeId lexical_scope_id{};
};

struct LegacyTemplateFunctionDeclarationRewrite {
	TemplateFunctionDeclarationNode* owner = nullptr;
	std::optional<TemplateFunctionDeclarationNode> snapshot;
};

struct LegacyTemplateClassDeclarationRewrite {
	TemplateClassDeclarationNode* owner = nullptr;
	std::optional<TemplateClassDeclarationNode> snapshot;
};

struct LegacyTemplateAliasDeclarationRewrite {
	TemplateAliasNode* owner = nullptr;
	std::optional<TemplateAliasNode> snapshot;
};

struct LegacyTemplateVariableDeclarationRewrite {
	TemplateVariableDeclarationNode* owner = nullptr;
	std::optional<TemplateVariableDeclarationNode> snapshot;
};

struct LegacyFriendDeclarationRewrite {
	FriendDeclarationNode* owner = nullptr;
	std::optional<FriendDeclarationNode> snapshot;
};

struct LegacyVariableDeclarationRewrite {
	VariableDeclarationNode* owner = nullptr;
	std::optional<VariableDeclarationNode> snapshot;
};

struct LegacyTypeSpecifierRewrite {
	TypeSpecifierNode* owner = nullptr;
	std::optional<TypeSpecifierNode> snapshot;
};

struct FrontendPublicationJournalMark {
	size_t context_depth = 0;
	size_t namespace_depth = 0;
	size_t legacy_type_registry_depth = 0;
	size_t scope_count = 0;
	size_t legacy_ast_node_count = 0;
	size_t legacy_qualified_identifier_rewrite_count = 0;
	size_t previous_legacy_qualified_identifier_rewrite_segment_start = 0;
	size_t legacy_outer_template_binding_rewrite_count = 0;
	size_t previous_legacy_outer_template_binding_rewrite_segment_start = 0;
	size_t legacy_struct_declaration_rewrite_count = 0;
	size_t previous_legacy_struct_declaration_rewrite_segment_start = 0;
	size_t legacy_function_declaration_rewrite_count = 0;
	size_t previous_legacy_function_declaration_rewrite_segment_start = 0;
	size_t legacy_constructor_declaration_rewrite_count = 0;
	size_t previous_legacy_constructor_declaration_rewrite_segment_start = 0;
	size_t legacy_destructor_declaration_rewrite_count = 0;
	size_t previous_legacy_destructor_declaration_rewrite_segment_start = 0;
	size_t legacy_declaration_node_rewrite_count = 0;
	size_t previous_legacy_declaration_node_rewrite_segment_start = 0;
	size_t legacy_enum_declaration_rewrite_count = 0;
	size_t previous_legacy_enum_declaration_rewrite_segment_start = 0;
	size_t legacy_typedef_declaration_rewrite_count = 0;
	size_t previous_legacy_typedef_declaration_rewrite_segment_start = 0;
	size_t legacy_template_function_declaration_rewrite_count = 0;
	size_t previous_legacy_template_function_declaration_rewrite_segment_start = 0;
	size_t legacy_template_class_declaration_rewrite_count = 0;
	size_t previous_legacy_template_class_declaration_rewrite_segment_start = 0;
	size_t legacy_template_alias_declaration_rewrite_count = 0;
	size_t previous_legacy_template_alias_declaration_rewrite_segment_start = 0;
	size_t legacy_template_variable_declaration_rewrite_count = 0;
	size_t previous_legacy_template_variable_declaration_rewrite_segment_start = 0;
	size_t legacy_friend_declaration_rewrite_count = 0;
	size_t previous_legacy_friend_declaration_rewrite_segment_start = 0;
	size_t legacy_variable_declaration_rewrite_count = 0;
	size_t previous_legacy_variable_declaration_rewrite_segment_start = 0;
	size_t legacy_type_specifier_rewrite_count = 0;
	size_t previous_legacy_type_specifier_rewrite_segment_start = 0;
	Parser& parser_publication_journal;
	size_t parser_publication_handle = SIZE_MAX;
	ScopeId current_scope_id{};
};

FrontendPublicationJournalMark beginFrontendPublicationJournal(FrontendContext& context);
Parser& validateFrontendPublicationJournal(const FrontendContext& context, const FrontendPublicationJournalMark& mark, bool rollback);
void commitFrontendPublicationJournal(FrontendContext& context, FrontendPublicationJournalMark mark);
void rollbackFrontendPublicationJournal(FrontendContext& context, FrontendPublicationJournalMark mark);

// A frontend probe covers scratch storage, declaration publication, template
// identity, candidate diagnostics in both the frontend and Parser engines, legacy type-registry publication, and
// namespace/symbol publication on tables bound to this context, including
// scope creation and cursors.
// Cold type payload and record arenas are journaled with the registry when the
// discarded range is unreachable. Pre-existing TypeInfo rows touched through the
// mutable accessor or mutator API are restored before truncation; a record range
// still referenced by a row that bypassed that API is retained instead of being
// truncated. Legacy AST nodes allocated after the mark are truncated after
// appended Parser roots are removed. A bound parser cursor is saved on begin,
// discarded on commit, and restored on rollback. The parser must outlive the transaction.
// Nested commits remain provisional until the outer probe commits.
class FrontendScratchTransaction {
	friend class FrontendContext;

	FrontendScratchTransaction(FrontendContext& context, DiagnosticEngine& diagnostics, DiagnosticEngine& parser_diagnostics,
		MonotonicScratchArena& arena, ScratchProbeRegistry& registry, DeclarationBuilder& builder, TemplateDeclTable& template_decls)
		: context_(context)
		, diagnostics_(diagnostics)
		, parser_diagnostics_(parser_diagnostics)
		, scratch_(arena, registry)
		, publication_(builder)
		, templates_(template_decls)
		, publication_journal_mark_(beginFrontendPublicationJournal(context)) {}
public:
	~FrontendScratchTransaction() noexcept {
		if (!completed_) {
			rollback();
		}
	}
	FrontendScratchTransaction(const FrontendScratchTransaction&) = delete;
	FrontendScratchTransaction& operator=(const FrontendScratchTransaction&) = delete;
	void commit() {
		if (completed_) {
			return;
		}
		validateCompletion(false);
		publication_.prepareCommit();
		publication_.commit();
		templates_.commit();
		scratch_.commit();
		commitFrontendPublicationJournal(context_, publication_journal_mark_);
		parser_diagnostics_.commit();
		diagnostics_.commit();
		completed_ = true;
	}
	void rollback() {
		if (completed_) {
			return;
		}
		validateCompletion(true);
		rollbackFrontendPublicationJournal(context_, publication_journal_mark_);
		templates_.rollback();
		publication_.rollback();
		scratch_.rollback();
		parser_diagnostics_.rollback();
		diagnostics_.rollback();
		completed_ = true;
	}
	MonotonicScratchArena& arena() { return scratch_.arena(); }
	ScratchProbeRegistry& registry() { return scratch_.registry(); }
private:
	void validateCompletion(bool rollback) const {
		diagnostics_.validateCompletion();
		parser_diagnostics_.validateCompletion();
		publication_.validateCompletion();
		templates_.validateCompletion();
		(void)validateFrontendPublicationJournal(context_, publication_journal_mark_, rollback);
	}

	FrontendContext& context_;
	DiagnosticTransaction diagnostics_;
	DiagnosticTransaction parser_diagnostics_;
	ScratchTransaction scratch_;
	PublicationTransaction publication_;
	TemplateDeclTableTransaction templates_;
	FrontendPublicationJournalMark publication_journal_mark_;
	bool completed_ = false;
};

// Per-translation-unit front-end shell. Active-context lookup uses a thread-local
// stack so concurrent compilations do not share state; each FrontendContext owns
// its own arenas and domain stats.
class FrontendContext {
	friend class SymbolTable;
	friend class Parser;

public:
	// Policy headroom until production probes provide workload measurements.
	static constexpr uint64_t kScratchByteLimit = 64ULL * 1024 * 1024;

	FrontendContext() {
		seedGlobalScopeRecord();
		pushActive(this);
		refreshScratchDomainStats();
	}

	~FrontendContext();

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

	FrontendScratchTransaction beginScratchTransaction();

	// A context has one active Parser; a second instance must wait until it is unbound.
	void bindParserPublicationJournal(Parser& parser);
	void unbindParserPublicationJournal(Parser& parser);

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

	void validatePersistentScopeEnter(ScopeId id, ScopeId parent_id) const {
		if (!id || id.value != static_cast<uint32_t>(scope_records_.size() + 1u)) {
			throw InternalError("FrontendContext: persistent ScopeId diverged from the scope arena");
		}
		if (!parent_id || findScopeRecord(parent_id) == nullptr) {
			throw InternalError("FrontendContext: published scope parent is not in the arena");
		}
	}

	void recordPersistentScopeEnter(ScopeId id, ScopeId parent_id, ScopeType scope_type, uint32_t depth, NamespaceHandle namespace_handle) {
		validatePersistentScopeEnter(id, parent_id);
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
		if (publication_transaction_depth_ != 0) {
			throw InternalError("FrontendContext: reset scopes during publication transaction");
		}
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

	SemanticArenaAccounting& semanticAccounting() {
		return semantic_accounting_;
	}

	BinaryOperatorCallTargetId publishBinaryOperatorCallTarget(const BinaryOperatorCallTarget& target) {
		if (!target.symbol.isValid() || !target.return_type || binary_operator_calls_.size() == UINT32_MAX) {
			throw InternalError("binary operator call target: invalid publication");
		}
		binary_operator_calls_.push_back(target);
		semantic_accounting_.update(SemanticArenaComponent::BinaryOperatorCalls,
			binary_operator_calls_.size() * sizeof(BinaryOperatorCallTarget), binary_operator_calls_.capacity() * sizeof(BinaryOperatorCallTarget));
		return BinaryOperatorCallTargetId{static_cast<uint32_t>(binary_operator_calls_.size())};
	}

	BinaryOperatorCallTarget binaryOperatorCallTarget(BinaryOperatorCallTargetId id) const {
		if (!id || id.value > binary_operator_calls_.size()) {
			throw InternalError("binary operator call target: invalid ID");
		}
		return binary_operator_calls_[id.value - 1];
	}

	DependentExpressionTable& dependentExpressions() {
		return dependent_expressions_;
	}

	const DependentExpressionTable& dependentExpressions() const {
		return dependent_expressions_;
	}

	TemplateDeclTable& templateDecls() {
		return template_decls_;
	}

	const TemplateDeclTable& templateDecls() const {
		return template_decls_;
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
	Parser& requireParserPublicationJournal() const;
	Parser& parserForPublicationMark(const FrontendPublicationJournalMark& mark) const;
	void journalQualifiedIdentifierRewrite(QualifiedIdentifierNode& node);
	friend void journalQualifiedIdentifierRewrite(QualifiedIdentifierNode& node);
	void journalOuterTemplateBindingRewrite(void* owner, TemplateParamNameVector& names, TemplateArgInfoVector& args,
		const TemplateEnvironmentSnapshotNode*& snapshot_node);
	friend void journalOuterTemplateBindingRewrite(void* owner, TemplateParamNameVector& names, TemplateArgInfoVector& args,
		const TemplateEnvironmentSnapshotNode*& snapshot_node);
	void journalOuterTemplateBindingRewrite(void* owner, TemplateParamNameVector& names, TemplateArgInfoVector& args);
	friend void journalOuterTemplateBindingRewrite(void* owner, TemplateParamNameVector& names, TemplateArgInfoVector& args);
	void journalStructDeclarationRewrite(StructDeclarationNode& node);
	friend void journalStructDeclarationRewrite(StructDeclarationNode& node);
	void journalFunctionDeclarationRewrite(FunctionDeclarationNode& node);
	friend void journalFunctionDeclarationRewrite(FunctionDeclarationNode& node);
	void journalConstructorDeclarationRewrite(ConstructorDeclarationNode& node);
	friend void journalConstructorDeclarationRewrite(ConstructorDeclarationNode& node);
	void journalDestructorDeclarationRewrite(DestructorDeclarationNode& node);
	friend void journalDestructorDeclarationRewrite(DestructorDeclarationNode& node);
	void journalDeclarationNodeRewrite(DeclarationNode& node);
	friend void journalDeclarationNodeRewrite(DeclarationNode& node);
	void journalEnumDeclarationRewrite(EnumDeclarationNode& node);
	friend void journalEnumDeclarationRewrite(EnumDeclarationNode& node);
	void journalTypedefDeclarationRewrite(TypedefDeclarationNode& node);
	friend void journalTypedefDeclarationRewrite(TypedefDeclarationNode& node);
	void journalTemplateFunctionDeclarationRewrite(TemplateFunctionDeclarationNode& node);
	friend void journalTemplateFunctionDeclarationRewrite(TemplateFunctionDeclarationNode& node);
	void journalTemplateClassDeclarationRewrite(TemplateClassDeclarationNode& node);
	friend void journalTemplateClassDeclarationRewrite(TemplateClassDeclarationNode& node);
	void journalTemplateAliasDeclarationRewrite(TemplateAliasNode& node);
	friend void journalTemplateAliasDeclarationRewrite(TemplateAliasNode& node);
	void journalTemplateVariableDeclarationRewrite(TemplateVariableDeclarationNode& node);
	friend void journalTemplateVariableDeclarationRewrite(TemplateVariableDeclarationNode& node);
	void journalFriendDeclarationRewrite(FriendDeclarationNode& node);
	friend void journalFriendDeclarationRewrite(FriendDeclarationNode& node);
	void journalVariableDeclarationRewrite(VariableDeclarationNode& node);
	friend void journalVariableDeclarationRewrite(VariableDeclarationNode& node);
	void journalTypeSpecifierRewrite(TypeSpecifierNode& node);
	friend void journalTypeSpecifierRewrite(TypeSpecifierNode& node);
	friend void journalTypeSpecifierRewriteImpl(TypeSpecifierNode& node);
	bool hasOpenPublicationTransaction() const {
		return publication_transaction_depth_ != 0;
	}

	std::array<DomainByteStats, 4> domain_stats_{};
	std::array<uint64_t, static_cast<std::size_t>(SyntaxAstFamily::Count)> syntax_family_counts_{};
	std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> declaration_kind_counts_{};
	std::array<uint64_t, static_cast<std::size_t>(DeclKind::Count)> entity_kind_counts_{};
	// Scratch limit diagnostics are owned here; legacy diagnostics stay in CompileContext.
	DiagnosticEngine diagnostics_;
	MonotonicScratchArena scratch_arena_{diagnostics_, kScratchByteLimit};
	ScratchProbeRegistry scratch_registry_;
	SemanticArenaAccounting semantic_accounting_;
	std::vector<BinaryOperatorCallTarget> binary_operator_calls_;
	CanonicalTypeTable canonical_types_{semantic_accounting_};
	DependentExpressionTable dependent_expressions_;
	TemplateDeclTable template_decls_;
	DeclarationBuilder declaration_builder_{canonical_types_, semantic_accounting_};
	ChunkedVector<ScopeRecord, kScopeArenaChunkSize> scope_records_;
	ScopeId current_scope_id_{1};
	std::vector<SymbolTable*> persistent_scope_publication_tables_;
	std::vector<LegacyQualifiedIdentifierRewrite> legacy_qualified_identifier_rewrites_;
	size_t legacy_qualified_identifier_rewrite_segment_start_ = 0;
	std::vector<LegacyOuterTemplateBindingRewrite> legacy_outer_template_binding_rewrites_;
	size_t legacy_outer_template_binding_rewrite_segment_start_ = 0;
	std::vector<LegacyStructDeclarationRewrite> legacy_struct_declaration_rewrites_;
	size_t legacy_struct_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyFunctionDeclarationRewrite> legacy_function_declaration_rewrites_;
	size_t legacy_function_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyConstructorDeclarationRewrite> legacy_constructor_declaration_rewrites_;
	size_t legacy_constructor_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyDestructorDeclarationRewrite> legacy_destructor_declaration_rewrites_;
	size_t legacy_destructor_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyDeclarationNodeRewrite> legacy_declaration_node_rewrites_;
	size_t legacy_declaration_node_rewrite_segment_start_ = 0;
	std::vector<LegacyEnumDeclarationRewrite> legacy_enum_declaration_rewrites_;
	size_t legacy_enum_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyTypedefDeclarationRewrite> legacy_typedef_declaration_rewrites_;
	size_t legacy_typedef_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyTemplateFunctionDeclarationRewrite> legacy_template_function_declaration_rewrites_;
	size_t legacy_template_function_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyTemplateClassDeclarationRewrite> legacy_template_class_declaration_rewrites_;
	size_t legacy_template_class_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyTemplateAliasDeclarationRewrite> legacy_template_alias_declaration_rewrites_;
	size_t legacy_template_alias_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyTemplateVariableDeclarationRewrite> legacy_template_variable_declaration_rewrites_;
	size_t legacy_template_variable_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyFriendDeclarationRewrite> legacy_friend_declaration_rewrites_;
	size_t legacy_friend_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyVariableDeclarationRewrite> legacy_variable_declaration_rewrites_;
	size_t legacy_variable_declaration_rewrite_segment_start_ = 0;
	std::vector<LegacyTypeSpecifierRewrite> legacy_type_specifier_rewrites_;
	size_t legacy_type_specifier_rewrite_segment_start_ = 0;
	Parser* parser_publication_journal_ = nullptr;
	size_t publication_transaction_depth_ = 0;

	friend FrontendPublicationJournalMark beginFrontendPublicationJournal(FrontendContext& context);
	friend Parser& validateFrontendPublicationJournal(const FrontendContext& context, const FrontendPublicationJournalMark& mark, bool rollback);
	friend void commitFrontendPublicationJournal(
		FrontendContext& context,
		FrontendPublicationJournalMark mark);
	friend void rollbackFrontendPublicationJournal(
		FrontendContext& context,
		FrontendPublicationJournalMark mark);
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
