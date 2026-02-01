#pragma once

/**
 * TemplateTypes.h - Core Template Type System
 * ============================================
 * 
 * This file contains the fundamental types for template instantiation lookup
 * using TypeIndex-based keys instead of string-based keys.
 * 
 * ## Key Design Decisions
 * 
 * 1. **TypeIndex-based Keys**: Template instantiation keys use TypeIndex (an index
 *    into gTypeInfo) instead of type name strings. This prevents ambiguity when
 *    type names contain underscores (e.g., "is_arithmetic_int" vs "is_arithmetic" + "_int").
 * 
 * 2. **InlineVector for Efficiency**: Most templates have 1-4 arguments. Using inline
 *    storage avoids heap allocation in ~95% of cases.
 * 
 * 3. **Separate Type/Value/Template Arguments**: Template arguments are categorized
 *    by their kind (type, non-type value, or template template parameter) for
 *    correct hashing and comparison.
 * 
 * ## Usage
 * 
 * ```cpp
 * // Building a template instantiation key
 * TemplateInstantiationKey key;
 * key.base_template = getTypeIndex("vector");  // TypeIndex of the template
 * key.type_args.push_back(TypeIndex(42));      // TypeIndex for "int"
 * 
 * // Looking up instantiation
 * auto it = gTemplateInstantiations.find(key);
 * ```
 */

#include "AstNodeTypes.h"  // For Type, TypeIndex
#include "StringTable.h"   // For StringHandle
#include <array>
#include <vector>
#include <cstdint>
#include <functional>  // For std::hash

namespace FlashCpp {

// ============================================================================
// InlineVector - SmallVector-style container with inline storage
// ============================================================================

/**
 * InlineVector - A small-buffer-optimized vector
 * 
 * Stores up to N elements inline (no heap allocation). Falls back to
 * std::vector for larger sizes. This is optimized for templates with
 * 1-4 arguments (the common case).
 * 
 * @tparam T The element type
 * @tparam N The inline capacity (default: 4)
 */
template<typename T, size_t N = 4>
class InlineVector {
public:
	InlineVector() = default;
	
	// Copy constructor
	InlineVector(const InlineVector& other) 
		: inline_count_(other.inline_count_), overflow_(other.overflow_) {
		for (size_t i = 0; i < inline_count_; ++i) {
			inline_data_[i] = other.inline_data_[i];
		}
	}
	
	// Move constructor
	InlineVector(InlineVector&& other) noexcept
		: inline_count_(other.inline_count_), overflow_(std::move(other.overflow_)) {
		for (size_t i = 0; i < inline_count_; ++i) {
			inline_data_[i] = other.inline_data_[i];
		}
		other.inline_count_ = 0;
	}
	
	// Copy assignment
	InlineVector& operator=(const InlineVector& other) {
		if (this != &other) {
			inline_count_ = other.inline_count_;
			overflow_ = other.overflow_;
			for (size_t i = 0; i < inline_count_; ++i) {
				inline_data_[i] = other.inline_data_[i];
			}
		}
		return *this;
	}
	
	// Move assignment
	InlineVector& operator=(InlineVector&& other) noexcept {
		if (this != &other) {
			inline_count_ = other.inline_count_;
			overflow_ = std::move(other.overflow_);
			for (size_t i = 0; i < inline_count_; ++i) {
				inline_data_[i] = other.inline_data_[i];
			}
			other.inline_count_ = 0;
		}
		return *this;
	}
	
	void push_back(const T& value) {
		if (inline_count_ < N) {
			inline_data_[inline_count_++] = value;
		} else {
			overflow_.push_back(value);
		}
	}
	
	void push_back(T&& value) {
		if (inline_count_ < N) {
			inline_data_[inline_count_++] = std::move(value);
		} else {
			overflow_.push_back(std::move(value));
		}
	}
	
	template<typename... Args>
	void emplace_back(Args&&... args) {
		if (inline_count_ < N) {
			inline_data_[inline_count_++] = T(std::forward<Args>(args)...);
		} else {
			overflow_.emplace_back(std::forward<Args>(args)...);
		}
	}
	
	[[nodiscard]] size_t size() const noexcept {
		return inline_count_ + overflow_.size();
	}
	
	[[nodiscard]] bool empty() const noexcept {
		return inline_count_ == 0 && overflow_.empty();
	}
	
	void clear() noexcept {
		inline_count_ = 0;
		overflow_.clear();
	}
	
	void reserve(size_t capacity) {
		if (capacity > N) {
			overflow_.reserve(capacity - N);
		}
	}
	
	T& operator[](size_t i) {
		return i < N ? inline_data_[i] : overflow_[i - N];
	}
	
	const T& operator[](size_t i) const {
		return i < N ? inline_data_[i] : overflow_[i - N];
	}
	
	T& back() {
		if (!overflow_.empty()) {
			return overflow_.back();
		}
		return inline_data_[inline_count_ - 1];
	}
	
	const T& back() const {
		if (!overflow_.empty()) {
			return overflow_.back();
		}
		return inline_data_[inline_count_ - 1];
	}
	
	bool operator==(const InlineVector& other) const {
		if (size() != other.size()) return false;
		for (size_t i = 0; i < size(); ++i) {
			if (!((*this)[i] == other[i])) return false;
		}
		return true;
	}
	
	bool operator!=(const InlineVector& other) const {
		return !(*this == other);
	}
	
	// Iterator support for range-based for loops
	class iterator {
	public:
		using iterator_category = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = T*;
		using reference = T&;
		
		iterator(InlineVector* vec, size_t idx) : vec_(vec), idx_(idx) {}
		
		reference operator*() { return (*vec_)[idx_]; }
		pointer operator->() { return &(*vec_)[idx_]; }
		
		iterator& operator++() { ++idx_; return *this; }
		iterator operator++(int) { iterator tmp = *this; ++idx_; return tmp; }
		iterator& operator--() { --idx_; return *this; }
		iterator operator--(int) { iterator tmp = *this; --idx_; return tmp; }
		
		bool operator==(const iterator& other) const { return idx_ == other.idx_; }
		bool operator!=(const iterator& other) const { return idx_ != other.idx_; }
		
	private:
		InlineVector* vec_;
		size_t idx_;
	};
	
	class const_iterator {
	public:
		using iterator_category = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = const T*;
		using reference = const T&;
		
		const_iterator(const InlineVector* vec, size_t idx) : vec_(vec), idx_(idx) {}
		
		reference operator*() const { return (*vec_)[idx_]; }
		pointer operator->() const { return &(*vec_)[idx_]; }
		
		const_iterator& operator++() { ++idx_; return *this; }
		const_iterator operator++(int) { const_iterator tmp = *this; ++idx_; return tmp; }
		const_iterator& operator--() { --idx_; return *this; }
		const_iterator operator--(int) { const_iterator tmp = *this; --idx_; return tmp; }
		
		bool operator==(const const_iterator& other) const { return idx_ == other.idx_; }
		bool operator!=(const const_iterator& other) const { return idx_ != other.idx_; }
		
	private:
		const InlineVector* vec_;
		size_t idx_;
	};
	
	iterator begin() { return iterator(this, 0); }
	iterator end() { return iterator(this, size()); }
	const_iterator begin() const { return const_iterator(this, 0); }
	const_iterator end() const { return const_iterator(this, size()); }
	const_iterator cbegin() const { return const_iterator(this, 0); }
	const_iterator cend() const { return const_iterator(this, size()); }

private:
	std::array<T, N> inline_data_{};
	uint8_t inline_count_ = 0;
	std::vector<T> overflow_;
};

// ============================================================================
// TypeIndexArg - A template type argument represented by TypeIndex
// ============================================================================

/**
 * TypeIndexArg - Represents a type template argument using TypeIndex
 * 
 * This is a simpler representation than TemplateTypeArg, focused purely on
 * identity for lookup purposes. The full type information (references, 
 * pointers, cv-qualifiers) is encoded in the TypeIndex itself.
 */
struct TypeIndexArg {
	TypeIndex type_index = 0;
	
	// CV-qualifiers and reference info that affect template identity
	// These are stored separately because the same TypeIndex with different
	// qualifiers represents different template arguments (e.g., int vs const int&)
	bool is_const = false;
	bool is_volatile = false;
	bool is_reference = false;
	bool is_rvalue_reference = false;
	uint8_t pointer_depth = 0;
	
	TypeIndexArg() = default;
	
	explicit TypeIndexArg(TypeIndex idx) : type_index(idx) {}
	
	TypeIndexArg(TypeIndex idx, bool is_const_, bool is_volatile_, 
	             bool is_ref, bool is_rvalue_ref, uint8_t ptr_depth)
		: type_index(idx)
		, is_const(is_const_)
		, is_volatile(is_volatile_)
		, is_reference(is_ref)
		, is_rvalue_reference(is_rvalue_ref)
		, pointer_depth(ptr_depth) {}
	
	bool operator==(const TypeIndexArg& other) const {
		return type_index == other.type_index &&
		       is_const == other.is_const &&
		       is_volatile == other.is_volatile &&
		       is_reference == other.is_reference &&
		       is_rvalue_reference == other.is_rvalue_reference &&
		       pointer_depth == other.pointer_depth;
	}
	
	bool operator!=(const TypeIndexArg& other) const {
		return !(*this == other);
	}
	
	size_t hash() const {
		size_t h = std::hash<TypeIndex>{}(type_index);
		// Pack qualifiers into a single byte for hashing
		uint8_t quals = (is_const ? 1 : 0) | (is_volatile ? 2 : 0) | 
		                (is_reference ? 4 : 0) | (is_rvalue_reference ? 8 : 0);
		h ^= std::hash<uint8_t>{}(quals) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<uint8_t>{}(pointer_depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}
};

// ============================================================================
// TemplateInstantiationKeyV2 - TypeIndex-based template instantiation key
// ============================================================================

/**
 * TemplateInstantiationKeyV2 - A template instantiation key using TypeIndex
 * 
 * This replaces string-based template instantiation keys with TypeIndex-based
 * keys. The key components are:
 * 
 * 1. base_template: TypeIndex of the template being instantiated (e.g., "vector")
 * 2. type_args: TypeIndex values for type template parameters
 * 3. value_args: int64_t values for non-type template parameters
 * 4. template_args: StringHandle for template template parameters
 * 
 * ## Why TypeIndex instead of strings?
 * 
 * String-based keys like "vector_int" are ambiguous:
 * - Is it "vector" with arg "int"?
 * - Or "vector_int" with no args?
 * - Or "vector_i" with arg "nt"?
 * 
 * TypeIndex-based keys are unambiguous because TypeIndex is assigned uniquely
 * to each type during parsing.
 */
struct TemplateInstantiationKeyV2 {
	StringHandle base_template;                           // Template name handle
	InlineVector<TypeIndexArg, 4> type_args;              // Type arguments
	InlineVector<int64_t, 4> value_args;                  // Non-type arguments
	InlineVector<StringHandle, 2> template_template_args; // Template template args
	
	TemplateInstantiationKeyV2() = default;
	
	explicit TemplateInstantiationKeyV2(StringHandle template_name)
		: base_template(template_name) {}
	
	bool operator==(const TemplateInstantiationKeyV2& other) const {
		return base_template == other.base_template &&
		       type_args == other.type_args &&
		       value_args == other.value_args &&
		       template_template_args == other.template_template_args;
	}
	
	bool operator!=(const TemplateInstantiationKeyV2& other) const {
		return !(*this == other);
	}
	
	// Check if the key is empty (no template specified)
	[[nodiscard]] bool empty() const {
		return base_template.handle == 0;
	}
	
	// Clear the key
	void clear() {
		base_template = StringHandle{};
		type_args.clear();
		value_args.clear();
		template_template_args.clear();
	}
};

/**
 * Hash function for TemplateInstantiationKeyV2
 */
struct TemplateInstantiationKeyV2Hash {
	size_t operator()(const TemplateInstantiationKeyV2& key) const {
		size_t h = std::hash<uint32_t>{}(key.base_template.handle);
		
		// Hash type arguments
		for (size_t i = 0; i < key.type_args.size(); ++i) {
			h ^= key.type_args[i].hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		
		// Hash value arguments
		for (size_t i = 0; i < key.value_args.size(); ++i) {
			h ^= std::hash<int64_t>{}(key.value_args[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		
		// Hash template template arguments
		for (size_t i = 0; i < key.template_template_args.size(); ++i) {
			h ^= std::hash<uint32_t>{}(key.template_template_args[i].handle) + 0x9e3779b9 + (h << 6) + (h >> 2);
		}
		
		return h;
	}
};

// ============================================================================
// Helper functions for building template keys
// ============================================================================
// NOTE: These functions are implemented in TemplateRegistry.h after the 
// TemplateTypeArg definition to avoid circular dependencies.
// 
// Available functions:
//   - FlashCpp::makeTypeIndexArg(const TemplateTypeArg& arg) -> TypeIndexArg
//   - FlashCpp::makeInstantiationKeyV2(StringHandle, const std::vector<TemplateTypeArg>&) -> TemplateInstantiationKeyV2

} // namespace FlashCpp

// Provide std::hash specialization for use with unordered_map
namespace std {
	template<>
	struct hash<FlashCpp::TemplateInstantiationKeyV2> {
		size_t operator()(const FlashCpp::TemplateInstantiationKeyV2& key) const {
			return FlashCpp::TemplateInstantiationKeyV2Hash{}(key);
		}
	};
	
	template<>
	struct hash<FlashCpp::TypeIndexArg> {
		size_t operator()(const FlashCpp::TypeIndexArg& arg) const {
			return arg.hash();
		}
	};
}
