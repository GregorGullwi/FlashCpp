#pragma once

#include <iostream>
#include <vector>
#include <list>
#include <memory_resource>
#include <typeinfo>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <array>
#include <assert.h>

template<std::size_t ChunkSize = 64 * 1024 * 1024, std::size_t InternalBufferSize = sizeof(std::pmr::list<std::pmr::vector<char>>) + 4 * sizeof(void*)>
class ChunkedAnyVector {
public:
	ChunkedAnyVector()
		: memory_resource(internal_buffer.data(), internal_buffer.max_size()),
		data(&memory_resource) {}

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
	std::pmr::monotonic_buffer_resource memory_resource;
	std::pmr::list<std::pmr::vector<char>> data;
	std::vector<void*> index_to_pointer;
	std::vector<std::type_index> index_to_type;
};

template<typename T, std::size_t ChunkSize = sizeof(T) * 4, std::size_t InternalBufferSize = sizeof(std::pmr::list<std::pmr::vector<T>>) + ChunkSize>
class ChunkedVector {
public:
	ChunkedVector()
		: memory_resource(internal_buffer.data(), internal_buffer.max_size()),
		data(&memory_resource) {}

	T& push_back(T&& value) {
		return emplace_back(std::forward<T>(value));
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

private:
	std::array<char, InternalBufferSize> internal_buffer;
	std::pmr::monotonic_buffer_resource memory_resource;
	std::pmr::list<std::pmr::vector<T>> data;
};
