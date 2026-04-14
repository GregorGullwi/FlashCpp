#pragma once

/**
 * InlineVector.h - Small-buffer-optimized vector
 * =============================================
 *
 * A vector-like container that stores small numbers of elements inline
 * (avoiding heap allocation) and moves all elements to contiguous heap storage
 * for larger sizes.
 * This is optimized for templates with 1-4 arguments (the common case).
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace FlashCpp {

/**
 * InlineVector - A small-buffer-optimized vector
 *
 * Stores up to N elements inline (no heap allocation). When it grows beyond
 * N, all elements are moved into std::vector-backed storage so the logical
 * range stays contiguous and can back std::span.
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
		assignFromVector(vec);
	}

	// Move-construct from std::vector
	InlineVector(std::vector<T>&& vec) {
		assignFromVector(std::move(vec));
	}

	// Assignment from std::vector
	InlineVector& operator=(const std::vector<T>& vec) {
		assignFromVector(vec);
		return *this;
	}

	// Move-assignment from std::vector
	InlineVector& operator=(std::vector<T>&& vec) {
		assignFromVector(std::move(vec));
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
		if (!using_inline_storage_) {
			return heap_data_;
		}
		return std::vector<T>(begin(), end());
	}

	InlineVector(const InlineVector& other) {
		copyFrom(other);
	}

	InlineVector(InlineVector&& other) {
		moveFrom(std::move(other));
	}

	InlineVector& operator=(const InlineVector& other) {
		if (this != &other) {
			resetStorage();
			copyFrom(other);
		}
		return *this;
	}

	InlineVector& operator=(InlineVector&& other) {
		if (this != &other) {
			resetStorage();
			moveFrom(std::move(other));
		}
		return *this;
	}

	void push_back(const T& value) {
		if (using_inline_storage_ && inline_count_ < N) {
			inline_data_[inline_count_++] = value;
			return;
		}
		if (referencesActiveInlineStorage(value)) {
			T copied_value = value;
			ensureHeapStorage(size() + 1);
			heap_data_.push_back(std::move(copied_value));
			return;
		}
		ensureHeapStorage(size() + 1);
		heap_data_.push_back(value);
	}

	void push_back(T&& value) {
		if (using_inline_storage_ && inline_count_ < N) {
			inline_data_[inline_count_++] = std::move(value);
			return;
		}
		T moved_value = std::move(value);
		ensureHeapStorage(size() + 1);
		heap_data_.push_back(std::move(moved_value));
	}

	template <typename... Args>
	void emplace_back(Args&&... args) {
		if (using_inline_storage_ && inline_count_ < N) {
			inline_data_[inline_count_++] = T(std::forward<Args>(args)...);
			return;
		}
		ensureHeapStorage(size() + 1);
		heap_data_.emplace_back(std::forward<Args>(args)...);
	}

	[[nodiscard]] size_t size() const noexcept {
		return using_inline_storage_ ? inline_count_ : heap_data_.size();
	}

	[[nodiscard]] bool empty() const noexcept {
		return size() == 0;
	}

	void pop_back() {
		assert(!empty() && "Cannot pop_back from an empty InlineVector");
		if (!using_inline_storage_) {
			heap_data_.pop_back();
			return;
		}
		--inline_count_;
		inline_data_[inline_count_] = T{};
	}

	void clear() noexcept(noexcept(std::declval<T&>() = T{})) {
		resetStorage();
	}

	void reserve(size_t capacity) {
		if (using_inline_storage_) {
			if (capacity > N) {
				ensureHeapStorage(capacity);
			}
			return;
		}
		heap_data_.reserve(capacity);
	}

	[[nodiscard]] T* data() noexcept {
		if (!using_inline_storage_) {
			return heap_data_.data();
		}
		return inline_data_.data();
	}

	[[nodiscard]] const T* data() const noexcept {
		if (!using_inline_storage_) {
			return heap_data_.data();
		}
		return inline_data_.data();
	}

	operator std::span<T>() noexcept {
		return std::span<T>(data(), size());
	}

	operator std::span<const T>() const noexcept {
		return std::span<const T>(data(), size());
	}

	T& operator[](size_t i) {
		assert(i < size() && "Index out of bounds in InlineVector::operator[]");
		return data()[i];
	}

	const T& operator[](size_t i) const {
		assert(i < size() && "Index out of bounds in InlineVector::operator[]");
		return data()[i];
	}

	T& front() {
		assert(!empty() && "Cannot call front() on an empty InlineVector");
		return data()[0];
	}

	const T& front() const {
		assert(!empty() && "Cannot call front() on an empty InlineVector");
		return data()[0];
	}

	T& back() {
		assert(!empty() && "Cannot call back() on an empty InlineVector");
		return data()[size() - 1];
	}

	const T& back() const {
		assert(!empty() && "Cannot call back() on an empty InlineVector");
		return data()[size() - 1];
	}

	bool operator==(const InlineVector& other) const {
		return size() == other.size() && std::equal(begin(), end(), other.begin());
	}

	bool operator!=(const InlineVector& other) const {
		return !(*this == other);
	}

	using iterator = T*;
	using const_iterator = const T*;

	iterator begin() noexcept { return data(); }
	iterator end() noexcept { return size() == 0 ? begin() : begin() + static_cast<std::ptrdiff_t>(size()); }
	const_iterator begin() const noexcept { return data(); }
	const_iterator end() const noexcept { return size() == 0 ? begin() : begin() + static_cast<std::ptrdiff_t>(size()); }
	const_iterator cbegin() const noexcept { return data(); }
	const_iterator cend() const noexcept { return size() == 0 ? cbegin() : cbegin() + static_cast<std::ptrdiff_t>(size()); }

	iterator insert_at(size_t idx, const T& value) {
		assert(idx <= size() && "Insert position out of bounds in InlineVector::insert_at");
		if (using_inline_storage_ && inline_count_ < N) {
			for (size_t i = inline_count_; i > idx; --i) {
				inline_data_[i] = std::move(inline_data_[i - 1]);
			}
			inline_data_[idx] = value;
			++inline_count_;
			return begin() + idx;
		}

		if (referencesActiveInlineStorage(value)) {
			T copied_value = value;
			ensureHeapStorage(size() + 1);
			heap_data_.insert(heap_data_.begin() + static_cast<typename std::vector<T>::difference_type>(idx), std::move(copied_value));
			return begin() + idx;
		}
		ensureHeapStorage(size() + 1);
		heap_data_.insert(heap_data_.begin() + static_cast<typename std::vector<T>::difference_type>(idx), value);
		return begin() + idx;
	}

	iterator insert(const_iterator pos, const T& value) {
		return insert_at(static_cast<size_t>(pos - begin()), value);
	}

	iterator insert(iterator pos, const T& value) {
		return insert_at(static_cast<size_t>(pos - begin()), value);
	}

	template <typename Iter>
	iterator insert_range_at(size_t idx, Iter first, Iter last) {
		assert(idx <= size() && "Insert position out of bounds in InlineVector::insert_range_at");
		if (first == last) {
			return begin() + idx;
		}

		std::vector<T> tmp(first, last);
		size_t count = tmp.size();
		if (using_inline_storage_ && inline_count_ + count <= N) {
			for (size_t i = inline_count_; i > idx; --i) {
				inline_data_[i + count - 1] = std::move(inline_data_[i - 1]);
			}
			for (size_t i = 0; i < count; ++i) {
				inline_data_[idx + i] = std::move(tmp[i]);
			}
			inline_count_ = static_cast<uint8_t>(inline_count_ + count);
			return begin() + idx;
		}

		ensureHeapStorage(size() + count);
		heap_data_.insert(
			heap_data_.begin() + static_cast<typename std::vector<T>::difference_type>(idx),
			tmp.begin(),
			tmp.end());
		return begin() + idx;
	}

	iterator insert(const_iterator pos, const_iterator first, const_iterator last) {
		return insert_range_at(static_cast<size_t>(pos - begin()), first, last);
	}

	iterator insert(iterator pos, iterator first, iterator last) {
		return insert_range_at(static_cast<size_t>(pos - begin()), first, last);
	}

private:
	static_assert(N <= 255, "InlineVector: N must be <= 255 (inline_count_ is uint8_t)");

	[[nodiscard]] bool referencesActiveInlineStorage(const T& value) const noexcept {
		if (!using_inline_storage_) {
			return false;
		}
		const auto value_address = reinterpret_cast<std::uintptr_t>(std::addressof(value));
		const auto inline_begin = reinterpret_cast<std::uintptr_t>(inline_data_.data());
		const auto inline_end = inline_begin + inline_count_ * sizeof(T);
		return value_address >= inline_begin && value_address < inline_end;
	}

	void resetInlineStorage() {
		for (size_t i = 0; i < inline_count_; ++i) {
			inline_data_[i] = T{};
		}
		inline_count_ = 0;
	}

	void resetStorage() {
		if (using_inline_storage_) {
			resetInlineStorage();
			return;
		}
		heap_data_.clear();
	}

	void ensureHeapStorage(size_t capacity) {
		if (!using_inline_storage_) {
			heap_data_.reserve(capacity);
			return;
		}

		heap_data_.reserve(capacity);
		heap_data_.insert(
			heap_data_.end(),
			std::make_move_iterator(inline_data_.begin()),
			std::make_move_iterator(inline_data_.begin() + inline_count_));
		inline_count_ = 0;
		using_inline_storage_ = false;
	}

	void copyFrom(const InlineVector& other) {
		using_inline_storage_ = other.using_inline_storage_;
		inline_count_ = 0;
		if (!using_inline_storage_) {
			heap_data_ = other.heap_data_;
			return;
		}
		assert(other.inline_count_ <= inline_data_.size() && "InlineVector inline storage overflow");
		std::copy_n(other.inline_data_.begin(), other.inline_count_, inline_data_.begin());
		inline_count_ = other.inline_count_;
	}

	void moveFrom(InlineVector&& other) {
		using_inline_storage_ = other.using_inline_storage_;
		inline_count_ = 0;
		if (!using_inline_storage_) {
			heap_data_ = std::move(other.heap_data_);
			return;
		}
		assert(other.inline_count_ <= inline_data_.size() && "InlineVector inline storage overflow");
		std::move(other.inline_data_.begin(), other.inline_data_.begin() + other.inline_count_, inline_data_.begin());
		inline_count_ = other.inline_count_;
		other.resetInlineStorage();
	}

	void assignFromVector(const std::vector<T>& vec) {
		resetStorage();
		using_inline_storage_ = vec.size() <= N;
		if (!using_inline_storage_) {
			heap_data_ = vec;
			return;
		}
		assert(vec.size() <= inline_data_.size() && "InlineVector inline storage overflow");
		std::copy_n(vec.begin(), vec.size(), inline_data_.data());
		inline_count_ = static_cast<uint8_t>(vec.size());
	}

	void assignFromVector(std::vector<T>&& vec) {
		resetStorage();
		using_inline_storage_ = vec.size() <= N;
		if (!using_inline_storage_) {
			heap_data_ = std::move(vec);
			return;
		}
		assert(vec.size() <= inline_data_.size() && "InlineVector inline storage overflow");
		std::move(vec.begin(), vec.end(), inline_data_.data());
		inline_count_ = static_cast<uint8_t>(vec.size());
	}

	std::array<T, N> inline_data_{};
	uint8_t inline_count_ = 0;
	bool using_inline_storage_ = true;
	std::vector<T> heap_data_;
};

} // namespace FlashCpp

// Make InlineVector available outside namespace for convenience
using FlashCpp::InlineVector;
