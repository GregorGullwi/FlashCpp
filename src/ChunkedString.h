#pragma once

#include <vector>
#include <string_view>
#include <string>
#include <memory>
#include <cassert>
#include <cstring>
#include <iostream>
#include <charconv>

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
    
    // Check if a string_view points to memory managed by this allocator
    bool owns_string(std::string_view sv) const {
        const char* ptr = sv.data();
        
        // Handle edge cases
        if (!ptr || sv.empty()) return false;
        
        // Avoid potential overflow: reject unreasonably large strings
        // (practical chunk sizes are much smaller than SIZE_MAX/2)
        if (sv.size() > SIZE_MAX / 2) {
            return false;
        }
        
        const char* end = ptr + sv.size();
        for (const auto& chunk : chunks_) {
            const char* chunk_start = chunk->data_.data();
            const char* chunk_end = chunk_start + chunk->next_free_;
            // Check that both start and end of the string are within chunk bounds
            if (ptr >= chunk_start && end <= chunk_end) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::unique_ptr<Chunk>> chunks_;
    size_t chunk_size_;

    friend class StringBuilder;
};

extern ChunkedStringAllocator gChunkedStringAllocator;

// Global to track which StringBuilder is currently active (for detecting overlapping usage)
// Using 'inline' instead of 'static' to ensure a single definition across all translation units
// (C++17 inline variables)
inline class StringBuilder* gCurrentStringBuilder = nullptr;

class StringBuilder {
public:
    explicit StringBuilder(ChunkedStringAllocator& allocator = gChunkedStringAllocator)
        : alloc_(allocator), chunk_(allocator.current_chunk()), buf_start_(chunk_->current_ptr()), write_ptr_(buf_start_) {}
    ~StringBuilder() {
        assert(buf_start_ == write_ptr_ && "did you forget to call commit() on the StringBuilder?");
    }

    StringBuilder& append(std::string_view sv) {
        assert((gCurrentStringBuilder == nullptr || gCurrentStringBuilder == this) && "More than one StringBuilder in the same scope detected. Call .commit() or .reset() before you start with the next string!");
        gCurrentStringBuilder = this;
        ensure_capacity(sv.size());
        std::memcpy(write_ptr_, sv.data(), sv.size());
        write_ptr_ += sv.size();
        return *this;
    }

    StringBuilder& append(char c) {
        assert((gCurrentStringBuilder == nullptr || gCurrentStringBuilder == this) && "More than one StringBuilder in the same scope detected. Call .commit() or .reset() before you start with the next string!");
        gCurrentStringBuilder = this;
        ensure_capacity(1);
        *write_ptr_++ = c;
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
        size_t len = write_ptr_ - buf_start_;
        // Add null terminator directly (don't use append() which would set gCurrentStringBuilder)
        ensure_capacity(1);
        *write_ptr_++ = '\0';
        chunk_->allocate(len + 1);
        std::string_view return_str(buf_start_, len);
        buf_start_ = write_ptr_ = chunk_->current_ptr();
        gCurrentStringBuilder = nullptr;  // Reset AFTER all operations
        return return_str;
    }

    std::string_view preview() {
        size_t len = write_ptr_ - buf_start_;
        return std::string_view(buf_start_, len);
    }

    void reset() {
        gCurrentStringBuilder = nullptr;
        buf_start_ = write_ptr_ = chunk_->current_ptr();
    }

private:
    void ensure_capacity(size_t needed) {
        size_t len = write_ptr_ - buf_start_;
        size_t new_len = len + needed;
        if (chunk_->remaining() < new_len)
        {
            char* new_buf_start = alloc_.peek_allocate(new_len);
            std::memcpy(new_buf_start, buf_start_, len);
            write_ptr_ = new_buf_start + len;
            buf_start_ = new_buf_start;
            chunk_ = alloc_.current_chunk();
        }
    }

    ChunkedStringAllocator& alloc_;
    Chunk* chunk_;
    char* buf_start_ = nullptr;
    char* write_ptr_ = nullptr;
};
