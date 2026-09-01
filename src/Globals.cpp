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

namespace {

FrontendContext* requirePersistentScopePublicationContext(const SymbolTable& table) {
	FrontendContext* context = table.persistentScopePublicationContext();
	if (context == nullptr) {
		throw InternalError("SymbolTable: persistent scope publication is missing its bound FrontendContext");
	}
	return context;
}

} // namespace

// Persistent-scope dual-write lives here so SymbolTable.h does not include
// FrontendContext.h (and so this is not its own modular translation unit).
void SymbolTable::enablePersistentScopePublication() {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		throw InternalError("SymbolTable: persistent scope publication requires an active FrontendContext");
	}
	if (publish_persistent_scopes_) {
		if (persistent_scope_publication_context_ != context) {
			throw InternalError("SymbolTable: persistent scope publication is already bound to a different FrontendContext");
		}
		return;
	}
	if (scopeCount() != 1 || currentScopeId().value != 1) {
		throw InternalError("SymbolTable: persistent scope publication can only be enabled at the global scope");
	}
	if (context->scopeRecordCount() != 1) {
		throw InternalError("SymbolTable: active FrontendContext is not at its initial global scope record");
	}
	persistent_scope_publication_context_ = context;
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
	context->resetPersistentScopes();
	table.enablePersistentScopePublication();
}

void publishPersistentScopeEnter(
	SymbolTable& table,
	ScopeId id,
	ScopeId parent_id,
	ScopeType scope_type,
	uint32_t depth,
	NamespaceHandle namespace_handle) {
	FrontendContext* context = requirePersistentScopePublicationContext(table);
	context->recordPersistentScopeEnter(id, parent_id, scope_type, depth, namespace_handle);
}

void publishPersistentScopeCursor(SymbolTable& table, ScopeId current_scope_id) {
	FrontendContext* context = requirePersistentScopePublicationContext(table);
	context->setPersistentScopeCursor(current_scope_id);
}

void resetPersistentScopes(SymbolTable& table) {
	FrontendContext* context = requirePersistentScopePublicationContext(table);
	context->resetPersistentScopes();
}

ScopeMetadataView readScopeMetadata(const SymbolTable& table, ScopeId scope_id) {
	if (table.persistentScopePublicationEnabled()) {
		FrontendContext* context = requirePersistentScopePublicationContext(table);
		const ScopeRecord& record = context->scopeRecord(scope_id);
		return ScopeMetadataView{
			record.parent_id,
			record.depth,
			record.scope_type,
			record.namespace_handle};
	}
	const Scope* scope = table.findScopeById(scope_id);
	if (scope == nullptr) {
		throw InternalError("readScopeMetadata: ScopeId out of range");
	}
	return ScopeMetadataView{
		scope->parent_scope_id,
		scope->depth,
		scope->scope_type,
		scope->namespace_handle};
}
