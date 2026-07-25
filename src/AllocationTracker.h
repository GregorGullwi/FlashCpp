#pragma once

#include <cstddef>
#include <cstdint>

#ifndef FLASHCPP_TRACK_ALLOCATIONS
#define FLASHCPP_TRACK_ALLOCATIONS 0
#endif

#ifndef FLASHCPP_TRACK_ALLOCATION_STACKS
#define FLASHCPP_TRACK_ALLOCATION_STACKS 0
#endif

#if FLASHCPP_TRACK_ALLOCATION_STACKS && !FLASHCPP_TRACK_ALLOCATIONS
#error FLASHCPP_TRACK_ALLOCATION_STACKS requires FLASHCPP_TRACK_ALLOCATIONS
#endif

namespace FlashCpp {

enum class AllocationPhase : uint8_t {
	Unknown,
	Preprocessing,
	LexerSetup,
	Parsing,
	SemanticAnalysis,
	IrConversion,
	DeferredGen,
	CodeGeneration,
	Other,
	Count
};

class AllocationTracker {
public:
	static void setEnabled(bool enabled);
	static bool isEnabled();

	static void setPhase(AllocationPhase phase);
	static AllocationPhase currentPhase();

	static void recordAllocation(std::size_t size);
	static void recordDeallocation(std::size_t size);

	static void printStats();

	struct PhaseSnapshot {
		uint64_t allocation_count = 0;
		uint64_t bytes_allocated = 0;
	};

	struct Snapshot {
		uint64_t allocation_count = 0;
		uint64_t deallocation_count = 0;
		uint64_t bytes_allocated = 0;
		uint64_t bytes_deallocated = 0;
		uint64_t current_live_bytes = 0;
		uint64_t peak_live_bytes = 0;
		PhaseSnapshot phases[static_cast<size_t>(AllocationPhase::Count)]{};
	};

	static Snapshot snapshot();
	static const char* phaseName(AllocationPhase phase);
};

} // namespace FlashCpp
