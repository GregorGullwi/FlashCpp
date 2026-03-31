#pragma once

#include <vector>
#include <string_view>
#include <string>
#include <algorithm>
#include <memory>
#include <cassert>
#include <cstring>
#include <exception>
#include <iostream>
#include <charconv>
#include <type_traits>

class Chunk {
public:
	explicit Chunk(size_t capacity)
		: data_(capacity), next_free_(0) {}

	bool has_space(size_t size) const {
		return next_free_ + size <= data_.size();
	}

	char* allocate(size_t size) {
		assert(has_space(size));
		char* ptr = data_.data() + next_free_;
		next_free_ += size;
		return ptr;
	}

	size_t remaining() const { return data_.size() - next_free_; }

	char* current_ptr() { return data_.data() + next_free_; }

private:
	std::vector<char> data_;
	size_t next_free_;

	friend class ChunkedStringAllocator;
};

class ChunkedStringAllocator {
public:
	explicit ChunkedStringAllocator(size_t chunk_size = 64 * 1024 * 1024) // 64 MB chunks
		: chunk_size_(chunk_size) {
		chunks_.emplace_back(std::make_unique<Chunk>(chunk_size_));
	}

	char* allocate(size_t size) {
		Chunk* chunk = current_chunk();
		if (!chunk->has_space(size)) {
			chunks_.emplace_back(std::make_unique<Chunk>(
				std::max(chunk_size_, size)));
			chunk = chunks_.back().get();
		}
		return chunk->allocate(size);
	}

	char* peek_allocate(size_t size) {
		Chunk* chunk = current_chunk();
		if (!chunk->has_space(size)) {
			chunks_.emplace_back(std::make_unique<Chunk>(
				std::max(chunk_size_, size)));
			chunk = chunks_.back().get();
		}
		return chunk->current_ptr();
	}

	bool tryFree(char* ptr, size_t size) {
		if (chunks_.empty())
			return false;

		Chunk* chunk = current_chunk();

		char* chunk_start = chunk->data_.data();
		char* chunk_end = chunk_start + chunk->data_.size();

		// Must belong to the current chunk
		if (ptr < chunk_start || ptr >= chunk_end)
			return false;

		// Must be the most recent allocation
		if (ptr + size != chunk->current_ptr())
			return false;

		// Rewind allocation
		chunk->next_free_ -= size;

		return true;
	}

	Chunk* current_chunk() { return chunks_.back().get(); }

	// StringTable support - get current chunk index (0-based)
	size_t getChunkIndex() const {
		return chunks_.size() - 1;
	}

	// StringTable support - get total number of chunks
	size_t getChunkCount() const {
		return chunks_.size();
	}

	// StringTable support - find which chunk contains a pointer
	// Returns the chunk index, or SIZE_MAX if pointer not found
	size_t findChunkIndex(const char* ptr) const {
		for (size_t i = 0; i < chunks_.size(); ++i) {
			const char* chunk_start = chunks_[i]->data_.data();
			const char* chunk_end = chunk_start + chunks_[i]->data_.size();
			if (ptr >= chunk_start && ptr < chunk_end) {
				return i;
			}
		}
		assert(false && "Expected chunk index to be found");
		return SIZE_MAX;	 // Not found
	}

	// StringTable support - resolve chunk index and offset to pointer
	char* getChunkPointer(size_t chunk_idx, size_t offset) const {
		assert(chunk_idx < chunks_.size() && "Invalid chunk index");
		return chunks_[chunk_idx]->data_.data() + offset;
	}

	// StringTable support - allocate and initialize string with metadata
	// This is safer than raw reinterpret_cast in user code
	template <typename MetadataType>
	MetadataType* allocateWithMetadata(size_t content_size) {
		static_assert(std::is_standard_layout_v<MetadataType>, "Metadata must be standard layout");
		size_t total_size = sizeof(MetadataType) + content_size;
		char* ptr = allocate(total_size);
		return new (ptr) MetadataType();	 // Placement new for safety
	}

private:
	std::vector<std::unique_ptr<Chunk>> chunks_;
	size_t chunk_size_;

	friend class StringBuilder;
	friend class StringTable;  // For StringTable access to chunks
};

extern ChunkedStringAllocator gChunkedStringAllocator;
