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
    explicit ChunkedStringAllocator(size_t chunk_size = 8 * 1024 * 1024) // 8 MB chunks
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

private:
    std::vector<std::unique_ptr<Chunk>> chunks_;
    size_t chunk_size_;

    friend class StringBuilder;
};

extern ChunkedStringAllocator gChunkedStringAllocator;

class StringBuilder {
public:
    explicit StringBuilder(ChunkedStringAllocator& allocator = gChunkedStringAllocator)
        : alloc_(allocator), chunk_(allocator.current_chunk()), buf_start_(chunk_->current_ptr()), write_ptr_(buf_start_) {}

    StringBuilder& append(std::string_view sv) {
        ensure_capacity(sv.size());
        std::memcpy(write_ptr_, sv.data(), sv.size());
        write_ptr_ += sv.size();
        return *this;
    }

    StringBuilder& append(char c) {
        ensure_capacity(1);
        *write_ptr_++ = c;
        return *this;
    }

    StringBuilder& append(int value) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(std::begin(buf), std::end(buf), value);
        if (ec == std::errc{}) {
            append(std::string_view(buf, ptr - buf));
        }
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

    std::string_view commit() {
        size_t len = write_ptr_ - buf_start_;
        append('\0');
        chunk_->allocate(len + 1);
        return std::string_view(buf_start_, len);
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
