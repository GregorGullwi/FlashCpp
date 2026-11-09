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

// Bookkeeping hooks run from hot AST mutators; avoid the thread-local active-context
// lookup when no frontend publication journal is open.
FrontendContext* activePublicationJournalContext() {
	return gFrontendPublicationJournalOwner != nullptr ? FrontendContext::active() : nullptr;
}

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
	const size_t previous_rewrite_start = context.legacy_qualified_identifier_rewrite_segment_start_;
	const size_t previous_template_binding_rewrite_start = context.legacy_outer_template_binding_rewrite_segment_start_;
	const size_t previous_struct_declaration_rewrite_start = context.legacy_struct_declaration_rewrite_segment_start_;
	const size_t previous_function_declaration_rewrite_start = context.legacy_function_declaration_rewrite_segment_start_;
	const size_t previous_constructor_declaration_rewrite_start = context.legacy_constructor_declaration_rewrite_segment_start_;
	const size_t previous_destructor_declaration_rewrite_start = context.legacy_destructor_declaration_rewrite_segment_start_;
	const size_t previous_declaration_node_rewrite_start = context.legacy_declaration_node_rewrite_segment_start_;
	const size_t previous_enum_declaration_rewrite_start = context.legacy_enum_declaration_rewrite_segment_start_;
	const size_t previous_typedef_declaration_rewrite_start = context.legacy_typedef_declaration_rewrite_segment_start_;
	const size_t previous_template_function_declaration_rewrite_start =
		context.legacy_template_function_declaration_rewrite_segment_start_;
	const size_t previous_template_class_declaration_rewrite_start =
		context.legacy_template_class_declaration_rewrite_segment_start_;
	const size_t previous_template_alias_declaration_rewrite_start =
		context.legacy_template_alias_declaration_rewrite_segment_start_;
	const size_t previous_template_variable_declaration_rewrite_start =
		context.legacy_template_variable_declaration_rewrite_segment_start_;
	const size_t previous_friend_declaration_rewrite_start = context.legacy_friend_declaration_rewrite_segment_start_;
	const size_t previous_variable_declaration_rewrite_start = context.legacy_variable_declaration_rewrite_segment_start_;
	const size_t previous_type_specifier_rewrite_start = context.legacy_type_specifier_rewrite_segment_start_;
	context.legacy_qualified_identifier_rewrite_segment_start_ = context.legacy_qualified_identifier_rewrites_.size();
	context.legacy_outer_template_binding_rewrite_segment_start_ = context.legacy_outer_template_binding_rewrites_.size();
	context.legacy_struct_declaration_rewrite_segment_start_ = context.legacy_struct_declaration_rewrites_.size();
	context.legacy_function_declaration_rewrite_segment_start_ = context.legacy_function_declaration_rewrites_.size();
	context.legacy_constructor_declaration_rewrite_segment_start_ = context.legacy_constructor_declaration_rewrites_.size();
	context.legacy_destructor_declaration_rewrite_segment_start_ = context.legacy_destructor_declaration_rewrites_.size();
	context.legacy_declaration_node_rewrite_segment_start_ = context.legacy_declaration_node_rewrites_.size();
	context.legacy_enum_declaration_rewrite_segment_start_ = context.legacy_enum_declaration_rewrites_.size();
	context.legacy_typedef_declaration_rewrite_segment_start_ = context.legacy_typedef_declaration_rewrites_.size();
	context.legacy_template_function_declaration_rewrite_segment_start_ =
		context.legacy_template_function_declaration_rewrites_.size();
	context.legacy_template_class_declaration_rewrite_segment_start_ =
		context.legacy_template_class_declaration_rewrites_.size();
	context.legacy_template_alias_declaration_rewrite_segment_start_ = context.legacy_template_alias_declaration_rewrites_.size();
	context.legacy_template_variable_declaration_rewrite_segment_start_ =
		context.legacy_template_variable_declaration_rewrites_.size();
	context.legacy_friend_declaration_rewrite_segment_start_ = context.legacy_friend_declaration_rewrites_.size();
	context.legacy_variable_declaration_rewrite_segment_start_ = context.legacy_variable_declaration_rewrites_.size();
	context.legacy_type_specifier_rewrite_segment_start_ = context.legacy_type_specifier_rewrites_.size();
	return FrontendPublicationJournalMark{context_depth, namespace_depth, legacy_type_registry_depth, context.scope_records_.size(),
		gChunkedAnyStorage.size(), context.legacy_qualified_identifier_rewrites_.size(), previous_rewrite_start,
		context.legacy_outer_template_binding_rewrites_.size(), previous_template_binding_rewrite_start,
		context.legacy_struct_declaration_rewrites_.size(), previous_struct_declaration_rewrite_start,
		context.legacy_function_declaration_rewrites_.size(), previous_function_declaration_rewrite_start,
		context.legacy_constructor_declaration_rewrites_.size(), previous_constructor_declaration_rewrite_start,
		context.legacy_destructor_declaration_rewrites_.size(), previous_destructor_declaration_rewrite_start,
		context.legacy_declaration_node_rewrites_.size(), previous_declaration_node_rewrite_start,
		context.legacy_enum_declaration_rewrites_.size(), previous_enum_declaration_rewrite_start,
		context.legacy_typedef_declaration_rewrites_.size(), previous_typedef_declaration_rewrite_start,
		context.legacy_template_function_declaration_rewrites_.size(), previous_template_function_declaration_rewrite_start,
		context.legacy_template_class_declaration_rewrites_.size(), previous_template_class_declaration_rewrite_start,
		context.legacy_template_alias_declaration_rewrites_.size(), previous_template_alias_declaration_rewrite_start,
		context.legacy_template_variable_declaration_rewrites_.size(), previous_template_variable_declaration_rewrite_start,
		context.legacy_friend_declaration_rewrites_.size(), previous_friend_declaration_rewrite_start,
		context.legacy_variable_declaration_rewrites_.size(), previous_variable_declaration_rewrite_start,
		context.legacy_type_specifier_rewrites_.size(), previous_type_specifier_rewrite_start, parser_journal,
		parser_publication_handle, context.current_scope_id_};
}

Parser& validateFrontendPublicationJournal(const FrontendContext& context, const FrontendPublicationJournalMark& mark, bool rollback) {
	if (gFrontendPublicationJournalOwner != &context || context.publication_transaction_depth_ != mark.context_depth) {
		throw InternalError("FrontendContext: publication transactions must close in nesting order");
	}
	Parser& parser = context.parserForPublicationMark(mark);
	parser.validateParserPublicationCheckpoint(mark.parser_publication_handle);
	if (!rollback) {
		parser.validateParserPublicationContexts(mark.parser_publication_handle, false);
	}
	for (const SymbolTable* table : context.persistent_scope_publication_tables_) {
		table->validatePublicationTransactionDepth(mark.context_depth);
	}
	gNamespaceRegistry.validatePublicationTransactionDepth(mark.namespace_depth);
	validateLegacyTypeRegistryJournal(mark.legacy_type_registry_depth);
	if (rollback) {
		parser.validateParserPublicationRollback(mark.parser_publication_handle);
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
		context.legacy_qualified_identifier_rewrites_.clear();
		context.legacy_qualified_identifier_rewrite_segment_start_ = 0;
		context.legacy_outer_template_binding_rewrites_.clear();
		context.legacy_outer_template_binding_rewrite_segment_start_ = 0;
		context.legacy_struct_declaration_rewrites_.clear();
		context.legacy_struct_declaration_rewrite_segment_start_ = 0;
		context.legacy_function_declaration_rewrites_.clear();
		context.legacy_function_declaration_rewrite_segment_start_ = 0;
		context.legacy_constructor_declaration_rewrites_.clear();
		context.legacy_constructor_declaration_rewrite_segment_start_ = 0;
		context.legacy_destructor_declaration_rewrites_.clear();
		context.legacy_destructor_declaration_rewrite_segment_start_ = 0;
		context.legacy_declaration_node_rewrites_.clear();
		context.legacy_declaration_node_rewrite_segment_start_ = 0;
		context.legacy_enum_declaration_rewrites_.clear();
		context.legacy_enum_declaration_rewrite_segment_start_ = 0;
		context.legacy_typedef_declaration_rewrites_.clear();
		context.legacy_typedef_declaration_rewrite_segment_start_ = 0;
		context.legacy_template_function_declaration_rewrites_.clear();
		context.legacy_template_function_declaration_rewrite_segment_start_ = 0;
		context.legacy_template_class_declaration_rewrites_.clear();
		context.legacy_template_class_declaration_rewrite_segment_start_ = 0;
		context.legacy_template_alias_declaration_rewrites_.clear();
		context.legacy_template_alias_declaration_rewrite_segment_start_ = 0;
		context.legacy_template_variable_declaration_rewrites_.clear();
		context.legacy_template_variable_declaration_rewrite_segment_start_ = 0;
		context.legacy_friend_declaration_rewrites_.clear();
		context.legacy_friend_declaration_rewrite_segment_start_ = 0;
		context.legacy_variable_declaration_rewrites_.clear();
		context.legacy_variable_declaration_rewrite_segment_start_ = 0;
		context.legacy_type_specifier_rewrites_.clear();
		context.legacy_type_specifier_rewrite_segment_start_ = 0;
		gFrontendPublicationJournalOwner = nullptr;
	} else {
		context.legacy_qualified_identifier_rewrite_segment_start_ = mark.previous_legacy_qualified_identifier_rewrite_segment_start;
		context.legacy_outer_template_binding_rewrite_segment_start_ = mark.previous_legacy_outer_template_binding_rewrite_segment_start;
		context.legacy_struct_declaration_rewrite_segment_start_ = mark.previous_legacy_struct_declaration_rewrite_segment_start;
		context.legacy_function_declaration_rewrite_segment_start_ = mark.previous_legacy_function_declaration_rewrite_segment_start;
		context.legacy_constructor_declaration_rewrite_segment_start_ = mark.previous_legacy_constructor_declaration_rewrite_segment_start;
		context.legacy_destructor_declaration_rewrite_segment_start_ = mark.previous_legacy_destructor_declaration_rewrite_segment_start;
		context.legacy_declaration_node_rewrite_segment_start_ = mark.previous_legacy_declaration_node_rewrite_segment_start;
		context.legacy_enum_declaration_rewrite_segment_start_ = mark.previous_legacy_enum_declaration_rewrite_segment_start;
		context.legacy_typedef_declaration_rewrite_segment_start_ = mark.previous_legacy_typedef_declaration_rewrite_segment_start;
		context.legacy_template_function_declaration_rewrite_segment_start_ =
			mark.previous_legacy_template_function_declaration_rewrite_segment_start;
		context.legacy_template_class_declaration_rewrite_segment_start_ =
			mark.previous_legacy_template_class_declaration_rewrite_segment_start;
		context.legacy_template_alias_declaration_rewrite_segment_start_ =
			mark.previous_legacy_template_alias_declaration_rewrite_segment_start;
		context.legacy_template_variable_declaration_rewrite_segment_start_ =
			mark.previous_legacy_template_variable_declaration_rewrite_segment_start;
		context.legacy_friend_declaration_rewrite_segment_start_ = mark.previous_legacy_friend_declaration_rewrite_segment_start;
		context.legacy_variable_declaration_rewrite_segment_start_ = mark.previous_legacy_variable_declaration_rewrite_segment_start;
		context.legacy_type_specifier_rewrite_segment_start_ = mark.previous_legacy_type_specifier_rewrite_segment_start;
	}
}

void rollbackFrontendPublicationJournal(FrontendContext& context, FrontendPublicationJournalMark mark) {
	Parser& parser = validateFrontendPublicationJournal(context, mark, true);
	for (auto it = context.persistent_scope_publication_tables_.rbegin();
		it != context.persistent_scope_publication_tables_.rend();
		++it) {
		(*it)->rollbackPublicationTransaction(mark.context_depth);
	}
	parser.rollbackParserPublication(mark.parser_publication_handle);
	while (context.scope_records_.size() > mark.scope_count) context.scope_records_.pop_back();
	context.current_scope_id_ = mark.current_scope_id;
	gNamespaceRegistry.rollbackPublicationTransaction(mark.namespace_depth);
	rollbackLegacyTypeRegistryJournal(mark.legacy_type_registry_depth);
	for (size_t index = context.legacy_qualified_identifier_rewrites_.size(); index > mark.legacy_qualified_identifier_rewrite_count; --index) {
		LegacyQualifiedIdentifierRewrite& rewrite = context.legacy_qualified_identifier_rewrites_[index - 1];
		if (rewrite.node == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.node)) {
			continue;
		}
		*rewrite.node = std::move(*rewrite.snapshot);
	}
	context.legacy_qualified_identifier_rewrites_.resize(mark.legacy_qualified_identifier_rewrite_count);
	// Whole-node constructor/destructor snapshots may include bindings captured after
	// an outer-binding rewrite. Restore them first so the targeted outer snapshot wins.
	for (size_t index = context.legacy_constructor_declaration_rewrites_.size(); index > mark.legacy_constructor_declaration_rewrite_count; --index) {
		LegacyConstructorDeclarationRewrite& rewrite = context.legacy_constructor_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_constructor_declaration_rewrites_.resize(mark.legacy_constructor_declaration_rewrite_count);
	for (size_t index = context.legacy_destructor_declaration_rewrites_.size(); index > mark.legacy_destructor_declaration_rewrite_count; --index) {
		LegacyDestructorDeclarationRewrite& rewrite = context.legacy_destructor_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_destructor_declaration_rewrites_.resize(mark.legacy_destructor_declaration_rewrite_count);
	for (size_t index = context.legacy_outer_template_binding_rewrites_.size(); index > mark.legacy_outer_template_binding_rewrite_count; --index) {
		LegacyOuterTemplateBindingRewrite& rewrite = context.legacy_outer_template_binding_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.names_snapshot.has_value() || !rewrite.args_snapshot.has_value() ||
			!gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.names = std::move(*rewrite.names_snapshot);
		*rewrite.args = std::move(*rewrite.args_snapshot);
		if (rewrite.snapshot_node != nullptr) {
			*rewrite.snapshot_node = rewrite.snapshot_node_value;
		}
	}
	context.legacy_outer_template_binding_rewrites_.resize(mark.legacy_outer_template_binding_rewrite_count);
	for (size_t index = context.legacy_struct_declaration_rewrites_.size(); index > mark.legacy_struct_declaration_rewrite_count; --index) {
		LegacyStructDeclarationRewrite& rewrite = context.legacy_struct_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_struct_declaration_rewrites_.resize(mark.legacy_struct_declaration_rewrite_count);
	for (size_t index = context.legacy_function_declaration_rewrites_.size(); index > mark.legacy_function_declaration_rewrite_count; --index) {
		LegacyFunctionDeclarationRewrite& rewrite = context.legacy_function_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		restoreFunctionDeclarationRewrite(*rewrite.owner, *rewrite.snapshot);
	}
	context.legacy_function_declaration_rewrites_.resize(mark.legacy_function_declaration_rewrite_count);
	for (size_t index = context.legacy_declaration_node_rewrites_.size(); index > mark.legacy_declaration_node_rewrite_count; --index) {
		LegacyDeclarationNodeRewrite& rewrite = context.legacy_declaration_node_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_declaration_node_rewrites_.resize(mark.legacy_declaration_node_rewrite_count);
	for (size_t index = context.legacy_enum_declaration_rewrites_.size(); index > mark.legacy_enum_declaration_rewrite_count; --index) {
		LegacyEnumDeclarationRewrite& rewrite = context.legacy_enum_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_enum_declaration_rewrites_.resize(mark.legacy_enum_declaration_rewrite_count);
	for (size_t index = context.legacy_typedef_declaration_rewrites_.size(); index > mark.legacy_typedef_declaration_rewrite_count; --index) {
		LegacyTypedefDeclarationRewrite& rewrite = context.legacy_typedef_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		restoreTypedefDeclarationRewrite(*rewrite.owner, rewrite.lexical_scope_id);
	}
	context.legacy_typedef_declaration_rewrites_.resize(mark.legacy_typedef_declaration_rewrite_count);
	for (size_t index = context.legacy_template_function_declaration_rewrites_.size();
		index > mark.legacy_template_function_declaration_rewrite_count; --index) {
		LegacyTemplateFunctionDeclarationRewrite& rewrite = context.legacy_template_function_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_template_function_declaration_rewrites_.resize(mark.legacy_template_function_declaration_rewrite_count);
	for (size_t index = context.legacy_template_class_declaration_rewrites_.size();
		index > mark.legacy_template_class_declaration_rewrite_count; --index) {
		LegacyTemplateClassDeclarationRewrite& rewrite = context.legacy_template_class_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_template_class_declaration_rewrites_.resize(mark.legacy_template_class_declaration_rewrite_count);
	for (size_t index = context.legacy_template_alias_declaration_rewrites_.size();
		index > mark.legacy_template_alias_declaration_rewrite_count; --index) {
		LegacyTemplateAliasDeclarationRewrite& rewrite = context.legacy_template_alias_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_template_alias_declaration_rewrites_.resize(mark.legacy_template_alias_declaration_rewrite_count);
	for (size_t index = context.legacy_template_variable_declaration_rewrites_.size();
		index > mark.legacy_template_variable_declaration_rewrite_count; --index) {
		LegacyTemplateVariableDeclarationRewrite& rewrite = context.legacy_template_variable_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_template_variable_declaration_rewrites_.resize(mark.legacy_template_variable_declaration_rewrite_count);
	for (size_t index = context.legacy_friend_declaration_rewrites_.size(); index > mark.legacy_friend_declaration_rewrite_count; --index) {
		LegacyFriendDeclarationRewrite& rewrite = context.legacy_friend_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_friend_declaration_rewrites_.resize(mark.legacy_friend_declaration_rewrite_count);
	for (size_t index = context.legacy_variable_declaration_rewrites_.size(); index > mark.legacy_variable_declaration_rewrite_count; --index) {
		LegacyVariableDeclarationRewrite& rewrite = context.legacy_variable_declaration_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_variable_declaration_rewrites_.resize(mark.legacy_variable_declaration_rewrite_count);
	for (size_t index = context.legacy_type_specifier_rewrites_.size(); index > mark.legacy_type_specifier_rewrite_count; --index) {
		LegacyTypeSpecifierRewrite& rewrite = context.legacy_type_specifier_rewrites_[index - 1];
		if (rewrite.owner == nullptr || !rewrite.snapshot.has_value() || !gChunkedAnyStorage.containsAddress(rewrite.owner)) {
			continue;
		}
		*rewrite.owner = std::move(*rewrite.snapshot);
	}
	context.legacy_type_specifier_rewrites_.resize(mark.legacy_type_specifier_rewrite_count);
	gChunkedAnyStorage.truncateTo(mark.legacy_ast_node_count);
	parser.restoreParserCursor(mark.parser_publication_handle);
	parser.discardParserCursor(mark.parser_publication_handle);
	--context.publication_transaction_depth_;
	if (context.publication_transaction_depth_ == 0) {
		context.legacy_qualified_identifier_rewrite_segment_start_ = 0;
		context.legacy_outer_template_binding_rewrite_segment_start_ = 0;
		context.legacy_struct_declaration_rewrite_segment_start_ = 0;
		context.legacy_function_declaration_rewrite_segment_start_ = 0;
		context.legacy_constructor_declaration_rewrite_segment_start_ = 0;
		context.legacy_destructor_declaration_rewrite_segment_start_ = 0;
		context.legacy_declaration_node_rewrite_segment_start_ = 0;
		context.legacy_enum_declaration_rewrite_segment_start_ = 0;
		context.legacy_typedef_declaration_rewrite_segment_start_ = 0;
		context.legacy_template_function_declaration_rewrite_segment_start_ = 0;
		context.legacy_template_class_declaration_rewrite_segment_start_ = 0;
		context.legacy_template_alias_declaration_rewrite_segment_start_ = 0;
		context.legacy_template_variable_declaration_rewrite_segment_start_ = 0;
		context.legacy_friend_declaration_rewrite_segment_start_ = 0;
		context.legacy_variable_declaration_rewrite_segment_start_ = 0;
		context.legacy_type_specifier_rewrite_segment_start_ = 0;
		gFrontendPublicationJournalOwner = nullptr;
	} else {
		context.legacy_qualified_identifier_rewrite_segment_start_ = mark.previous_legacy_qualified_identifier_rewrite_segment_start;
		context.legacy_outer_template_binding_rewrite_segment_start_ = mark.previous_legacy_outer_template_binding_rewrite_segment_start;
		context.legacy_struct_declaration_rewrite_segment_start_ = mark.previous_legacy_struct_declaration_rewrite_segment_start;
		context.legacy_function_declaration_rewrite_segment_start_ = mark.previous_legacy_function_declaration_rewrite_segment_start;
		context.legacy_constructor_declaration_rewrite_segment_start_ = mark.previous_legacy_constructor_declaration_rewrite_segment_start;
		context.legacy_destructor_declaration_rewrite_segment_start_ = mark.previous_legacy_destructor_declaration_rewrite_segment_start;
		context.legacy_declaration_node_rewrite_segment_start_ = mark.previous_legacy_declaration_node_rewrite_segment_start;
		context.legacy_enum_declaration_rewrite_segment_start_ = mark.previous_legacy_enum_declaration_rewrite_segment_start;
		context.legacy_typedef_declaration_rewrite_segment_start_ = mark.previous_legacy_typedef_declaration_rewrite_segment_start;
		context.legacy_template_function_declaration_rewrite_segment_start_ =
			mark.previous_legacy_template_function_declaration_rewrite_segment_start;
		context.legacy_template_class_declaration_rewrite_segment_start_ =
			mark.previous_legacy_template_class_declaration_rewrite_segment_start;
		context.legacy_template_alias_declaration_rewrite_segment_start_ =
			mark.previous_legacy_template_alias_declaration_rewrite_segment_start;
		context.legacy_template_variable_declaration_rewrite_segment_start_ =
			mark.previous_legacy_template_variable_declaration_rewrite_segment_start;
		context.legacy_friend_declaration_rewrite_segment_start_ = mark.previous_legacy_friend_declaration_rewrite_segment_start;
		context.legacy_variable_declaration_rewrite_segment_start_ = mark.previous_legacy_variable_declaration_rewrite_segment_start;
		context.legacy_type_specifier_rewrite_segment_start_ = mark.previous_legacy_type_specifier_rewrite_segment_start;
	}
}

Parser& FrontendContext::requireParserPublicationJournal() const {
	if (parser_publication_journal_ == nullptr) {
		throw InternalError("FrontendContext: publication requires a bound Parser");
	}
	return *parser_publication_journal_;
}

void FrontendContext::journalQualifiedIdentifierRewrite(QualifiedIdentifierNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_qualified_identifier_rewrite_segment_start_; index < legacy_qualified_identifier_rewrites_.size(); ++index) {
		if (legacy_qualified_identifier_rewrites_[index].node == &node) {
			return;
		}
	}
	LegacyQualifiedIdentifierRewrite rewrite;
	rewrite.node = &node;
	rewrite.snapshot.emplace(node);
	legacy_qualified_identifier_rewrites_.push_back(std::move(rewrite));
}

void journalQualifiedIdentifierRewrite(QualifiedIdentifierNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalQualifiedIdentifierRewrite(node);
}

void FrontendContext::journalOuterTemplateBindingRewrite(void* owner, TemplateParamNameVector& names, TemplateArgInfoVector& args,
	const TemplateEnvironmentSnapshotNode*& snapshot_node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(owner)) {
		return;
	}
	for (size_t index = legacy_outer_template_binding_rewrite_segment_start_; index < legacy_outer_template_binding_rewrites_.size(); ++index) {
		if (legacy_outer_template_binding_rewrites_[index].owner == owner) {
			return;
		}
	}
	LegacyOuterTemplateBindingRewrite rewrite;
	rewrite.owner = owner;
	rewrite.names = &names;
	rewrite.args = &args;
	rewrite.snapshot_node = &snapshot_node;
	rewrite.names_snapshot.emplace(names);
	rewrite.args_snapshot.emplace(args);
	rewrite.snapshot_node_value = snapshot_node;
	legacy_outer_template_binding_rewrites_.push_back(std::move(rewrite));
}

void FrontendContext::journalOuterTemplateBindingRewrite(void* owner, TemplateParamNameVector& names, TemplateArgInfoVector& args) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(owner)) {
		return;
	}
	for (size_t index = legacy_outer_template_binding_rewrite_segment_start_; index < legacy_outer_template_binding_rewrites_.size(); ++index) {
		if (legacy_outer_template_binding_rewrites_[index].owner == owner) {
			return;
		}
	}
	LegacyOuterTemplateBindingRewrite rewrite;
	rewrite.owner = owner;
	rewrite.names = &names;
	rewrite.args = &args;
	rewrite.names_snapshot.emplace(names);
	rewrite.args_snapshot.emplace(args);
	legacy_outer_template_binding_rewrites_.push_back(std::move(rewrite));
}

void FrontendContext::journalStructDeclarationRewrite(StructDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_struct_declaration_rewrite_segment_start_; index < legacy_struct_declaration_rewrites_.size(); ++index) {
		if (legacy_struct_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyStructDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_struct_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalStructDeclarationRewrite(StructDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalStructDeclarationRewrite(node);
}

void FrontendContext::journalFunctionDeclarationRewrite(FunctionDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_function_declaration_rewrite_segment_start_; index < legacy_function_declaration_rewrites_.size(); ++index) {
		if (legacy_function_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyFunctionDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node.makeJournalSnapshot());
	legacy_function_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalFunctionDeclarationRewrite(FunctionDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalFunctionDeclarationRewrite(node);
}

void FrontendContext::journalConstructorDeclarationRewrite(ConstructorDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_constructor_declaration_rewrite_segment_start_; index < legacy_constructor_declaration_rewrites_.size(); ++index) {
		if (legacy_constructor_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyConstructorDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_constructor_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalConstructorDeclarationRewrite(ConstructorDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalConstructorDeclarationRewrite(node);
}

void FrontendContext::journalDestructorDeclarationRewrite(DestructorDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_destructor_declaration_rewrite_segment_start_; index < legacy_destructor_declaration_rewrites_.size(); ++index) {
		if (legacy_destructor_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyDestructorDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_destructor_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalDestructorDeclarationRewrite(DestructorDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalDestructorDeclarationRewrite(node);
}

void FrontendContext::journalDeclarationNodeRewrite(DeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_declaration_node_rewrite_segment_start_; index < legacy_declaration_node_rewrites_.size(); ++index) {
		if (legacy_declaration_node_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyDeclarationNodeRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_declaration_node_rewrites_.push_back(std::move(rewrite));
}

void journalDeclarationNodeRewrite(DeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalDeclarationNodeRewrite(node);
}

void FrontendContext::journalEnumDeclarationRewrite(EnumDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_enum_declaration_rewrite_segment_start_; index < legacy_enum_declaration_rewrites_.size(); ++index) {
		if (legacy_enum_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyEnumDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_enum_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalEnumDeclarationRewrite(EnumDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalEnumDeclarationRewrite(node);
}

void FrontendContext::journalTypedefDeclarationRewrite(TypedefDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_typedef_declaration_rewrite_segment_start_; index < legacy_typedef_declaration_rewrites_.size(); ++index) {
		if (legacy_typedef_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyTypedefDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.lexical_scope_id = node.lexical_scope_id();
	legacy_typedef_declaration_rewrites_.push_back(rewrite);
}

void journalTypedefDeclarationRewrite(TypedefDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalTypedefDeclarationRewrite(node);
}

void FrontendContext::journalTemplateFunctionDeclarationRewrite(TemplateFunctionDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_template_function_declaration_rewrite_segment_start_;
		index < legacy_template_function_declaration_rewrites_.size(); ++index) {
		if (legacy_template_function_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyTemplateFunctionDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_template_function_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalTemplateFunctionDeclarationRewrite(TemplateFunctionDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalTemplateFunctionDeclarationRewrite(node);
}

void FrontendContext::journalTemplateClassDeclarationRewrite(TemplateClassDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_template_class_declaration_rewrite_segment_start_;
		index < legacy_template_class_declaration_rewrites_.size(); ++index) {
		if (legacy_template_class_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyTemplateClassDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_template_class_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalTemplateClassDeclarationRewrite(TemplateClassDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalTemplateClassDeclarationRewrite(node);
}

void FrontendContext::journalTemplateAliasDeclarationRewrite(TemplateAliasNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_template_alias_declaration_rewrite_segment_start_;
		index < legacy_template_alias_declaration_rewrites_.size(); ++index) {
		if (legacy_template_alias_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyTemplateAliasDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_template_alias_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalTemplateAliasDeclarationRewrite(TemplateAliasNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalTemplateAliasDeclarationRewrite(node);
}

void FrontendContext::journalTemplateVariableDeclarationRewrite(TemplateVariableDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_template_variable_declaration_rewrite_segment_start_;
		index < legacy_template_variable_declaration_rewrites_.size(); ++index) {
		if (legacy_template_variable_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyTemplateVariableDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_template_variable_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalTemplateVariableDeclarationRewrite(TemplateVariableDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalTemplateVariableDeclarationRewrite(node);
}

void FrontendContext::journalFriendDeclarationRewrite(FriendDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_friend_declaration_rewrite_segment_start_; index < legacy_friend_declaration_rewrites_.size(); ++index) {
		if (legacy_friend_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyFriendDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_friend_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalFriendDeclarationRewrite(FriendDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalFriendDeclarationRewrite(node);
}

void FrontendContext::journalVariableDeclarationRewrite(VariableDeclarationNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_variable_declaration_rewrite_segment_start_; index < legacy_variable_declaration_rewrites_.size(); ++index) {
		if (legacy_variable_declaration_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyVariableDeclarationRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_variable_declaration_rewrites_.push_back(std::move(rewrite));
}

void journalVariableDeclarationRewrite(VariableDeclarationNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalVariableDeclarationRewrite(node);
}

void FrontendContext::journalTypeSpecifierRewrite(TypeSpecifierNode& node) {
	if (publication_transaction_depth_ == 0 || !gChunkedAnyStorage.containsAddress(&node)) {
		return;
	}
	for (size_t index = legacy_type_specifier_rewrite_segment_start_; index < legacy_type_specifier_rewrites_.size(); ++index) {
		if (legacy_type_specifier_rewrites_[index].owner == &node) {
			return;
		}
	}
	LegacyTypeSpecifierRewrite rewrite;
	rewrite.owner = &node;
	rewrite.snapshot.emplace(node);
	legacy_type_specifier_rewrites_.push_back(std::move(rewrite));
}

void journalTypeSpecifierRewriteImpl(TypeSpecifierNode& node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalTypeSpecifierRewrite(node);
}

namespace {

const bool type_specifier_rewrite_journal_hook_installed = []() {
	gTypeSpecifierRewriteJournalHook = &journalTypeSpecifierRewriteImpl;
	return true;
}();

} // namespace

void journalOuterTemplateBindingRewrite(void* owner, TemplateParamNameVector& names, TemplateArgInfoVector& args,
	const TemplateEnvironmentSnapshotNode*& snapshot_node) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalOuterTemplateBindingRewrite(owner, names, args, snapshot_node);
}

void journalOuterTemplateBindingRewrite(void* owner, TemplateParamNameVector& names, TemplateArgInfoVector& args) {
	FrontendContext* context = activePublicationJournalContext();
	if (context == nullptr) {
		return;
	}
	context->journalOuterTemplateBindingRewrite(owner, names, args);
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
