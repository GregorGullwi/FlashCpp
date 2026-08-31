#pragma once

#include "ArenaDomains.h"
#include "ChunkedString.h"
#include "DeclarationBuilder.h"
#include "FrontendIds.h"
#include "InlineVector.h"
#include "Log.h"
#include "StringTable.h"

#include <array>
#include <cstdint>
#include <vector>

// Per-translation-unit front-end shell. Active-context lookup uses a thread-local
// stack so concurrent compilations do not share state; each FrontendContext owns
// its own arenas and domain stats.
class FrontendContext {
public:
	FrontendContext() {
		pushActive(this);
		refreshScratchDomainStats();
	}

	~FrontendContext() {
		popActive(this);
	}

	FrontendContext(const FrontendContext&) = delete;
	FrontendContext& operator=(const FrontendContext&) = delete;

	static FrontendContext* active() {
		std::vector<FrontendContext*>& stack = activeContextStack();
		if (stack.empty()) {
			return nullptr;
		}
		return stack.back();
	}

	ScratchTransaction beginScratchTransaction() {
		return ScratchTransaction(scratch_arena_, scratch_registry_);
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

	DomainByteStats domainStats(AllocationDomain domain) const {
		return domain_stats_[static_cast<std::size_t>(domain)];
	}

	std::size_t inlineVectorSpillCount() const {
		return FlashCpp::inlineVectorSpillCount();
	}

	void publishScopeState(ScopeId current_scope_id, std::size_t scope_count) {
		current_scope_id_ = current_scope_id;
		scope_count_ = scope_count;
	}

	ScopeId currentScopeId() const {
		return current_scope_id_;
	}

	std::size_t scopeCount() const {
		return scope_count_;
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
		const uint64_t used =
			declaration_builder_.declarationArenaUsedBytes() + declaration_builder_.entityArenaUsedBytes();
		const uint64_t reserved =
			declaration_builder_.declarationArenaReservedBytes() +
			declaration_builder_.entityArenaReservedBytes();
		DomainByteStats& stats = domain_stats_[static_cast<std::size_t>(AllocationDomain::Semantic)];
		stats.current_bytes = used;
		stats.peak_bytes = declaration_builder_.peakSemanticArenaUsedBytes();
		stats.reserved_bytes = reserved;
		stats.peak_reserved_bytes = declaration_builder_.peakSemanticArenaReservedBytes();
	}

	void refreshSyntaxDomainStats() {
		applyDomainBytes(
			domain_stats_[static_cast<std::size_t>(AllocationDomain::Syntax)],
			gChunkedAnyStorage.usedBytes(),
			gChunkedAnyStorage.reservedBytes());
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
		FLASH_LOG(General, Info,
				  "  persistent scopes (count/current ScopeId): ",
				  scope_count_,
				  "/",
				  current_scope_id_.value);
		FLASH_LOG(General, Info,
				  "  declarations/entities: ",
				  declaration_builder_.declarationCount(),
				  "/",
				  declaration_builder_.entityCount());
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

	std::array<DomainByteStats, 4> domain_stats_{};
	MonotonicScratchArena scratch_arena_;
	ScratchProbeRegistry scratch_registry_;
	DeclarationBuilder declaration_builder_;
	ScopeId current_scope_id_;
	std::size_t scope_count_ = 1;
};

inline FrontendContext* frontendContext() {
	return FrontendContext::active();
}

