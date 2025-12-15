#pragma once

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <cassert>
#include <cstring>
#include "ChunkedString.h"

/**
 * @file StringTable.h
 * @brief Global string interning system for FlashCpp IR optimization
 * 
 * This file implements a zero-allocation string handling system that replaces
 * std::string/std::string_view with compact 32-bit handles. Benefits:
 * - Reduces IrOperand size significantly
 * - Eliminates string copying and hashing during variable lookups
 * - Provides O(1) string reconstruction via handle
 */

/**
 * @brief Metadata stored before each string in the chunk allocator
 * 
 * Memory layout for each interned string:
 * [StringMetadata][String Content (N bytes)][\0]
 */
struct StringMetadata {
	uint64_t hash;    // Pre-computed FNV-1a hash
	uint32_t length;  // String length in bytes
	
	// Convenience methods
	const char* getContent() const {
		return reinterpret_cast<const char*>(this + 1);
	}
	
	char* getContent() {
		return reinterpret_cast<char*>(this + 1);
	}
	
	static constexpr size_t SIZE = sizeof(uint64_t) + sizeof(uint32_t);  // 12 bytes
} __attribute__((packed));

static_assert(sizeof(StringMetadata) == 12, "StringMetadata must be 12 bytes (packed)");

/**
 * @brief Lightweight 32-bit handle representing a string in the global allocator
 * 
 * Memory layout:
 * [31...26] (6 bits)  : Chunk Index (supports up to 64 chunks)
 * [25...0]  (26 bits) : Byte Offset within chunk (up to 64MB addressable per chunk)
 */
struct StringHandle {
	// Bit layout constants
	static constexpr uint32_t CHUNK_INDEX_BITS = 6;
	static constexpr uint32_t OFFSET_BITS = 26;
	static constexpr uint32_t MAX_CHUNK_INDEX = (1u << CHUNK_INDEX_BITS) - 1;  // 63
	static constexpr uint32_t MAX_OFFSET = (1u << OFFSET_BITS) - 1;  // 67108863 bytes (64MB)
	static constexpr uint32_t OFFSET_MASK = MAX_OFFSET;
	
	// Note: We add 1 to offset in constructor to reserve handle 0 as invalid,
	// which means the actual usable offset range is [0, MAX_OFFSET - 1]
	static constexpr uint32_t MAX_USABLE_OFFSET = MAX_OFFSET - 1;  // 67108862 bytes
	
	uint32_t handle = 0;  // Packed: chunk_index (high 8 bits) + offset (low 24 bits)

	// Default constructor creates invalid handle
	StringHandle() = default;

	// Construct from chunk index and offset
	explicit StringHandle(uint32_t chunk_idx, uint32_t offset) {
		assert(chunk_idx <= MAX_CHUNK_INDEX && "Chunk index must fit in 8 bits");
		assert(offset <= MAX_USABLE_OFFSET && "Offset exceeds usable range (need to reserve 0 as invalid)");
		// Add 1 to offset so that handle 0 is reserved as invalid
		handle = (chunk_idx << OFFSET_BITS) | (offset + 1);
	}

	// Extract chunk index (high 8 bits)
	uint32_t chunkIndex() const {
		return handle >> OFFSET_BITS;
	}

	// Extract offset (low 24 bits) - subtract 1 to get actual offset
	uint32_t offset() const {
		return (handle & OFFSET_MASK) - 1;
	}

	// Validity check - handle 0 is reserved as invalid
	bool isValid() const {
		return handle != 0;
	}

	// Comparison operators (for use in maps/sets)
	bool operator==(const StringHandle& other) const noexcept {
		return handle == other.handle;
	}

	bool operator!=(const StringHandle& other) const noexcept {
		return handle != other.handle;
	}

	bool operator<(const StringHandle& other) const noexcept {
		return handle < other.handle;
	}

	// Hash support for unordered containers
	size_t hash() const noexcept {
		// Identity hash - handle is already unique and well-distributed
		return static_cast<size_t>(handle);
	}
};

/**
 * @brief Global string table for interning and managing string handles
 * 
 * Memory layout for each interned string:
 * [Hash (8 bytes)] [Length (4 bytes)] [String Content (N bytes)] [\0]
 * 
 * This 12-byte overhead enables:
 * - O(1) hash retrieval (for fast map lookups)
 * - O(1) string_view reconstruction (length + pointer)
 * - Null-terminated string for C compatibility
 */
class StringTable {
public:
	/**
	 * @brief FNV-1a hash function (fast, good distribution)
	 */
	static uint64_t hashString(std::string_view str) {
		// FNV-1a constants for 64-bit hash
		constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
		constexpr uint64_t FNV_PRIME = 1099511628211ULL;

		uint64_t hash = FNV_OFFSET_BASIS;
		for (unsigned char c : str) {
			hash ^= static_cast<uint64_t>(c);
			hash *= FNV_PRIME;
		}
		return hash;
	}

	/**
	 * @brief Create a new string handle (does not check for duplicates)
	 * Use this for strings that are known to be unique.
	 * 
	 * Note: This uses findChunkIndex() which performs a linear search.
	 * Performance: O(n) where n = number of chunks. In practice, this is
	 * very fast because:
	 * 1. Number of chunks is typically small (few chunks even for large programs)
	 * 2. Default chunk size is 64MB, so even 1GB of strings = only 16 chunks
	 * 3. The search is cache-friendly (sequential pointer comparisons)
	 * 
	 * Future optimization: If profiling shows this is a bottleneck, consider
	 * caching the last chunk index or using a more sophisticated data structure.
	 */
	static StringHandle createStringHandle(std::string_view str) {
		// Allocate using safe placement new
		StringMetadata* metadata = gChunkedStringAllocator.allocateWithMetadata<StringMetadata>(str.size() + 1);
		
		// Find which chunk contains the allocated pointer (safe from race conditions)
		char* ptr = reinterpret_cast<char*>(metadata);
		size_t chunk_idx = gChunkedStringAllocator.findChunkIndex(ptr);
		assert(chunk_idx != SIZE_MAX && "Allocated pointer must be in a valid chunk");
		
		// Calculate offset within that chunk
		char* chunk_start = gChunkedStringAllocator.getChunkPointer(chunk_idx, 0);
		size_t offset = ptr - chunk_start;

		// Initialize metadata
		metadata->hash = hashString(str);
		metadata->length = static_cast<uint32_t>(str.size());
		
		// Write string content after metadata
		char* content = metadata->getContent();
		std::memcpy(content, str.data(), str.size());
		content[str.size()] = '\0';  // Null terminator

		return StringHandle(static_cast<uint32_t>(chunk_idx), static_cast<uint32_t>(offset));
	}

	/**
	 * @brief Get or create an interned string handle
	 * Returns existing handle if string is already interned, creates new one otherwise.
	 */
	static StringHandle getOrInternStringHandle(std::string_view str) {
		// Check if already interned
		auto it = getInternMap().find(str);
		if (it != getInternMap().end()) {
			return it->second;
		}

		// Create new handle
		StringHandle handle = createStringHandle(str);

		// Store in intern map (key is string_view pointing to the interned data)
		std::string_view interned_view = getStringView(handle);
		getInternMap()[interned_view] = handle;

		return handle;
	}

	/**
	 * @brief Resolve handle to string_view (O(1))
	 */
	static std::string_view getStringView(StringHandle handle) {
		assert(handle.isValid() && "Invalid StringHandle");
		
		char* ptr = gChunkedStringAllocator.getChunkPointer(
			handle.chunkIndex(), 
			handle.offset()
		);

		// Access metadata using struct
		const StringMetadata* metadata = reinterpret_cast<const StringMetadata*>(ptr);
		return std::string_view(metadata->getContent(), metadata->length);
	}

	/**
	 * @brief Get pre-computed hash for a handle (O(1))
	 */
	static uint64_t getHash(StringHandle handle) {
		assert(handle.isValid() && "Invalid StringHandle");
		
		char* ptr = gChunkedStringAllocator.getChunkPointer(
			handle.chunkIndex(), 
			handle.offset()
		);

		// Access hash from metadata using struct
		const StringMetadata* metadata = reinterpret_cast<const StringMetadata*>(ptr);
		return metadata->hash;
	}

	/**
	 * @brief Clear the intern map (useful for testing)
	 */
	static void clearInternMap() {
		getInternMap().clear();
	}

	/**
	 * @brief Get statistics about interned strings
	 */
	static size_t getInternedCount() {
		return getInternMap().size();
	}

private:
	// Singleton intern map: string_view -> StringHandle
	// Keys are string_views pointing to interned data in gChunkedStringAllocator
	static std::unordered_map<std::string_view, StringHandle>& getInternMap() {
		static std::unordered_map<std::string_view, StringHandle> intern_map;
		return intern_map;
	}
};

// Hash specialization for StringHandle (for use in unordered_map/set)
namespace std {
	template<>
	struct hash<StringHandle> {
		size_t operator()(const StringHandle& handle) const noexcept {
			return handle.hash();
		}
	};
}
