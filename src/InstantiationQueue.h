// InstantiationQueue.h - Phase 2: Explicit instantiation tracking
// Implements the instantiation queue strategy from KNOWN_ISSUES.md Phase 2

#pragma once

#include "AstNodeTypes.h"
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <optional>

namespace FlashCpp {

// Status of a template instantiation
enum class InstantiationStatus {
	Pending,      // Queued but not started
	InProgress,   // Currently being instantiated
	Complete,     // Successfully instantiated
	Failed        // Instantiation failed
};

// Source location for error reporting
struct SourceLocation {
	std::string file;
	size_t line;
	size_t column;
	
	SourceLocation() : line(0), column(0) {}
	SourceLocation(std::string f, size_t l, size_t c) 
		: file(std::move(f)), line(l), column(c) {}
};

// Template argument for instantiation
struct TemplateArgument {
	enum class Kind {
		Type,      // Type argument (e.g., int, float)
		Value,     // Non-type value argument (e.g., 42, true)
		Template   // Template template argument
	};
	
	Kind kind;
	Type type;                // For type arguments
	TypeIndex type_index;     // For complex type arguments
	unsigned long long value; // For value arguments
	
	// Constructor for type arguments
	static TemplateArgument makeType(Type t, TypeIndex idx = 0) {
		TemplateArgument arg;
		arg.kind = Kind::Type;
		arg.type = t;
		arg.type_index = idx;
		arg.value = 0;
		return arg;
	}
	
	// Constructor for value arguments
	static TemplateArgument makeValue(unsigned long long v, Type t = Type::Int) {
		TemplateArgument arg;
		arg.kind = Kind::Value;
		arg.type = t;
		arg.type_index = 0;
		arg.value = v;
		return arg;
	}
	
	// Hash for use in maps
	size_t hash() const {
		size_t h = std::hash<int>{}(static_cast<int>(kind));
		h ^= std::hash<int>{}(static_cast<int>(type)) << 1;
		h ^= std::hash<TypeIndex>{}(type_index) << 2;
		h ^= std::hash<unsigned long long>{}(value) << 3;
		return h;
	}
	
	bool operator==(const TemplateArgument& other) const {
		if (kind != other.kind) return false;
		switch (kind) {
			case Kind::Type:
				return type == other.type && type_index == other.type_index;
			case Kind::Value:
				return value == other.value && type == other.type;
			case Kind::Template:
				return type_index == other.type_index;
		}
		return false;
	}
};

// Key for identifying unique instantiations
struct InstantiationKey {
	StringHandle template_name;
	std::vector<TemplateArgument> arguments;
	
	size_t hash() const {
		size_t h = std::hash<StringHandle>{}(template_name);
		for (const auto& arg : arguments) {
			h ^= arg.hash() << 1;
		}
		return h;
	}
	
	bool operator==(const InstantiationKey& other) const {
		return template_name == other.template_name && 
		       arguments == other.arguments;
	}
};

// Hash function for InstantiationKey
struct InstantiationKeyHash {
	size_t operator()(const InstantiationKey& key) const {
		return key.hash();
	}
};

// Record of a template instantiation
struct InstantiationRecord {
	InstantiationKey key;
	SourceLocation point_of_instantiation;
	InstantiationStatus status;
	std::optional<TypeIndex> result_type_index;  // Set when status == Complete
	std::string error_message;                    // Set when status == Failed
	
	InstantiationRecord(InstantiationKey k, SourceLocation loc)
		: key(std::move(k)), point_of_instantiation(std::move(loc)), 
		  status(InstantiationStatus::Pending) {}
};

// Phase 2: Explicit instantiation queue
class InstantiationQueue {
private:
	// Queue of pending instantiations
	std::vector<InstantiationRecord> pending_;
	
	// Set of instantiations in progress (for cycle detection)
	std::unordered_set<InstantiationKey, InstantiationKeyHash> in_progress_;
	
	// Map of completed instantiations to their results
	std::unordered_map<InstantiationKey, TypeIndex, InstantiationKeyHash> completed_;
	
	// Map of failed instantiations to their error messages
	std::unordered_map<InstantiationKey, std::string, InstantiationKeyHash> failed_;

public:
	// Enqueue a template instantiation
	void enqueue(const InstantiationKey& key, const SourceLocation& loc) {
		// Don't enqueue if already completed or in progress
		if (completed_.contains(key) || in_progress_.contains(key) || failed_.contains(key)) {
			return;
		}
		
		// Check if already in pending queue
		for (const auto& record : pending_) {
			if (record.key == key) {
				return;  // Already queued
			}
		}
		
		pending_.emplace_back(key, loc);
	}
	
	// Check if an instantiation is complete
	bool isComplete(const InstantiationKey& key) const {
		return completed_.contains(key);
	}
	
	// Get the result of a completed instantiation
	std::optional<TypeIndex> getResult(const InstantiationKey& key) const {
		auto it = completed_.find(key);
		if (it != completed_.end()) {
			return it->second;
		}
		return std::nullopt;
	}
	
	// Check if an instantiation failed
	bool isFailed(const InstantiationKey& key) const {
		return failed_.contains(key);
	}
	
	// Get the error message for a failed instantiation
	std::string getError(const InstantiationKey& key) const {
		auto it = failed_.find(key);
		if (it != failed_.end()) {
			return it->second;
		}
		return "";
	}
	
	// Mark an instantiation as in progress
	bool markInProgress(const InstantiationKey& key) {
		if (in_progress_.contains(key)) {
			return false;  // Cycle detected
		}
		in_progress_.insert(key);
		return true;
	}
	
	// Mark an instantiation as complete
	void markComplete(const InstantiationKey& key, TypeIndex result) {
		in_progress_.erase(key);
		completed_[key] = result;
		
		// Remove from pending queue
		pending_.erase(
			std::remove_if(pending_.begin(), pending_.end(),
				[&key](const InstantiationRecord& r) { return r.key == key; }),
			pending_.end()
		);
	}
	
	// Mark an instantiation as failed
	void markFailed(const InstantiationKey& key, const std::string& error) {
		in_progress_.erase(key);
		failed_[key] = error;
		
		// Remove from pending queue
		pending_.erase(
			std::remove_if(pending_.begin(), pending_.end(),
				[&key](const InstantiationRecord& r) { return r.key == key; }),
			pending_.end()
		);
	}
	
	// Get all pending instantiations
	const std::vector<InstantiationRecord>& getPending() const {
		return pending_;
	}
	
	// Check if there are pending instantiations
	bool hasPending() const {
		return !pending_.empty();
	}
	
	// Clear all queues (for testing)
	void clear() {
		pending_.clear();
		in_progress_.clear();
		completed_.clear();
		failed_.clear();
	}
	
	// Get statistics
	struct Statistics {
		size_t pending_count;
		size_t in_progress_count;
		size_t completed_count;
		size_t failed_count;
	};
	
	Statistics getStatistics() const {
		return Statistics{
			pending_.size(),
			in_progress_.size(),
			completed_.size(),
			failed_.size()
		};
	}
};

// Global instance for use throughout the codebase
extern InstantiationQueue gInstantiationQueue;

} // namespace FlashCpp
