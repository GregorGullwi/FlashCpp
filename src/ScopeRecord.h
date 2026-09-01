#pragma once

#include "FrontendIds.h"
#include "NamespaceRegistry.h"

#include <cstdint>
#include <type_traits>

enum class ScopeType : uint8_t {
	Global,
	Function,
	Block,
	Namespace,
};

// Compact persistent-scope metadata owned by FrontendContext. Symbol maps,
// using-directives, and aliases stay on SymbolTable until a later deletion.
struct ScopeRecord {
	ScopeId id;
	ScopeId parent_id;
	uint32_t depth = 1;
	NamespaceHandle namespace_handle{};
	ScopeType scope_type = ScopeType::Global;
	uint8_t reserved = 0;
};

// Lookup-facing scope metadata. When persistent publication is enabled,
// SymbolTable lookup reads this view from FrontendContext ScopeRecords.
struct ScopeMetadataView {
	ScopeId parent_id{};
	uint32_t depth = 0;
	ScopeType scope_type = ScopeType::Global;
	NamespaceHandle namespace_handle{};
};

inline ScopeRecord makeGlobalScopeRecord() {
	ScopeRecord record{};
	record.id = ScopeId{1};
	record.parent_id = ScopeId{};
	record.depth = 1;
	record.namespace_handle = NamespaceHandle{NamespaceHandle::INVALID_HANDLE};
	record.scope_type = ScopeType::Global;
	record.reserved = 0;
	return record;
}

// Sized from measured persistent-scope counts on the compiler test corpus
// (max 114 on sampled TUs; 256 leaves headroom for larger translation units).
inline constexpr uint32_t kScopeArenaChunkSize = 256;

static_assert(sizeof(ScopeRecord) == 16);
static_assert(alignof(ScopeRecord) == 4);
static_assert(std::is_trivially_copyable_v<ScopeRecord>);
static_assert(std::is_standard_layout_v<ScopeRecord>);
