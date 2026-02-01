#pragma once

#include <iostream>
#include <vector>
#include <list>
#include <typeinfo>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <array>
#include <assert.h>
#include <deque>
#include <string>
#include <cstdint>

#ifdef __has_include
#if __has_include(<memory_resource>)
#define HAS_MEMORY_RESOURCE 1
#endif
#endif

#ifdef HAS_MEMORY_RESOURCE
#include <memory_resource>
#define DEQUE_SIZE_ANY sizeof(std::pmr::deque<std::pmr::vector<char>>)
#define DEQUE_SIZE_T sizeof(std::pmr::deque<std::pmr::vector<T>>)
#else
#define DEQUE_SIZE_ANY sizeof(std::deque<std::vector<char>>)
#define DEQUE_SIZE_T sizeof(std::deque<std::vector<T>>)
#endif

template<uint32_t ChunkSize = 64 * 1024 * 1024, uint32_t InternalBufferSize = static_cast<uint32_t>(DEQUE_SIZE_ANY + 4 * sizeof(void*))>
class ChunkedAnyVector {
public:
	ChunkedAnyVector()
#ifdef HAS_MEMORY_RESOURCE
    : memory_resource(internal_buffer.data(), internal_buffer.max_size()),
    data(&memory_resource)
#endif
    {}

	template<typename T>
	T& push_back(T&& value) {
		return emplace_back<T>(std::forward<T>(value));
	}

	template<typename T, typename... Args>
	T& emplace_back(Args&&... args) {
		std::size_t size = sizeof(T);
		std::size_t alignment = alignof(T);
		if (data.empty() || data.back().size() + size > ChunkSize) {
			data.emplace_back().reserve(ChunkSize);
		}
		auto& chunk = data.back();
		std::size_t offset = (chunk.size() + alignment - 1) & ~(alignment - 1);

		// Ensure we don't exceed the reserved capacity
		if (offset + size > chunk.capacity()) {
			// Start a new chunk if this one can't fit the object
			data.emplace_back().reserve(ChunkSize);
			auto& new_chunk = data.back();
			offset = (new_chunk.size() + alignment - 1) & ~(alignment - 1);
			new_chunk.resize(offset + size);
			std::decay_t<T>* p = new (&new_chunk[offset]) std::decay_t<T>(std::forward<Args>(args)...);
			index_to_pointer.emplace_back(p);
			index_to_type.emplace_back(typeid(T));
			return *p;
		}

		chunk.resize(offset + size);
		std::decay_t<T>* p = new (&chunk[offset]) std::decay_t<T>(std::forward<Args>(args)...);
		index_to_pointer.emplace_back(p);
		index_to_type.emplace_back(typeid(T));
		return *p;
	}

	template<typename F>
	void visit(F&& f) const {
		for (std::size_t i = 0; i < index_to_pointer.size(); ++i) {
			void* pointer = index_to_pointer[i];
			std::type_index type = index_to_type[i];
			f(pointer, type);
		}
	}

private:
	std::array<char, InternalBufferSize> internal_buffer;
#ifdef HAS_MEMORY_RESOURCE
    std::pmr::monotonic_buffer_resource memory_resource;
    std::pmr::deque<std::pmr::vector<char>> data;
#else
    std::deque<std::vector<char>> data;
#endif
	std::vector<void*> index_to_pointer;
	std::vector<std::type_index> index_to_type;
};

template<typename T, uint32_t ChunkSize = static_cast<uint32_t>(sizeof(T) * 4), uint32_t InternalBufferSize = static_cast<uint32_t>(DEQUE_SIZE_T) + ChunkSize>
class ChunkedVector {
public:
	ChunkedVector()
#ifdef HAS_MEMORY_RESOURCE
		: memory_resource(internal_buffer.data(), internal_buffer.max_size()),
		data(&memory_resource)
#endif
    {}

	ChunkedVector(std::initializer_list<T> init)
#ifdef HAS_MEMORY_RESOURCE
		: memory_resource(internal_buffer.data(), internal_buffer.max_size()),
		data(&memory_resource)
#endif
    {
		for (const auto& value : init) {
			push_back(value);
		}
	}

	ChunkedVector(const ChunkedVector& other)
#ifdef HAS_MEMORY_RESOURCE
		: memory_resource(internal_buffer.data(), internal_buffer.max_size()),
		data(&memory_resource)
#endif
    {
		// Note: std::deque doesn't have reserve(), but that's okay
		// It will allocate blocks as needed
		for (size_t i = 0, e = other.size(); i < e; ++i) {
			push_back(other[i]);
		}
	}

	ChunkedVector(ChunkedVector&& other) noexcept
#ifdef HAS_MEMORY_RESOURCE
		: memory_resource(internal_buffer.data(), internal_buffer.max_size()),
		data(std::move(other.data), &memory_resource)
#else
        : data(std::move(other.data))
#endif
    {}

	ChunkedVector& operator=(const ChunkedVector& other) {
		if (this != &other) {
			data.clear();
			// Note: std::deque doesn't have reserve(), but that's okay
			// It will allocate blocks as needed
			for (size_t i = 0, e = other.size(); i < e; ++i) {
				push_back(other[i]);
			}
		}
		return *this;
	}

	ChunkedVector& operator=(ChunkedVector&& other) noexcept {
		if (this != &other) {
			data = std::move(other.data);
		}
		return *this;
	}

	T& push_back(T&& value) {
		return emplace_back(std::move(value));
	}

	T& push_back(const T& value) {
		return emplace_back(value);
	}

	template<typename... Args>
	T& emplace_back(Args&&... args) {
		if (data.empty() || data.back().size() == ChunkSize) {
			data.emplace_back().reserve(ChunkSize);
		}
		auto& chunk = data.back();
		chunk.emplace_back(std::forward<Args>(args)...);
		return chunk.back();
	}

	T& operator[](std::size_t index) {
		// Precondition: data must not be empty and index must be valid
		assert(!data.empty() && "ChunkedVector::operator[] called on empty container");

		// O(1) access: calculate chunk and element indices directly
		std::size_t chunk_idx = index / ChunkSize;
		std::size_t elem_idx = index % ChunkSize;
		return data[chunk_idx][elem_idx];
	}

	const T& operator[](std::size_t index) const {
		// Precondition: data must not be empty and index must be valid
		assert(!data.empty() && "ChunkedVector::operator[] called on empty container");

		// O(1) access: calculate chunk and element indices directly
		std::size_t chunk_idx = index / ChunkSize;
		std::size_t elem_idx = index % ChunkSize;
		return data[chunk_idx][elem_idx];
	}

	size_t size() const {
		if (data.size() == 0) return 0;
		return (data.size() - 1) * ChunkSize + data.back().size();
	}

	bool empty() const {
		return data.empty() || (data.size() == 1 && data.front().empty());
	}

	void clear() {
		data.clear();
	}

	// Note: ChunkedVector doesn't provide data() because elements are not contiguous
	// Use operator[], iterators, or visit() instead

	template<typename Visitor>
	void visit(Visitor&& visitor) {
		for (const auto& chunk : data) {
			for (const auto& element : chunk) {
				visitor(element);
			}
		}
	}

  template<typename Visitor>
	void visit(Visitor&& visitor) const {
		for (const auto& chunk : data) {
			for (const auto& element : chunk) {
				visitor(element);
			}
		}
	}

#ifdef HAS_MEMORY_RESOURCE
	using DataType = std::pmr::deque<std::pmr::vector<T>>;
#else
	using DataType = std::deque<std::vector<T>>;
#endif

	template<bool IsConst>
	class iterator_impl {
	public:
		using iterator_category = std::forward_iterator_tag;
		using value_type = std::conditional_t<IsConst, const T, T>;
		using difference_type = std::ptrdiff_t;
		using pointer = std::conditional_t<IsConst, const T*, T*>;
		using reference = std::conditional_t<IsConst, const T&, T&>;
		using data_ptr_t = std::conditional_t<IsConst, const DataType*, DataType*>;

		iterator_impl() = default;
		iterator_impl(data_ptr_t d, uint32_t chunk_idx, uint32_t elem_idx)
			: data_(d), chunk_idx_(chunk_idx), elem_idx_(elem_idx) {
			if (data_) {
				total_chunks_ = static_cast<uint32_t>(data_->size());
				if (chunk_idx_ < total_chunks_) {
					chunk_size_ = static_cast<uint32_t>((*data_)[chunk_idx_].size());
				}
			}
		}

		reference operator*() const { return (*data_)[chunk_idx_][elem_idx_]; }
		pointer operator->() const { return &(*data_)[chunk_idx_][elem_idx_]; }

		iterator_impl& operator++() {
			++elem_idx_;
			if (elem_idx_ >= chunk_size_) {
				elem_idx_ = 0;
				++chunk_idx_;
				chunk_size_ = (chunk_idx_ < total_chunks_) ? static_cast<uint32_t>((*data_)[chunk_idx_].size()) : 0;
			}
			return *this;
		}

		iterator_impl operator++(int) {
			iterator_impl tmp = *this;
			++(*this);
			return tmp;
		}

		bool operator==(const iterator_impl& other) const {
			return data_ == other.data_ && chunk_idx_ == other.chunk_idx_ && elem_idx_ == other.elem_idx_;
		}
		bool operator!=(const iterator_impl& other) const { return !(*this == other); }

		bool operator==(std::default_sentinel_t) const {
			if (!data_) return true;
			assert(total_chunks_ == data_->size() && "Container modified during iteration");
			return chunk_idx_ >= total_chunks_ || (chunk_idx_ + 1 == total_chunks_ && elem_idx_ >= chunk_size_);
		}
		bool operator!=(std::default_sentinel_t s) const { return !(*this == s); }

	private:
		data_ptr_t data_ = nullptr;
		uint32_t chunk_idx_ = 0;
		uint32_t elem_idx_ = 0;
		uint32_t chunk_size_ = 0;
		uint32_t total_chunks_ = 0;
	};

	using iterator = iterator_impl<false>;
	using const_iterator = iterator_impl<true>;

	iterator begin() { return iterator(&data, 0, 0); }
	std::default_sentinel_t end() { return std::default_sentinel; }
	const_iterator begin() const { return const_iterator(&data, 0, 0); }
	std::default_sentinel_t end() const { return std::default_sentinel; }

private:
	std::array<char, InternalBufferSize> internal_buffer;
#ifdef HAS_MEMORY_RESOURCE
	std::pmr::monotonic_buffer_resource memory_resource;
	std::pmr::deque<std::pmr::vector<T>> data;
#else
    std::deque<std::vector<T>> data;
#endif
};

extern ChunkedAnyVector<> gChunkedAnyStorage;
