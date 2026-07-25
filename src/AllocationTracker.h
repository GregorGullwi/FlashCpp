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

class AllocationTracker {
public:
	static void setEnabled(bool enabled);
	static bool isEnabled();

	static void recordAllocation(std::size_t size);
	static void recordDeallocation(std::size_t size);

	static void printStats();

	struct Snapshot {
		uint64_t allocation_count = 0;
		uint64_t deallocation_count = 0;
		uint64_t bytes_allocated = 0;
		uint64_t bytes_deallocated = 0;
		uint64_t current_live_bytes = 0;
		uint64_t peak_live_bytes = 0;
	};

	static Snapshot snapshot();
};

} // namespace FlashCpp
