// Globals.cpp - Shared global state for FlashCpp compiler
// These globals are needed by both main.cpp and FlashCppTest.cpp.
// Included via FlashCppUnity.h (unity build).

#include "NamespaceRegistry.h"
#include "LazyMemberResolver.h"
#include "InstantiationQueue.h"
#include "TemplateTypes.h"
#include "LegacyChunkedAnyStoragePolicyComplete.h"
#include "FrontendContext.h"
#include "SymbolTable.h"

#include <algorithm>

// Global debug flag
bool g_enable_debug_output = false;

// Global exception handling control
bool g_enable_exceptions = true;

NamespaceRegistry gNamespaceRegistry;
LegacyAstChunkedAnyVector gChunkedAnyStorage;

namespace FlashCpp {
ChunkedVector<StructuralClassValue> gStructuralClassValues;
LazyMemberResolver gLazyMemberResolver;
InstantiationQueue gInstantiationQueue;
} // namespace FlashCpp

// Persistent scope publication lives here so SymbolTable.h does not include
// FrontendContext.h (and so this is not its own modular translation unit).
namespace {

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

FrontendPublicationJournalMark beginFrontendPublicationJournal(FrontendContext& context) {
	const size_t context_depth = context.publication_transaction_depth_ + 1U;
	const size_t namespace_depth = gNamespaceRegistry.beginPublicationTransaction();

	size_t opened_tables = 0;
	try {
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
		gNamespaceRegistry.rollbackPublicationTransaction(namespace_depth);
		throw;
	}

	context.publication_transaction_depth_ = context_depth;
	return FrontendPublicationJournalMark{context_depth, namespace_depth};
}

void commitFrontendPublicationJournal(FrontendContext& context, FrontendPublicationJournalMark mark) {
	if (context.publication_transaction_depth_ != mark.context_depth) {
		throw InternalError("FrontendContext: publication transactions must close in nesting order");
	}
	for (auto it = context.persistent_scope_publication_tables_.rbegin();
		it != context.persistent_scope_publication_tables_.rend();
		++it) {
		(*it)->commitPublicationTransaction(mark.context_depth);
	}
	gNamespaceRegistry.commitPublicationTransaction(mark.namespace_depth);
	--context.publication_transaction_depth_;
}

void rollbackFrontendPublicationJournal(FrontendContext& context, FrontendPublicationJournalMark mark) {
	if (context.publication_transaction_depth_ != mark.context_depth) {
		throw InternalError("FrontendContext: publication transactions must close in nesting order");
	}
	for (auto it = context.persistent_scope_publication_tables_.rbegin();
		it != context.persistent_scope_publication_tables_.rend();
		++it) {
		(*it)->rollbackPublicationTransaction(mark.context_depth);
	}
	gNamespaceRegistry.rollbackPublicationTransaction(mark.namespace_depth);
	--context.publication_transaction_depth_;
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
	for (SymbolTable* table : persistent_scope_publication_tables_) {
		table->clearPersistentScopePublicationBinding();
	}
	persistent_scope_publication_tables_.clear();
}

void SymbolTable::clearPersistentScopePublicationBinding() {
	publish_persistent_scopes_ = false;
	persistent_scope_publication_context_ = nullptr;
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
	context->resetPersistentScopes();
	if (table.scopeCount() != 1 || table.currentScopeId().value != 1) {
		table.clear();
	}
	table.enablePersistentScopePublication();
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
