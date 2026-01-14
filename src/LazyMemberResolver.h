// LazyMemberResolver.h - Phase 2: Lazy member resolution with caching
// Implements the member resolution strategy from KNOWN_ISSUES.md Phase 2

#pragma once

#include "AstNodeTypes.h"
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <queue>

namespace FlashCpp {

// Result of member lookup with full context
struct MemberResolutionResult {
	const StructMember* member;          // The resolved member (nullptr if not found)
	const StructTypeInfo* owner_struct;  // The struct that owns the member
	size_t adjusted_offset;              // Offset adjusted for inheritance
	bool from_cache;                     // Whether this result came from cache
	
	MemberResolutionResult() 
		: member(nullptr), owner_struct(nullptr), adjusted_offset(0), from_cache(false) {}
	
	MemberResolutionResult(const StructMember* m, const StructTypeInfo* owner, size_t offset, bool cached = false)
		: member(m), owner_struct(owner), adjusted_offset(offset), from_cache(cached) {}
	
	explicit operator bool() const { return member != nullptr; }
};

// Cache key for member lookups
struct MemberLookupKey {
	TypeIndex type_index;
	StringHandle member_name;
	
	bool operator==(const MemberLookupKey& other) const {
		return type_index == other.type_index && member_name == other.member_name;
	}
};

// Hash function for MemberLookupKey
struct MemberLookupKeyHash {
	size_t operator()(const MemberLookupKey& key) const {
		// Combine hashes
		size_t h1 = std::hash<TypeIndex>{}(key.type_index);
		size_t h2 = std::hash<StringHandle>{}(key.member_name);
		return h1 ^ (h2 << 1);
	}
};

// Phase 2: Lazy member resolver with caching and cycle detection
class LazyMemberResolver {
private:
	// Cache of resolved members
	std::unordered_map<MemberLookupKey, MemberResolutionResult, MemberLookupKeyHash> cache_;
	
	// Types currently being resolved (for cycle detection)
	std::unordered_set<MemberLookupKey, MemberLookupKeyHash> in_progress_;
	
	// Statistics for debugging/profiling
	size_t cache_hits_ = 0;
	size_t cache_misses_ = 0;
	size_t cycles_detected_ = 0;

public:
	// Resolve a member with caching and cycle detection
	MemberResolutionResult resolve(TypeIndex type_index, StringHandle member_name) {
		MemberLookupKey key{type_index, member_name};
		
		// Check cache first
		auto cache_it = cache_.find(key);
		if (cache_it != cache_.end()) {
			++cache_hits_;
			auto result = cache_it->second;
			result.from_cache = true;
			return result;
		}
		
		++cache_misses_;
		
		// Check for cycles
		if (in_progress_.contains(key)) {
			++cycles_detected_;
			return MemberResolutionResult();  // Cycle detected, return not found
		}
		
		// Mark as in progress
		in_progress_.insert(key);
		
		// Perform the actual resolution
		MemberResolutionResult result = resolveInternal(type_index, member_name);
		
		// Remove from in-progress
		in_progress_.erase(key);
		
		// Cache the result (even if not found, to avoid repeated lookups)
		cache_[key] = result;
		
		return result;
	}
	
	// Clear the cache (useful for testing or when type system changes)
	void clearCache() {
		cache_.clear();
		cache_hits_ = 0;
		cache_misses_ = 0;
		cycles_detected_ = 0;
	}
	
	// Get cache statistics
	struct Statistics {
		size_t cache_hits;
		size_t cache_misses;
		size_t cycles_detected;
		size_t cache_size;
		
		double hit_rate() const {
			size_t total = cache_hits + cache_misses;
			return total > 0 ? static_cast<double>(cache_hits) / static_cast<double>(total) : 0.0;
		}
	};
	
	Statistics getStatistics() const {
		return Statistics{cache_hits_, cache_misses_, cycles_detected_, cache_.size()};
	}

private:
	// Internal resolution logic
	MemberResolutionResult resolveInternal(TypeIndex type_index, StringHandle member_name) {
		// Validate type index
		if (type_index >= gTypeInfo.size()) {
			return MemberResolutionResult();
		}
		
		const TypeInfo& type_info = gTypeInfo[type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		
		if (!struct_info) {
			return MemberResolutionResult();
		}
		
		// Use BFS to handle inheritance, avoiding the recursive approach
		// that can cause issues with complex template hierarchies
		std::queue<std::pair<const StructTypeInfo*, size_t>> to_visit;
		std::unordered_set<const StructTypeInfo*> visited;
		
		to_visit.push({struct_info, 0});
		
		while (!to_visit.empty()) {
			auto [current_struct, current_offset] = to_visit.front();
			to_visit.pop();
			
			// Skip if already visited (cycle prevention at struct level)
			if (visited.contains(current_struct)) {
				continue;
			}
			visited.insert(current_struct);
			
			// Check direct members
			for (const auto& member : current_struct->members) {
				if (member.getName() == member_name) {
					return MemberResolutionResult(
						&member,
						current_struct,
						member.offset + current_offset,
						false
					);
				}
			}
			
			// Add base classes to the queue
			for (const auto& base : current_struct->base_classes) {
				if (base.type_index < gTypeInfo.size()) {
					const TypeInfo& base_type = gTypeInfo[base.type_index];
					const StructTypeInfo* base_info = base_type.getStructInfo();
					
					if (base_info) {
						to_visit.push({base_info, current_offset + base.offset});
					}
				}
			}
		}
		
		// Not found
		return MemberResolutionResult();
	}
};

// Global instance for use throughout the codebase
// This will be initialized in the implementation file
extern LazyMemberResolver gLazyMemberResolver;

} // namespace FlashCpp
