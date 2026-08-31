#include "ScopePublication.h"

#include "CompileError.h"
#include "FrontendContext.h"
#include "SymbolTable.h"

void SymbolTable::enablePersistentScopePublication() {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		throw InternalError("SymbolTable: persistent scope publication requires an active FrontendContext");
	}
	if (scopeCount() != 1 || currentScopeId().value != 1) {
		throw InternalError("SymbolTable: persistent scope publication can only be enabled at the global scope");
	}
	if (context->scopeRecordCount() != 1) {
		throw InternalError("SymbolTable: active FrontendContext is not at its initial global scope record");
	}
	publish_persistent_scopes_ = true;
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
	ScopeId id,
	ScopeId parent_id,
	ScopeType scope_type,
	uint32_t depth,
	NamespaceHandle namespace_handle) {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		throw InternalError("persistent scope publication enabled without an active FrontendContext");
	}
	context->recordPersistentScopeEnter(id, parent_id, scope_type, depth, namespace_handle);
}

void publishPersistentScopeCursor(ScopeId current_scope_id) {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		throw InternalError("persistent scope publication enabled without an active FrontendContext");
	}
	context->setPersistentScopeCursor(current_scope_id);
}

void resetPersistentScopes() {
	FrontendContext* context = FrontendContext::active();
	if (context == nullptr) {
		throw InternalError("persistent scope publication enabled without an active FrontendContext");
	}
	context->resetPersistentScopes();
}
