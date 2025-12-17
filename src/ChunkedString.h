#pragma once

#include <vector>
#include <string_view>
#include <string>
#include <memory>
#include <cassert>
#include <cstring>
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
        return SIZE_MAX;  // Not found
    }

    // StringTable support - resolve chunk index and offset to pointer
    char* getChunkPointer(size_t chunk_idx, size_t offset) const {
        assert(chunk_idx < chunks_.size() && "Invalid chunk index");
        return chunks_[chunk_idx]->data_.data() + offset;
    }

    // StringTable support - allocate and initialize string with metadata
    // This is safer than raw reinterpret_cast in user code
    template<typename MetadataType>
    MetadataType* allocateWithMetadata(size_t content_size) {
        static_assert(std::is_standard_layout_v<MetadataType>, "Metadata must be standard layout");
        size_t total_size = sizeof(MetadataType) + content_size;
        char* ptr = allocate(total_size);
        return new (ptr) MetadataType();  // Placement new for safety
    }

private:
    std::vector<std::unique_ptr<Chunk>> chunks_;
    size_t chunk_size_;

    friend class StringBuilder;
    friend class StringTable;  // For StringTable access to chunks
};

extern ChunkedStringAllocator gChunkedStringAllocator;

// Global to track which StringBuilder is currently active (for detecting parallel usage in same scope)
// Using 'inline' instead of 'static' to ensure a single definition across all translation units
// (C++17 inline variables)
inline class StringBuilder* gCurrentStringBuilder = nullptr;

class StringBuilder {
public:
    // Initial capacity for the temporary buffer - tuned to reduce small allocations
    // while not wasting too much memory for short strings
    static constexpr size_t INITIAL_BUFFER_CAPACITY = 256;
    
    explicit StringBuilder(ChunkedStringAllocator& allocator = gChunkedStringAllocator)
        : alloc_(allocator),
          previous_builder_(gCurrentStringBuilder),  // Save the currently active builder (for nested support)
          is_committed_(false) {
        // Use temporary std::string for building - this makes nesting work naturally
        // since each StringBuilder has independent storage
        temp_buffer_.reserve(INITIAL_BUFFER_CAPACITY);
    }
    
    ~StringBuilder() {
        // Verify that commit() or reset() was called
        assert(is_committed_ && "did you forget to call commit() or reset() on the StringBuilder?");
        // Restore previous builder if this was the active one
        if (gCurrentStringBuilder == this) {
            gCurrentStringBuilder = previous_builder_;
        }
    }

    StringBuilder& append(std::string_view sv) {
        handle_activation();
        temp_buffer_.append(sv);
        return *this;
    }

    // Forward declaration - defined in StringTable.h to avoid circular dependency
    StringBuilder& append(struct StringHandle sh);

    StringBuilder& append(char c) {
        handle_activation();
        temp_buffer_.push_back(c);
        return *this;
    }

    StringBuilder& append(int64_t value) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(std::begin(buf), std::end(buf), value);
        if (ec == std::errc{}) {
            append(std::string_view(buf, ptr - buf));
        }
        return *this;
    }

    StringBuilder& append(uint64_t value) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(std::begin(buf), std::end(buf), value);
        if (ec == std::errc{}) {
            append(std::string_view(buf, ptr - buf));
        }
        return *this;
    }

    StringBuilder& operator+=(std::string_view sv) {
        return append(sv);
    }

    StringBuilder& operator+=(char c) {
        return append(c);
    }

    StringBuilder& operator+=(int64_t value) {
        return append(value);
    }

    StringBuilder& operator+=(uint64_t value) {
        return append(value);
    }

    std::string_view commit() {
        // Copy the temporary buffer to the global allocator
        char* ptr = alloc_.allocate(temp_buffer_.size() + 1);
        std::memcpy(ptr, temp_buffer_.data(), temp_buffer_.size());
        ptr[temp_buffer_.size()] = '\0';
        std::string_view result(ptr, temp_buffer_.size());
        
        // Clear the temporary buffer and mark as committed
        temp_buffer_.clear();
        is_committed_ = true;
        
        // Restore previous builder
        if (gCurrentStringBuilder == this) {
            gCurrentStringBuilder = previous_builder_;
        }
        
        return result;
    }

    std::string_view preview() const {
        return std::string_view(temp_buffer_);
    }

    void reset() {
        temp_buffer_.clear();
        is_committed_ = true;
        
        // Restore previous builder
        if (gCurrentStringBuilder == this) {
            gCurrentStringBuilder = previous_builder_;
        }
    }

private:
    void handle_activation() {
        // Detect parallel usage: if there's already an active builder and it's NOT our parent
        // (i.e., not the previous_builder_), then we have parallel usage in the same scope
        if (gCurrentStringBuilder != nullptr && 
            gCurrentStringBuilder != this && 
            gCurrentStringBuilder != previous_builder_) {
            // Parallel usage detected - this is an error
            assert(false && "Parallel StringBuilder usage detected in the same scope! "
                           "You have two StringBuilders being used at the same time. "
                           "Call .commit() or .reset() on the first builder before using the second one.");
        }
        
        // Set this builder as the current one (previous_builder_ was already saved in constructor)
        // This is safe for nested usage since the inner builder will restore gCurrentStringBuilder
        // when it commits/resets
        if (gCurrentStringBuilder != this) {
            gCurrentStringBuilder = this;
        }
    }

    ChunkedStringAllocator& alloc_;
    std::string temp_buffer_;  // Temporary storage - independent for each StringBuilder
    StringBuilder* previous_builder_;  // Stack of nested StringBuilders
    bool is_committed_;  // Whether commit() or reset() was called
};
