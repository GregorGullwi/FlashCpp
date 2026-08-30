#pragma once

#include "ArenaDomains.h"
#include "ChunkedString.h"
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

	void refreshScratchDomainStats() {
		const std::size_t scratch_index = static_cast<std::size_t>(AllocationDomain::Scratch);
		domain_stats_[scratch_index].current_bytes = scratch_arena_.currentBytes();
		domain_stats_[scratch_index].peak_bytes = scratch_arena_.peakBytes();
		domain_stats_[scratch_index].reserved_bytes = scratch_arena_.reservedBytes();
		domain_stats_[scratch_index].peak_reserved_bytes = scratch_arena_.peakReservedBytes();
	}

	static uint64_t stringTableEntryCount() {
		return StringTable::getInternedCount();
	}

	static uint64_t stringTableSpellingBytes() {
		return gChunkedStringAllocator.allocatedBytes();
	}

	void printArenaTelemetry() {
		refreshScratchDomainStats();
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
	}

private:
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
};

inline FrontendContext* frontendContext() {
	return FrontendContext::active();
}
