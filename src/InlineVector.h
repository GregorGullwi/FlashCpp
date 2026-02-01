#pragma once

/**
 * InlineVector.h - Small-buffer-optimized vector
 * =============================================
 * 
 * A vector-like container that stores small numbers of elements inline
 * (avoiding heap allocation) and overflows to std::vector for larger sizes.
 * This is optimized for templates with 1-4 arguments (the common case).
 */

#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace FlashCpp {

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
		// If i is within inline storage that has been filled, use inline_data_
		// Otherwise use overflow (handles case when i >= N)
		return i < static_cast<size_t>(inline_count_) ? inline_data_[i] : overflow_[i - N];
	}
	
	const T& operator[](size_t i) const {
		return i < static_cast<size_t>(inline_count_) ? inline_data_[i] : overflow_[i - N];
	}
	
	T& back() {
		// Precondition: container must not be empty
		// Note: Calling back() on empty container is undefined behavior (matches std::vector)
		if (!overflow_.empty()) {
			return overflow_.back();
		}
		// inline_count_ > 0 is guaranteed if overflow_ is empty and container is non-empty
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
	// Unified iterator implementation using template parameter for const/non-const
	template<bool IsConst>
	class iterator_impl {
	public:
		using iterator_category = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using vec_type = std::conditional_t<IsConst, const InlineVector*, InlineVector*>;
		using pointer = std::conditional_t<IsConst, const T*, T*>;
		using reference = std::conditional_t<IsConst, const T&, T&>;
		
		iterator_impl(vec_type vec, size_t idx) : vec_(vec), idx_(idx) {}
		
		reference operator*() const { return (*vec_)[idx_]; }
		pointer operator->() const { return &(*vec_)[idx_]; }
		
		iterator_impl& operator++() { ++idx_; return *this; }
		iterator_impl operator++(int) { iterator_impl tmp = *this; ++idx_; return tmp; }
		iterator_impl& operator--() { --idx_; return *this; }
		iterator_impl operator--(int) { iterator_impl tmp = *this; --idx_; return tmp; }
		
		bool operator==(const iterator_impl& other) const { return idx_ == other.idx_; }
		bool operator!=(const iterator_impl& other) const { return idx_ != other.idx_; }
		
	private:
		vec_type vec_;
		size_t idx_;
	};
	
	using iterator = iterator_impl<false>;
	using const_iterator = iterator_impl<true>;
	
	iterator begin() { return iterator(this, 0); }
	iterator end() { return iterator(this, size()); }
	const_iterator begin() const { return const_iterator(this, 0); }
	const_iterator end() const { return const_iterator(this, size()); }
	const_iterator cbegin() const { return const_iterator(this, 0); }
	const_iterator cend() const { return const_iterator(this, size()); }

private:
	static_assert(N <= 255, "InlineVector: N must be <= 255 (inline_count_ is uint8_t)");
	std::array<T, N> inline_data_{};
	uint8_t inline_count_ = 0;
	std::vector<T> overflow_;
};

} // namespace FlashCpp

// Make InlineVector available outside namespace for convenience
using FlashCpp::InlineVector;
