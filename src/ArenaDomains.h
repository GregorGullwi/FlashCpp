#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "ArenaAccounting.h"

class DiagnosticEngine;

enum class AllocationDomain : uint8_t {
	Syntax,
	Semantic,
	Scratch,
	Ir,
};

struct ScratchArenaState {
	std::size_t block_index = 0;
	std::size_t block_used = 0;
	std::size_t destructor_count = 0;
};

struct ScratchRegistryState {
	std::size_t live_count = 0;
	uint32_t next_id = 1;
	std::size_t committed_count = 0;
};

class MonotonicScratchArena {
public:
	// The engine must outlive the arena and rendering of its limit diagnostics.
	MonotonicScratchArena(DiagnosticEngine& diagnostics, uint64_t byte_limit)
		: diagnostics_(diagnostics), byte_limit_(byte_limit) {}
	~MonotonicScratchArena() {
		runDestructorsFrom(0);
	}

	MonotonicScratchArena(const MonotonicScratchArena&) = delete;
	MonotonicScratchArena& operator=(const MonotonicScratchArena&) = delete;
	MonotonicScratchArena(MonotonicScratchArena&&) = delete;
	MonotonicScratchArena& operator=(MonotonicScratchArena&&) = delete;

	void* allocate(std::size_t size, std::size_t alignment);

	template<typename T, typename... Args>
	T* allocateObject(Args&&... args) {
		void* storage = allocate(sizeof(T), alignof(T));
		T* object = new (storage) T(std::forward<Args>(args)...);
		if constexpr (!std::is_trivially_destructible_v<T>) {
			registerDestructor(object);
		}
		return object;
	}

	ScratchArenaState mark() const;
	void rollbackTo(ScratchArenaState state);

	uint64_t currentBytes() const { return current_bytes_; }
	uint64_t peakBytes() const { return peak_bytes_; }
	uint64_t reservedBytes() const;
	uint64_t peakReservedBytes() const { return peak_reserved_bytes_; }
	uint64_t discardedBytes() const { return discarded_bytes_; }
	uint64_t byteLimit() const { return byte_limit_; }

private:
	struct Block {
		std::vector<std::byte> data;
		std::size_t used = 0;
	};

	struct DestructorEntry {
		void (*destroy)(void*);
		void* object;
	};

	template<typename T>
	void registerDestructor(T* object) {
		destructors_.push_back(DestructorEntry{
			[](void* ptr) { static_cast<T*>(ptr)->~T(); },
			object});
	}

	void runDestructorsFrom(std::size_t first_index);
	[[noreturn]] void reportAllocationLimit();

	DiagnosticEngine& diagnostics_;
	// Bounds payload/reservation and cumulative allocation work, not container metadata.
	const uint64_t byte_limit_;
	std::vector<Block> blocks_;
	std::vector<DestructorEntry> destructors_;
	uint64_t current_bytes_ = 0;
	uint64_t peak_bytes_ = 0;
	uint64_t peak_reserved_bytes_ = 0;
	uint64_t discarded_bytes_ = 0;
};

// Probe registry used by scratch transactions. Committed entries survive failed
// probes; entries registered during an open transaction roll back on failure.
class ScratchProbeRegistry {
public:
	ScratchProbeRegistry() = default;
	ScratchProbeRegistry(const ScratchProbeRegistry&) = delete;
	ScratchProbeRegistry& operator=(const ScratchProbeRegistry&) = delete;

	uint32_t registerEntry();
	ScratchRegistryState mark() const;
	void rollbackTo(ScratchRegistryState state);
	void commitToCurrent();

	std::size_t committedCount() const { return committed_count_; }
	std::size_t liveCount() const { return entries_.size(); }

private:
	std::vector<uint32_t> entries_;
	std::size_t committed_count_ = 0;
	uint32_t next_id_ = 1;
};

class ScratchTransaction {
public:
	ScratchTransaction(MonotonicScratchArena& arena, ScratchProbeRegistry& registry);
	~ScratchTransaction();

	ScratchTransaction(const ScratchTransaction&) = delete;
	ScratchTransaction& operator=(const ScratchTransaction&) = delete;

	void commit();
	void rollback();

	MonotonicScratchArena& arena() { return arena_; }
	ScratchProbeRegistry& registry() { return registry_; }

private:
	MonotonicScratchArena& arena_;
	ScratchProbeRegistry& registry_;
	ScratchArenaState arena_state_;
	ScratchRegistryState registry_state_;
	bool committed_ = false;
	bool rolled_back_ = false;
};
