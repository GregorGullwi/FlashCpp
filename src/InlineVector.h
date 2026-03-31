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
#include <cassert>
#include <initializer_list>
#include <utility>

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
template <typename T, size_t N = 4>
class InlineVector {
public:
	InlineVector() = default;

	InlineVector(std::initializer_list<T> init) {
		reserve(init.size());
		for (const auto& item : init) {
			push_back(item);
		}
	}

 // Construct from std::vector (enables seamless migration)
	InlineVector(const std::vector<T>& vec) {
		reserve(vec.size());
		for (const auto& item : vec) {
			push_back(item);
		}
	}

 // Move-construct from std::vector
	InlineVector(std::vector<T>&& vec) {
		reserve(vec.size());
		for (auto& item : vec) {
			push_back(std::move(item));
		}
	}

 // Assignment from std::vector
	InlineVector& operator=(const std::vector<T>& vec) {
		clear();
		reserve(vec.size());
		for (const auto& item : vec) {
			push_back(item);
		}
		return *this;
	}

 // Move-assignment from std::vector
	InlineVector& operator=(std::vector<T>&& vec) {
		clear();
		reserve(vec.size());
		for (auto& item : vec) {
			push_back(std::move(item));
		}
		return *this;
	}

	InlineVector& operator=(std::initializer_list<T> init) {
		clear();
		reserve(init.size());
		for (const auto& item : init) {
			push_back(item);
		}
		return *this;
	}

 // Implicit conversion to std::vector (enables seamless migration)
	operator std::vector<T>() const {
		std::vector<T> result;
		result.reserve(size());
		for (size_t i = 0; i < size(); ++i) {
			result.push_back((*this)[i]);
		}
		return result;
	}

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
			inline_data_[i] = std::move(other.inline_data_[i]);
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
				inline_data_[i] = std::move(other.inline_data_[i]);
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

	template <typename... Args>
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

	void pop_back() {
		assert(!empty() && "Cannot pop_back from an empty InlineVector");
		if (!overflow_.empty()) {
			overflow_.pop_back();
			return;
		}
		--inline_count_;
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
	// If i is within inline storage, use inline_data_
	// Otherwise use overflow - index into overflow is (i - N) since inline storage holds exactly N elements
		assert(i < size() && "Index out of bounds in InlineVector::operator[]");
		return i < N ? inline_data_[i] : overflow_[i - N];
	}

	const T& operator[](size_t i) const {
		assert(i < size() && "Index out of bounds in InlineVector::operator[]");
		return i < N ? inline_data_[i] : overflow_[i - N];
	}

	T& front() {
		assert(!empty() && "Cannot call front() on an empty InlineVector");
		return inline_data_[0];
	}

	const T& front() const {
		assert(!empty() && "Cannot call front() on an empty InlineVector");
		return inline_data_[0];
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
		if (size() != other.size())
			return false;
		for (size_t i = 0; i < size(); ++i) {
			if (!((*this)[i] == other[i]))
				return false;
		}
		return true;
	}

	bool operator!=(const InlineVector& other) const {
		return !(*this == other);
	}

 // Iterator support for range-based for loops
 // Unified iterator implementation using template parameter for const/non-const
	template <bool IsConst>
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

		iterator_impl& operator++() {
			++idx_;
			return *this;
		}
		iterator_impl operator++(int) {
			iterator_impl tmp = *this;
			++idx_;
			return tmp;
		}
		iterator_impl& operator--() {
			--idx_;
			return *this;
		}
		iterator_impl operator--(int) {
			iterator_impl tmp = *this;
			--idx_;
			return tmp;
		}

		iterator_impl operator+(difference_type n) const { return iterator_impl(vec_, idx_ + n); }
		friend iterator_impl operator+(difference_type n, const iterator_impl& it) { return iterator_impl(it.vec_, it.idx_ + n); }
		iterator_impl operator-(difference_type n) const { return iterator_impl(vec_, idx_ - n); }
		difference_type operator-(const iterator_impl& other) const { return static_cast<difference_type>(idx_) - static_cast<difference_type>(other.idx_); }
		iterator_impl& operator+=(difference_type n) {
			idx_ += n;
			return *this;
		}
		iterator_impl& operator-=(difference_type n) {
			idx_ -= n;
			return *this;
		}
		reference operator[](difference_type n) const { return (*vec_)[idx_ + n]; }

		bool operator==(const iterator_impl& other) const { return vec_ == other.vec_ && idx_ == other.idx_; }
		bool operator!=(const iterator_impl& other) const { return vec_ != other.vec_ || idx_ != other.idx_; }
		bool operator<(const iterator_impl& other) const { return idx_ < other.idx_; }
		bool operator>(const iterator_impl& other) const { return idx_ > other.idx_; }
		bool operator<=(const iterator_impl& other) const { return idx_ <= other.idx_; }
		bool operator>=(const iterator_impl& other) const { return idx_ >= other.idx_; }

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

 // Internal helper: insert a single element at logical index `idx`.
 // Handles the inline/overflow spill boundary correctly.
	iterator insert_at(size_t idx, const T& value) {
		if (idx >= N) {
	// Insert position is in the overflow region
			overflow_.insert(overflow_.begin() + static_cast<typename std::vector<T>::difference_type>(idx - N), value);
		} else if (inline_count_ < N) {
	// Inline has room — shift elements right and insert
			for (size_t i = inline_count_; i > idx; --i) {
				inline_data_[i] = std::move(inline_data_[i - 1]);
			}
			inline_data_[idx] = value;
			inline_count_++;
		} else {
	// Inline is full and idx < N — spill the last inline element
	// into the front of overflow, shift [idx..N-2] right, then insert.
			overflow_.insert(overflow_.begin(), std::move(inline_data_[N - 1]));
			for (size_t i = N - 1; i > idx; --i) {
				inline_data_[i] = std::move(inline_data_[i - 1]);
			}
			inline_data_[idx] = value;
	// inline_count_ stays N
		}
		return iterator(this, idx);
	}

 // Insert a single element at position (const iterator)
	iterator insert(const_iterator pos, const T& value) {
		return insert_at(static_cast<size_t>(pos - begin()), value);
	}

 // Insert a single element at position (non-const iterator)
	iterator insert(iterator pos, const T& value) {
		return insert_at(static_cast<size_t>(pos - begin()), value);
	}

 // Internal helper: insert a range of `count` elements at logical index `idx`.
 // Materialises elements into a temporary buffer first so that self-referencing
 // iterators (pointing into *this) are safe.
	template <typename Iter>
	iterator insert_range_at(size_t idx, Iter first, Iter last) {
		if (first == last)
			return iterator(this, idx);

	// Materialise into a temporary buffer so we don't invalidate source
	// iterators if they point into *this.
		std::vector<T> tmp(first, last);
		size_t count = tmp.size();

		if (idx >= N) {
	// Entirely in the overflow region
			overflow_.insert(
				overflow_.begin() + static_cast<typename std::vector<T>::difference_type>(idx - N),
				tmp.begin(), tmp.end());
			return iterator(this, idx);
		}

	// idx < N — some or all new elements land in inline storage.
	// Figure out how many existing inline tail elements will be displaced.
		size_t inline_tail = (inline_count_ > idx) ? (inline_count_ - idx) : 0;
	// Total elements that would occupy slots [idx .. idx+count+inline_tail-1].
	// Slots >= N must spill to overflow.

	// 1. Spill existing inline elements [N - spill_count .. inline_count_) into
	//    the front of overflow (in order) to make room.
		size_t new_inline_used = idx + count + inline_tail; // would-be inline occupancy
		if (new_inline_used > N) {
	// The spilled elements come from the rightmost positions of the
	// combined sequence (existing tail elements first, then new elements that
	// don't fit). It's simplest to: move ALL tail elements to a temp, write
	// new elements, then put tail elements back — spilling as needed.

	// Collect existing tail [idx .. inline_count_)
			std::vector<T> tail;
			tail.reserve(inline_tail);
			for (size_t i = idx; i < inline_count_; ++i) {
				tail.push_back(std::move(inline_data_[i]));
			}

	// Write as many new elements as fit into inline slots [idx..)
			size_t written_inline = 0;
			for (size_t i = 0; i < count && (idx + i) < N; ++i) {
				inline_data_[idx + i] = std::move(tmp[i]);
				written_inline++;
			}

	// Remaining new elements that didn't fit go to a combined spill list
			std::vector<T> to_overflow;
			to_overflow.reserve(count - written_inline + inline_tail);
			for (size_t i = written_inline; i < count; ++i) {
				to_overflow.push_back(std::move(tmp[i]));
			}

	// Now place tail elements: fill remaining inline slots, rest to overflow
			size_t inline_cursor = idx + written_inline;
			size_t tail_i = 0;
			for (; tail_i < tail.size() && inline_cursor < N; ++tail_i, ++inline_cursor) {
				inline_data_[inline_cursor] = std::move(tail[tail_i]);
			}
			for (; tail_i < tail.size(); ++tail_i) {
				to_overflow.push_back(std::move(tail[tail_i]));
			}

	// Insert the overflow portion at the front of the existing overflow
			if (!to_overflow.empty()) {
				overflow_.insert(overflow_.begin(), to_overflow.begin(), to_overflow.end());
			}

			inline_count_ = static_cast<uint8_t>(std::min(new_inline_used, N));
		} else {
	// Everything fits in inline storage — simple shift and copy
	// Shift existing [idx .. inline_count_) right by `count`
			for (size_t i = inline_count_; i > idx; --i) {
				inline_data_[i + count - 1] = std::move(inline_data_[i - 1]);
			}
			for (size_t i = 0; i < count; ++i) {
				inline_data_[idx + i] = std::move(tmp[i]);
			}
			inline_count_ = static_cast<uint8_t>(inline_count_ + count);
		}
		return iterator(this, idx);
	}

 // Insert a range of elements at position (const iterators)
	iterator insert(const_iterator pos, const_iterator first, const_iterator last) {
		return insert_range_at(static_cast<size_t>(pos - begin()), first, last);
	}

 // Insert a range of elements at position (non-const iterators)
	iterator insert(iterator pos, iterator first, iterator last) {
		return insert_range_at(static_cast<size_t>(pos - begin()), first, last);
	}

private:
	static_assert(N <= 255, "InlineVector: N must be <= 255 (inline_count_ is uint8_t)");
	std::array<T, N> inline_data_{};
	uint8_t inline_count_ = 0;
	std::vector<T> overflow_;
};

} // namespace FlashCpp

// Make InlineVector available outside namespace for convenience
using FlashCpp::InlineVector;
