#include "ArenaDomains.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <new>

namespace {

std::size_t alignUp(std::size_t value, std::size_t alignment) {
	return (value + alignment - 1U) & ~(alignment - 1U);
}

} // namespace

uint64_t MonotonicScratchArena::reservedBytes() const {
	uint64_t total = 0;
	for (const Block& block : blocks_) {
		total += block.data.size();
	}
	return total;
}

void* MonotonicScratchArena::allocate(std::size_t size, std::size_t alignment) {
	if (size == 0) {
		return nullptr;
	}
	if (alignment == 0 || (alignment & (alignment - 1U)) != 0) {
		assert(false && "MonotonicScratchArena alignment must be a power of two");
		return nullptr;
	}

	if (blocks_.empty()) {
		const std::size_t initial_capacity = std::max<std::size_t>(4096U, size + alignment);
		blocks_.push_back(Block{std::vector<std::byte>(initial_capacity), 0});
	}

	for (;;) {
		Block& block = blocks_.back();
		const std::size_t aligned_used = alignUp(block.used, alignment);
		if (aligned_used + size <= block.data.size()) {
			const std::size_t allocated_bytes = (aligned_used + size) - block.used;
			void* result = block.data.data() + aligned_used;
			block.used = aligned_used + size;
			current_bytes_ += allocated_bytes;
			peak_bytes_ = std::max(peak_bytes_, current_bytes_);
			peak_reserved_bytes_ = std::max(peak_reserved_bytes_, reservedBytes());
			return result;
		}

		const std::size_t block_capacity = std::max<std::size_t>(4096U, size + alignment);
		blocks_.push_back(Block{std::vector<std::byte>(block_capacity), 0});
		peak_reserved_bytes_ = std::max(peak_reserved_bytes_, reservedBytes());
	}
}

ScratchArenaState MonotonicScratchArena::mark() const {
	ScratchArenaState state;
	state.block_index = blocks_.empty() ? 0U : blocks_.size() - 1U;
	state.block_used = blocks_.empty() ? 0U : blocks_.back().used;
	state.destructor_count = destructors_.size();
	return state;
}

void MonotonicScratchArena::rollbackTo(ScratchArenaState state) {
	const uint64_t bytes_before = current_bytes_;

	runDestructorsFrom(state.destructor_count);

	while (blocks_.size() > state.block_index + 1U) {
		current_bytes_ -= blocks_.back().used;
		blocks_.pop_back();
	}

	if (!blocks_.empty() && state.block_index < blocks_.size()) {
		Block& block = blocks_[state.block_index];
		if (state.block_used <= block.used) {
			current_bytes_ -= block.used - state.block_used;
			block.used = state.block_used;
		}
	}

	if (bytes_before > current_bytes_) {
		discarded_bytes_ += bytes_before - current_bytes_;
	}
}

void MonotonicScratchArena::runDestructorsFrom(std::size_t first_index) {
	for (std::size_t index = destructors_.size(); index > first_index; --index) {
		DestructorEntry& entry = destructors_[index - 1U];
		entry.destroy(entry.object);
	}
	destructors_.resize(first_index);
}

uint32_t ScratchProbeRegistry::registerEntry() {
	const uint32_t id = next_id_++;
	entries_.push_back(id);
	return id;
}

ScratchRegistryState ScratchProbeRegistry::mark() const {
	return ScratchRegistryState{
		.live_count = entries_.size(),
		.next_id = next_id_,
		.committed_count = committed_count_,
	};
}

void ScratchProbeRegistry::rollbackTo(ScratchRegistryState state) {
	entries_.resize(state.live_count);
	next_id_ = state.next_id;
	committed_count_ = state.committed_count;
}

void ScratchProbeRegistry::commitToCurrent() {
	committed_count_ = entries_.size();
}

ScratchTransaction::ScratchTransaction(MonotonicScratchArena& arena, ScratchProbeRegistry& registry)
	: arena_(arena)
	, registry_(registry)
	, arena_state_(arena.mark())
	, registry_state_(registry.mark()) {
}

ScratchTransaction::~ScratchTransaction() {
	if (!committed_ && !rolled_back_) {
		rollback();
	}
}

void ScratchTransaction::commit() {
	registry_.commitToCurrent();
	committed_ = true;
}

void ScratchTransaction::rollback() {
	registry_.rollbackTo(registry_state_);
	arena_.rollbackTo(arena_state_);
	rolled_back_ = true;
}
