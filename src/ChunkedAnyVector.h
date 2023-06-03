#include <iostream>
#include <vector>
#include <list>
#include <memory_resource>
#include <typeinfo>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <algorithm>
#include <assert.h>

template<std::size_t ChunkSize = 1, std::size_t InternalBufferSize = sizeof(std::pmr::list<std::pmr::vector<char>>) + 4 * sizeof(void*)>
class ChunkedAnyVector {
public:
	ChunkedAnyVector()
		: memory_resource(internal_buffer.data(), internal_buffer.max_size()),
		data(&memory_resource) {}

	template<typename T>
	T& push_back(T&& value) {
		std::size_t size = sizeof(T);
		std::size_t alignment = alignof(T);
		if (data.empty() || data.back().size() + size > ChunkSize) {
			data.emplace_back().reserve(ChunkSize);
		}
		auto& chunk = data.back();
		std::size_t offset = (chunk.size() + alignment - 1) & ~(alignment - 1);
		chunk.resize(offset + size);
		T* p = new (&chunk[offset]) T(std::forward<T>(value));
		index_to_pointer.emplace(index++, p);
		pointer_to_type.emplace(p, std::type_index(typeid(T)));
		return *p;
	}

	template<typename F>
	void visit(F&& f) const {
		for (const auto& [index, pointer] : index_to_pointer) {
			auto it = pointer_to_type.find(pointer);
			assert(it != pointer_to_type.end());
			std::type_index type = it->second;
			f(pointer, type);
		}
	}

private:
	std::array<char, InternalBufferSize> internal_buffer;
	std::pmr::monotonic_buffer_resource memory_resource;
	std::pmr::list<std::pmr::vector<char>> data;
	std::unordered_map<std::size_t, void*> index_to_pointer;
	std::unordered_map<void*, std::type_index> pointer_to_type;
	std::size_t index = 0;
};
