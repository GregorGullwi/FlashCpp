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

#ifdef __has_include
#if __has_include(<memory_resource>)
#define HAS_MEMORY_RESOURCE 1
#endif
#endif

#ifdef HAS_MEMORY_RESOURCE
#include <memory_resource>
#define LIST_SIZE_ANY sizeof(std::pmr::list<std::pmr::vector<char>>)
#define LIST_SIZE_T sizeof(std::pmr::list<std::pmr::vector<T>>)
#else
#define LIST_SIZE_ANY sizeof(std::list<std::vector<char>>)
#define LIST_SIZE_T sizeof(std::list<std::vector<T>>)
#endif

template<std::size_t ChunkSize = 64 * 1024 * 1024, std::size_t InternalBufferSize = LIST_SIZE_ANY + 4 * sizeof(void*)>
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
    std::pmr::list<std::pmr::vector<char>> data;
#else
    std::list<std::vector<char>> data;
#endif
	std::vector<void*> index_to_pointer;
	std::vector<std::type_index> index_to_type;
};

template<typename T, std::size_t ChunkSize = sizeof(T) * 4, std::size_t InternalBufferSize = LIST_SIZE_T + ChunkSize>
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
		for (auto& chunk : data) {
			if (index < chunk.size()) {
				return chunk[index];
			}
			index -= chunk.size();
		}
		throw std::out_of_range("Index out of range");
	}

	const T& operator[](std::size_t index) const {
		for (const auto& chunk : data) {
			if (index < chunk.size()) {
				return chunk[index];
			}
			index -= chunk.size();
		}
		throw std::out_of_range("Index out of range");
	}

	size_t size() const {
		if (data.size() == 0) return 0;
		return (data.size() - 1) * ChunkSize + data.back().size();
	}

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

private:
	std::array<char, InternalBufferSize> internal_buffer;
#ifdef HAS_MEMORY_RESOURCE
	std::pmr::monotonic_buffer_resource memory_resource;
	std::pmr::list<std::pmr::vector<T>> data;
#else
    std::list<std::vector<T>> data;
#endif
};
