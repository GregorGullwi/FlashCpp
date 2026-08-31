#pragma once

#include "ScopeRecord.h"

class SymbolTable;

// Choke-point hooks so SymbolTable.h does not include FrontendContext.h.
// No-ops unless the calling table opted in through
// SymbolTable::enablePersistentScopePublication.
void publishPersistentScopeEnter(
	ScopeId id,
	ScopeId parent_id,
	ScopeType scope_type,
	uint32_t depth,
	NamespaceHandle namespace_handle);
void publishPersistentScopeCursor(ScopeId current_scope_id);
void resetPersistentScopes();
void bindPersistentScopePublication(SymbolTable& table);
