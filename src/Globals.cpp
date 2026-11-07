// Globals.cpp - Shared global state for FlashCpp compiler
// These globals are needed by both main.cpp and FlashCppTest.cpp.
// Included via FlashCppUnity.h (unity build).

#include "AstNodeTypes.h"
#include "NamespaceRegistry.h"
#include "LazyMemberResolver.h"
#include "InstantiationQueue.h"
#include "TemplateTypes.h"
#include "LegacyChunkedAnyStoragePolicyComplete.h"
#include "FrontendContext.h"
#include "Parser.h"
#include "SymbolTable.h"

#include <algorithm>

const Diagnostic& OwnedDiagnosticSnapshot::checkedDiagnostic(const DiagnosticEngine& engine, uint32_t diagnostic_index) {
	if (diagnostic_index >= engine.diagnostics().size()) throw InternalError("diagnostic snapshot: record index is not published");
	return engine.diagnostic(diagnostic_index);
}

OwnedDiagnosticSnapshot::OwnedDiagnosticSnapshot(const DiagnosticEngine& engine, uint32_t diagnostic_index)
	: diagnostic_(checkedDiagnostic(engine, diagnostic_index)) {
	const auto source_notes = engine.notes();
	if (!diagnostic_.note_indices.empty() && diagnostic_.note_indices.size() - 1 > UINT32_MAX) {
		throw InternalError("diagnostic snapshot: too many notes");
	}
	notes_.reserve(diagnostic_.note_indices.size());
	for (uint32_t index : diagnostic_.note_indices) {
		if (index >= source_notes.size()) throw InternalError("diagnostic snapshot: note index is not published");
		notes_.push_back(source_notes[index]);
	}
	for (size_t index = 0; index < diagnostic_.note_indices.size(); ++index) diagnostic_.note_indices[index] = static_cast<uint32_t>(index);
	captureText();
}

OwnedDiagnosticSnapshot::OwnedDiagnosticSnapshot(const OwnedDiagnosticSnapshot& other)
	: diagnostic_(other.diagnostic_), notes_(other.notes_), text_(other.text_) {
	rebindTextViews();
}

OwnedDiagnosticSnapshot& OwnedDiagnosticSnapshot::operator=(const OwnedDiagnosticSnapshot& other) {
	if (this != &other) {
		OwnedDiagnosticSnapshot copied(other);
		*this = std::move(copied);
	}
	return *this;
}

OwnedDiagnosticSnapshot::OwnedDiagnosticSnapshot(OwnedDiagnosticSnapshot&& other) noexcept
	: diagnostic_(std::move(other.diagnostic_)), notes_(std::move(other.notes_)), text_(std::move(other.text_)) {
	other.clearMovedFrom();
}

OwnedDiagnosticSnapshot& OwnedDiagnosticSnapshot::operator=(OwnedDiagnosticSnapshot&& other) noexcept {
	if (this != &other) {
		diagnostic_ = std::move(other.diagnostic_);
		notes_ = std::move(other.notes_);
		text_ = std::move(other.text_);
		other.clearMovedFrom();
	}
	return *this;
}

void OwnedDiagnosticSnapshot::captureText() {
	size_t byte_count = 1;
	forEachText([&](std::string_view text) {
		if (text.size() > text_.max_size() - byte_count) throw InternalError("diagnostic snapshot: text exceeds owned buffer capacity");
		byte_count += text.size();
	});
	text_.reserve(byte_count);
	forEachText([&](std::string_view text) {
		if (!text.empty()) text_.insert(text_.end(), text.begin(), text.end());
	});
	text_.push_back('\0');
	rebindTextViews();
}

void OwnedDiagnosticSnapshot::rebindTextViews() {
	if (text_.empty()) return;
	size_t offset = 0;
	forEachText([&](std::string_view& text) {
		const size_t size = text.size();
		if (size > text_.size() - 1 - offset) throw InternalError("diagnostic snapshot: invalid owned text layout");
		text = std::string_view(text_.data() + offset, size);
		offset += size;
	});
	if (offset != text_.size() - 1) throw InternalError("diagnostic snapshot: owned text layout has unused bytes");
}

void OwnedDiagnosticSnapshot::clearMovedFrom() noexcept {
	diagnostic_.id = DiagnosticId::None;
	diagnostic_.message_template = {};
	diagnostic_.arguments.clear();
	diagnostic_.note_indices.clear();
	diagnostic_.instantiation_context.clear();
	notes_.clear();
	text_.clear();
}

std::string OwnedDiagnosticSnapshot::render(const std::deque<std::string>& file_paths) const {
	return renderDiagnostic(diagnostic_, notes_, file_paths);
}

CompileError::CompileError(std::string rendered_message, const DiagnosticEngine& engine, uint32_t diagnostic_index, BridgeTag)
	: std::runtime_error(std::move(rendered_message)), structured_(std::in_place, engine, diagnostic_index) {}

CompileError CompileError::fromStructuredDiagnostic(const DiagnosticEngine& engine, uint32_t diagnostic_index) {
	if (diagnostic_index >= engine.diagnostics().size()) throw InternalError("compile error: diagnostic index is not published");
	const Diagnostic& diagnostic = engine.diagnostic(diagnostic_index);
	return CompileError(renderDiagnosticMessage(diagnostic.message_template, diagnostic.arguments), engine, diagnostic_index, BridgeTag{});
}

CompileError& CompileError::operator=(const CompileError& other) {
	if (this != &other) {
		CompileError copied(other);
		*this = std::move(copied);
	}
	return *this;
}

CompileError::CompileError(CompileError&& other) noexcept
	: std::runtime_error(std::move(other)), structured_(std::move(other.structured_)) {
	other.structured_.reset();
}

CompileError& CompileError::operator=(CompileError&& other) noexcept {
	if (this != &other) {
		std::runtime_error::operator=(std::move(other));
		structured_ = std::move(other.structured_);
		other.structured_.reset();
	}
	return *this;
}

std::string CompileError::renderStructuredDiagnostic(const std::deque<std::string>& file_paths) const {
	if (!structured_) throw InternalError("compile error: no structured diagnostic to render");
	return structured_->render(file_paths);
}

// Completion checks live here because Diagnostics.h is below CompileError.h.
DiagnosticTransaction::DiagnosticTransaction(DiagnosticEngine& engine)
	: engine_(&engine), parent_(engine.active_transaction_), diagnostic_count_(engine.diagnostics_.size()),
	  note_undo_count_(engine.note_attachment_undos_.size()) {
	engine.active_transaction_ = this;
}

DiagnosticTransaction::~DiagnosticTransaction() noexcept {
	rollback();
}

void DiagnosticTransaction::validateCompletion() const {
	if (engine_ != nullptr && engine_->active_transaction_ != this) {
		throw InternalError("diagnostics: transactions must close in nesting order");
	}
}

void DiagnosticTransaction::commit() {
	if (engine_ == nullptr) return;
	validateCompletion();
	engine_->active_transaction_ = parent_;
	if (parent_ == nullptr) engine_->note_attachment_undos_.clear();
	engine_ = nullptr;
}

void DiagnosticTransaction::rollback() {
	if (engine_ == nullptr) return;
	validateCompletion();
	for (size_t index = engine_->note_attachment_undos_.size(); index > note_undo_count_; --index) {
		const auto& undo = engine_->note_attachment_undos_[index - 1];
		if (undo.diagnostic_index >= diagnostic_count_) continue;
		auto& notes = engine_->diagnostics_[undo.diagnostic_index].note_indices;
		if (notes.size() < undo.note_count) throw InternalError("diagnostics: note attachment disappeared before rollback");
		notes.resize(undo.note_count);
	}
	engine_->note_attachment_undos_.resize(note_undo_count_);
	while (engine_->diagnostics_.size() > diagnostic_count_) {
		auto& count = engine_->severity_counts_[static_cast<size_t>(engine_->diagnostics_.back().severity)];
		if (count == 0) throw InternalError("diagnostics: severity count is inconsistent before rollback");
		engine_->diagnostics_.pop_back();
		--count;
	}
	engine_->active_transaction_ = parent_;
	engine_ = nullptr;
}

void DiagnosticEngine::recordNoteAttachment(uint32_t diagnostic_index) {
	if (diagnostic_index >= diagnostics_.size()) throw InternalError("diagnostics: note target is not a published diagnostic");
	note_attachment_undos_.push_back({diagnostics_[diagnostic_index].note_indices.size(), diagnostic_index});
}

// Global debug flag
bool g_enable_debug_output = false;

// Global exception handling control
bool g_enable_exceptions = true;

NamespaceRegistry gNamespaceRegistry;
LegacyAstChunkedAnyVector gChunkedAnyStorage;

namespace {

MonotonicScratchArena& persistentArena() {
	static MonotonicScratchArena* arena = new MonotonicScratchArena(*new DiagnosticEngine(), 2ULL * 1024ULL * 1024ULL * 1024ULL);
	return *arena;
}

} // namespace

namespace FlashCpp {

void* allocatePersistentArena(std::size_t size, std::size_t alignment) { return persistentArena().allocate(size, alignment); }
uint64_t persistentArenaUsedBytes() { return persistentArena().currentBytes(); }
uint64_t persistentArenaPeakBytes() { return persistentArena().peakBytes(); }
uint64_t persistentArenaReservedBytes() { return persistentArena().reservedBytes(); }
uint64_t persistentArenaPeakReservedBytes() { return persistentArena().peakReservedBytes(); }

} // namespace FlashCpp

namespace FlashCpp {
ChunkedVector<StructuralClassValue> gStructuralClassValues;
ChunkedVector<NonTypeValueIdentity, 32> gTemplateValueIdentities;
ChunkedVector<FunctionSignature, 64> gTemplateFunctionSignatures;
LazyMemberResolver gLazyMemberResolver;
InstantiationQueue gInstantiationQueue;
} // namespace FlashCpp

// Persistent scope publication lives here so SymbolTable.h does not include
// FrontendContext.h (and so this is not its own modular translation unit).
namespace {

// Namespace and legacy type journals are process-global until registry ownership
// migrates. Reject overlapping owners rather than mixing context-local journals.
FrontendContext* gFrontendPublicationJournalOwner = nullptr;

FrontendContext& requireActiveFrontendContext() {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		throw InternalError("SymbolTable: persistent scope publication requires an active FrontendContext");
	}
	return *context;
}

} // namespace

FrontendContext& requirePersistentScopePublicationContext(const SymbolTable& table) {
	FrontendContext* context = table.persistentScopePublicationContext();
	if (context == nullptr) {
		throw InternalError("SymbolTable: persistent scope publication is missing its bound FrontendContext");
	}
	return *context;
}

FrontendScratchTransaction FrontendContext::beginScratchTransaction() {
	Parser& parser = requireParserPublicationJournal();
	return FrontendScratchTransaction(*this, diagnostics_, parser.diagnostics(), scratch_arena_, scratch_registry_, declaration_builder_,
		template_decls_);
}

FrontendPublicationJournalMark beginFrontendPublicationJournal(FrontendContext& context) {
	if (gFrontendPublicationJournalOwner != nullptr && gFrontendPublicationJournalOwner != &context) {
		throw InternalError("FrontendContext: overlapping publication transactions require the same context");
	}
	Parser& parser_journal = context.requireParserPublicationJournal();
	size_t parser_publication_handle = SIZE_MAX;
	const size_t context_depth = context.publication_transaction_depth_ + 1U;
	size_t namespace_depth = 0;
	LegacyTypeRegistryJournalMark legacy_type_registry_depth = 0;
	size_t opened_tables = 0;
	try {
		parser_publication_handle = parser_journal.saveParserCursor();
		namespace_depth = gNamespaceRegistry.beginPublicationTransaction();
		legacy_type_registry_depth = beginLegacyTypeRegistryJournal();
		for (SymbolTable* table : context.persistent_scope_publication_tables_) {
			const size_t table_depth = table->beginPublicationTransaction();
			if (table_depth != context_depth) {
				table->rollbackPublicationTransaction(table_depth);
				throw InternalError("FrontendContext: symbol publication transaction depth is out of sync");
			}
			++opened_tables;
		}
	} catch (...) {
		while (opened_tables > 0) {
			context.persistent_scope_publication_tables_[--opened_tables]->rollbackPublicationTransaction(context_depth);
		}
		if (legacy_type_registry_depth != 0) {
			rollbackLegacyTypeRegistryJournal(legacy_type_registry_depth);
		}
		if (namespace_depth != 0) {
			gNamespaceRegistry.rollbackPublicationTransaction(namespace_depth);
		}
		if (parser_publication_handle != SIZE_MAX) {
			parser_journal.discardParserCursor(parser_publication_handle);
		}
		throw;
	}

	context.publication_transaction_depth_ = context_depth;
	gFrontendPublicationJournalOwner = &context;
	return FrontendPublicationJournalMark{context_depth, namespace_depth, legacy_type_registry_depth, context.scope_records_.size(),
		gChunkedAnyStorage.size(), parser_journal, parser_publication_handle, context.current_scope_id_};
}

Parser& validateFrontendPublicationJournal(const FrontendContext& context, const FrontendPublicationJournalMark& mark, bool rollback) {
	if (gFrontendPublicationJournalOwner != &context || context.publication_transaction_depth_ != mark.context_depth) {
		throw InternalError("FrontendContext: publication transactions must close in nesting order");
	}
	Parser& parser = context.parserForPublicationMark(mark);
	parser.validateParserPublicationCheckpoint(mark.parser_publication_handle);
	for (const SymbolTable* table : context.persistent_scope_publication_tables_) {
		table->validatePublicationTransactionDepth(mark.context_depth);
	}
	gNamespaceRegistry.validatePublicationTransactionDepth(mark.namespace_depth);
	validateLegacyTypeRegistryJournal(mark.legacy_type_registry_depth);
	if (rollback) {
		parser.validateParserRootPublicationRollback(mark.parser_publication_handle);
	}
	return parser;
}

void commitFrontendPublicationJournal(FrontendContext& context, FrontendPublicationJournalMark mark) {
	Parser& parser = validateFrontendPublicationJournal(context, mark, false);
	for (auto it = context.persistent_scope_publication_tables_.rbegin();
		it != context.persistent_scope_publication_tables_.rend();
		++it) {
		(*it)->commitPublicationTransaction(mark.context_depth);
	}
	gNamespaceRegistry.commitPublicationTransaction(mark.namespace_depth);
	commitLegacyTypeRegistryJournal(mark.legacy_type_registry_depth);
	parser.discardParserCursor(mark.parser_publication_handle);
	--context.publication_transaction_depth_;
	if (context.publication_transaction_depth_ == 0) {
		gFrontendPublicationJournalOwner = nullptr;
	}
}

void rollbackFrontendPublicationJournal(FrontendContext& context, FrontendPublicationJournalMark mark) {
	Parser& parser = validateFrontendPublicationJournal(context, mark, true);
	for (auto it = context.persistent_scope_publication_tables_.rbegin();
		it != context.persistent_scope_publication_tables_.rend();
		++it) {
		(*it)->rollbackPublicationTransaction(mark.context_depth);
	}
	parser.rollbackParserRootPublication(mark.parser_publication_handle);
	while (context.scope_records_.size() > mark.scope_count) context.scope_records_.pop_back();
	context.current_scope_id_ = mark.current_scope_id;
	gNamespaceRegistry.rollbackPublicationTransaction(mark.namespace_depth);
	rollbackLegacyTypeRegistryJournal(mark.legacy_type_registry_depth);
	gChunkedAnyStorage.truncateTo(mark.legacy_ast_node_count);
	parser.restoreParserCursor(mark.parser_publication_handle);
	parser.discardParserCursor(mark.parser_publication_handle);
	--context.publication_transaction_depth_;
	if (context.publication_transaction_depth_ == 0) {
		gFrontendPublicationJournalOwner = nullptr;
	}
}

Parser& FrontendContext::requireParserPublicationJournal() const {
	if (parser_publication_journal_ == nullptr) {
		throw InternalError("FrontendContext: publication requires a bound Parser");
	}
	return *parser_publication_journal_;
}

Parser& FrontendContext::parserForPublicationMark(const FrontendPublicationJournalMark& mark) const {
	Parser& parser = requireParserPublicationJournal();
	if (mark.parser_publication_handle == SIZE_MAX || &parser != &mark.parser_publication_journal) {
		throw InternalError("FrontendContext: publication checkpoint belongs to a different Parser");
	}
	return parser;
}

void FrontendContext::bindParserPublicationJournal(Parser& parser) {
	if (parser_publication_journal_ != nullptr && parser_publication_journal_ != &parser) {
		throw InternalError("FrontendContext: a different Parser is already bound");
	}
	if (publication_transaction_depth_ != 0 && parser_publication_journal_ == nullptr) {
		throw InternalError("FrontendContext: cannot bind a Parser during publication");
	}
	parser_publication_journal_ = &parser;
	parser.publication_journal_context_ = this;
}

void FrontendContext::unbindParserPublicationJournal(Parser& parser) {
	if (parser_publication_journal_ != &parser || parser.publication_journal_context_ != this) {
		throw InternalError("FrontendContext: cannot unbind a Parser owned by another context");
	}
	if (publication_transaction_depth_ != 0) {
		throw InternalError("FrontendContext: cannot unbind a Parser while publication transactions are open");
	}
	parser_publication_journal_ = nullptr;
	parser.publication_journal_context_ = nullptr;
}

FrontendContext::~FrontendContext() {
	if (publication_transaction_depth_ != 0) {
		std::terminate();
	}
	if (parser_publication_journal_ != nullptr) {
		parser_publication_journal_->publication_journal_context_ = nullptr;
	}
	parser_publication_journal_ = nullptr;
	releasePersistentScopePublicationTables();
	popActive(this);
}

void FrontendContext::registerPersistentScopePublicationTable(SymbolTable& table) {
	const auto already_registered = std::find(
		persistent_scope_publication_tables_.begin(),
		persistent_scope_publication_tables_.end(),
		&table);
	if (already_registered != persistent_scope_publication_tables_.end()) {
		return;
	}
	persistent_scope_publication_tables_.push_back(&table);
	size_t opened_depths = 0;
	try {
		while (opened_depths < publication_transaction_depth_) {
			const size_t table_depth = table.beginPublicationTransaction();
			if (table_depth != opened_depths + 1U) {
				table.rollbackPublicationTransaction(table_depth);
				throw InternalError("FrontendContext: late symbol table registration has invalid transaction depth");
			}
			++opened_depths;
		}
	} catch (...) {
		while (opened_depths > 0) {
			table.rollbackPublicationTransaction(opened_depths--);
		}
		persistent_scope_publication_tables_.pop_back();
		throw;
	}
}

void FrontendContext::releasePersistentScopePublicationTables() {
	while (!persistent_scope_publication_tables_.empty()) {
		persistent_scope_publication_tables_.back()->clearPersistentScopePublicationBinding();
	}
}

SymbolTable::~SymbolTable() noexcept {
	// Undo retained declaration changes before abandoning this table's journals.
	while (!publication_transaction_marks_.empty()) rollbackPublicationTransaction(publication_transaction_marks_.size());
	clearPersistentScopePublicationBinding();
}

void SymbolTable::clearPersistentScopePublicationBinding() noexcept {
	if (persistent_scope_publication_context_ != nullptr) {
		auto& tables = persistent_scope_publication_context_->persistent_scope_publication_tables_;
		if (!tables.empty() && tables.back() == this) tables.pop_back();
		else std::erase(tables, this);
	}
	publish_persistent_scopes_ = false;
	persistent_scope_publication_context_ = nullptr;
}

void SymbolTable::reset() {
	if (!publication_transaction_marks_.empty()) {
		throw InternalError("SymbolTable: reset during publication transaction");
	}
	clearPersistentScopePublicationBinding();
	clear();
	diagnostics_ = nullptr;
}

void SymbolTable::enablePersistentScopePublication() {
	FrontendContext& context = requireActiveFrontendContext();
	if (publish_persistent_scopes_) {
		if (persistent_scope_publication_context_ != &context) {
			throw InternalError("SymbolTable: persistent scope publication is already bound to a different FrontendContext");
		}
		return;
	}
	if (scopeCount() != 1 || currentScopeId().value != 1) {
		throw InternalError("SymbolTable: persistent scope publication can only be enabled at the global scope");
	}
	if (context.scopeRecordCount() != 1) {
		throw InternalError("SymbolTable: active FrontendContext is not at its initial global scope record");
	}
	context.registerPersistentScopePublicationTable(*this);
	persistent_scope_publication_context_ = &context;
	publish_persistent_scopes_ = true;
}

FrontendContext* SymbolTable::persistentScopePublicationContext() const {
	return persistent_scope_publication_context_;
}

void bindPersistentScopePublication(SymbolTable& table) {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		return;
	}
	if (table.persistentScopePublicationEnabled()) {
		if (table.persistentScopePublicationContext() != context) {
			throw InternalError("SymbolTable: persistent scope publication is already bound to a different FrontendContext");
		}
		table.clear();
		return;
	}
	if (context->scopeRecordCount() != 1 || context->currentScopeId() != ScopeId{1}) context->resetPersistentScopes();
	if (table.scopeCount() != 1 || table.currentScopeId().value != 1) {
		table.clear();
	}
	table.enablePersistentScopePublication();
}

void validatePersistentScopeEnter(const SymbolTable& table, ScopeId id, ScopeId parent_id) {
	requirePersistentScopePublicationContext(table).validatePersistentScopeEnter(id, parent_id);
}

void publishPersistentScopeEnter(
	SymbolTable& table,
	ScopeId id,
	ScopeId parent_id,
	ScopeType scope_type,
	uint32_t depth,
	NamespaceHandle namespace_handle) {
	requirePersistentScopePublicationContext(table).recordPersistentScopeEnter(
		id, parent_id, scope_type, depth, namespace_handle);
}

void publishPersistentScopeCursor(SymbolTable& table, ScopeId current_scope_id) {
	requirePersistentScopePublicationContext(table).setPersistentScopeCursor(current_scope_id);
}

void resetPersistentScopes(SymbolTable& table) {
	requirePersistentScopePublicationContext(table).resetPersistentScopes();
}

ScopeMetadataView readScopeMetadata(const SymbolTable& table, ScopeId scope_id) {
	if (!scope_id) {
		throw InternalError("readScopeMetadata: ScopeId out of range");
	}
	if (table.persistentScopePublicationEnabled()) {
		const ScopeRecord& record =
			requirePersistentScopePublicationContext(table).scopeRecord(scope_id);
		return ScopeMetadataView{
			record.parent_id,
			record.depth,
			record.scope_type,
			record.namespace_handle};
	}
	const ScopeMetadataView metadata = table.legacyScopeMetadata(scope_id);
	return ScopeMetadataView{
		metadata.parent_id,
		metadata.depth,
		metadata.scope_type,
		metadata.namespace_handle};
}
