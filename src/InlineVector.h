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

#include "MigrationTelemetryConfig.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace FlashCpp {

enum class InlineVectorSpillFamily : uint8_t {
	Unknown = 0,
	OverloadResolution = 1,
	TemplateArgument = 2,
	QualifiedLookup = 3,
	ClassLayout = 4,
	AggregateInitialization = 5,
	Count,
};

inline std::string_view inlineVectorSpillFamilyLabel(InlineVectorSpillFamily family) {
	switch (family) {
	case InlineVectorSpillFamily::Unknown:
		return "unknown";
	case InlineVectorSpillFamily::OverloadResolution:
		return "overload-resolution";
	case InlineVectorSpillFamily::TemplateArgument:
		return "template-argument";
	case InlineVectorSpillFamily::QualifiedLookup:
		return "qualified-lookup";
	case InlineVectorSpillFamily::ClassLayout:
		return "class-layout";
	case InlineVectorSpillFamily::AggregateInitialization:
		return "aggregate-initialization";
	case InlineVectorSpillFamily::Count:
		break;
	}
	return "invalid";
}

#if FLASHCPP_TRACK_INLINE_VECTOR_SPILLS
inline thread_local uint64_t gInlineVectorSpillCount = 0;
inline thread_local std::array<uint64_t, static_cast<std::size_t>(InlineVectorSpillFamily::Count)>
	gInlineVectorSpillFamilyCounts{};
#endif

/**
 * InlineVector - A small-buffer-optimized vector
 *
 * Stores up to N elements inline (no heap allocation). When it grows beyond
 * N, all elements are moved into std::vector-backed storage so the logical
 * range stays contiguous and can back std::span.
 *
 * @tparam T The element type
 * @tparam SpillFamily Migration telemetry family attributed when inline storage spills
 * @tparam N The inline capacity (default: 4)
 */
template <typename T, InlineVectorSpillFamily SpillFamily, size_t N = 4>
class InlineVector {
public:
	static_assert(SpillFamily != InlineVectorSpillFamily::Unknown &&
				  SpillFamily != InlineVectorSpillFamily::Count,
		"InlineVector requires a concrete spill telemetry family");

	InlineVector() = default;

	InlineVector(std::initializer_list<T> init) {
		reserve(init.size());
		for (const auto& item : init) {
			push_back(item);
		}
	}

	// Construct from std::vector (enables seamless migration)
	InlineVector(std::span<const T> vec) {
		assignFromVector(vec);
	}

	// Move-construct from std::vector
	InlineVector(std::vector<T>&& vec) {
		assignFromVector(std::move(vec));
	}

	// Assignment from std::vector
	InlineVector& operator=(std::span<const T> vec) {
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

	[[nodiscard]] std::vector<T> toVector() const & {
		if (!using_inline_storage_) {
			return heap_data_;
		}
		return std::vector<T>(cbegin(), cend());
	}

	[[nodiscard]] std::vector<T> toVector() && {
		if (!using_inline_storage_) {
			return std::move(heap_data_);
		}
		return std::vector<T>(
			std::make_move_iterator(begin()),
			std::make_move_iterator(end()));
	}

	InlineVector(const InlineVector& other) {
		copyFrom(other);
	}

	InlineVector(InlineVector&& other) noexcept(std::is_nothrow_move_assignable_v<T> && std::is_nothrow_default_constructible_v<T>) {
		moveFrom(std::move(other));
	}

	InlineVector& operator=(const InlineVector& other) {
		if (this != &other) {
			resetStorage();
			copyFrom(other);
		}
		return *this;
	}

	InlineVector& operator=(InlineVector&& other) noexcept(std::is_nothrow_move_assignable_v<T> && std::is_nothrow_default_constructible_v<T>) {
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
		T value(std::forward<Args>(args)...);
		ensureHeapStorage(size() + 1);
		heap_data_.push_back(std::move(value));
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

	[[nodiscard]] size_t capacity() const noexcept {
		if (using_inline_storage_) {
			return N;
		}
		return heap_data_.capacity();
	}

	template <std::input_iterator InputIt>
	void assign(InputIt first, InputIt last) {
		clear();
		for (InputIt it = first; it != last; ++it)
			push_back(*it);
	}

	void assign(size_t count, const T& value) {
		clear();
		reserve(count);
		for (size_t i = 0; i < count; ++i) {
			push_back(value);
		}
	}

	void assign(std::initializer_list<T> init) {
		assign(init.begin(), init.end());
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

	void shrink_to_fit() {
		if (using_inline_storage_) {
			return;
		}
		heap_data_.shrink_to_fit();
	}

	void resize(size_t count) {
		size_t current_size = size();
		if (count < current_size) {
			erase(begin() + count, end());
			return;
		}
		if (count == current_size) {
			return;
		}
		reserve(count);
		for (size_t i = current_size; i < count; ++i) {
			emplace_back();
		}
	}

	void resize(size_t count, const T& value) {
		size_t current_size = size();
		if (count < current_size) {
			erase(begin() + count, end());
			return;
		}
		if (count == current_size) {
			return;
		}
		reserve(count);
		for (size_t i = current_size; i < count; ++i) {
			push_back(value);
		}
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

	T& at(size_t i) {
		assert(i < size() && "Index out of bounds in InlineVector::at");
		return data()[i];
	}

	const T& at(size_t i) const {
		assert(i < size() && "Index out of bounds in InlineVector::at");
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

	using value_type = T;
	using size_type = size_t;
	using difference_type = std::ptrdiff_t;
	using iterator = T*;
	using const_iterator = const T*;

	iterator begin() noexcept { return data(); }
	iterator end() noexcept { return data() + static_cast<std::ptrdiff_t>(size()); }
	const_iterator begin() const noexcept { return data(); }
	const_iterator end() const noexcept { return data() + static_cast<std::ptrdiff_t>(size()); }
	const_iterator cbegin() const noexcept { return data(); }
	const_iterator cend() const noexcept { return data() + static_cast<std::ptrdiff_t>(size()); }

	iterator insert_at(size_t idx, const T& value) {
		return insertSingleAt(idx, value);
	}

	iterator insert(const_iterator pos, const T& value) {
		return insert_at(static_cast<size_t>(pos - begin()), value);
	}

	iterator insert(iterator pos, const T& value) {
		return insert_at(static_cast<size_t>(pos - begin()), value);
	}

	iterator insert_at(size_t idx, T&& value) {
		return insertSingleAt(idx, std::move(value));
	}

	iterator insert(const_iterator pos, T&& value) {
		return insert_at(static_cast<size_t>(pos - begin()), std::move(value));
	}

	iterator insert(iterator pos, T&& value) {
		return insert_at(static_cast<size_t>(pos - begin()), std::move(value));
	}

	iterator insert(const_iterator pos, size_t count, const T& value) {
		size_t idx = static_cast<size_t>(pos - begin());
		if (count == 0) {
			return begin() + idx;
		}
		if (using_inline_storage_ && inline_count_ + count <= N) {
			for (size_t i = inline_count_; i > idx; --i) {
				inline_data_[i + count - 1] = std::move(inline_data_[i - 1]);
			}
			for (size_t i = 0; i < count; ++i) {
				inline_data_[idx + i] = value;
			}
			inline_count_ = static_cast<uint8_t>(inline_count_ + count);
			return begin() + idx;
		}
		ensureHeapStorage(size() + count);
		heap_data_.insert(
			heap_data_.begin() + static_cast<typename std::vector<T>::difference_type>(idx),
			count,
			value);
		return begin() + idx;
	}

	iterator insert(iterator pos, size_t count, const T& value) {
		return insert(static_cast<const_iterator>(pos), count, value);
	}

	iterator insert(const_iterator pos, std::initializer_list<T> init) {
		return insert_range_at(static_cast<size_t>(pos - begin()), init.begin(), init.end());
	}

	iterator insert(iterator pos, std::initializer_list<T> init) {
		return insert(static_cast<const_iterator>(pos), init);
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

	template <typename... Args>
	iterator emplace(const_iterator pos, Args&&... args) {
		return insert_at(static_cast<size_t>(pos - begin()), T(std::forward<Args>(args)...));
	}

	template <typename... Args>
	iterator emplace(iterator pos, Args&&... args) {
		return emplace(static_cast<const_iterator>(pos), std::forward<Args>(args)...);
	}

	iterator erase(const_iterator pos) {
		return erase(pos, pos + 1);
	}

	iterator erase(iterator pos) {
		return erase(static_cast<const_iterator>(pos));
	}

	iterator erase(const_iterator first, const_iterator last) {
		size_t idx = static_cast<size_t>(first - begin());
		size_t last_idx = static_cast<size_t>(last - begin());
		assert(idx <= last_idx && last_idx <= size() && "Erase range out of bounds in InlineVector::erase");
		if (idx == last_idx) {
			return begin() + idx;
		}
		size_t count = last_idx - idx;

		if (!using_inline_storage_) {
			heap_data_.erase(
				heap_data_.begin() + static_cast<typename std::vector<T>::difference_type>(idx),
				heap_data_.begin() + static_cast<typename std::vector<T>::difference_type>(last_idx));
			return begin() + idx;
		}

		for (size_t i = idx; i + count < inline_count_; ++i) {
			inline_data_[i] = std::move(inline_data_[i + count]);
		}
		for (size_t i = inline_count_ - count; i < inline_count_; ++i) {
			inline_data_[i] = T{};
		}
		inline_count_ = static_cast<uint8_t>(inline_count_ - count);
		return begin() + idx;
	}

	iterator erase(iterator first, iterator last) {
		return erase(static_cast<const_iterator>(first), static_cast<const_iterator>(last));
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

	template <typename ForwardedT>
	iterator insertSingleAt(size_t idx, ForwardedT&& value) {
		assert(idx <= size() && "Insert position out of bounds in InlineVector::insert_at");
		T inserted_value(std::forward<ForwardedT>(value));
		if (using_inline_storage_ && inline_count_ < N) {
			for (size_t i = inline_count_; i > idx; --i) {
				inline_data_[i] = std::move(inline_data_[i - 1]);
			}
			inline_data_[idx] = std::move(inserted_value);
			++inline_count_;
			return begin() + idx;
		}
		ensureHeapStorage(size() + 1);
		heap_data_.insert(
			heap_data_.begin() + static_cast<typename std::vector<T>::difference_type>(idx),
			std::move(inserted_value));
		return begin() + idx;
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
		using_inline_storage_ = true;
	}

	void ensureHeapStorage(size_t capacity) {
		if (!using_inline_storage_) {
			return;
		}

#if FLASHCPP_TRACK_INLINE_VECTOR_SPILLS
		++gInlineVectorSpillCount;
		++gInlineVectorSpillFamilyCounts[static_cast<std::size_t>(SpillFamily)];
#endif
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

	void moveFrom(InlineVector&& other) noexcept(std::is_nothrow_move_assignable_v<T> && std::is_nothrow_default_constructible_v<T>) {
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

	void assignFromVector(std::span<const T> vec) {
		resetStorage();
		using_inline_storage_ = vec.size() <= N;
		if (!using_inline_storage_) {
			heap_data_.assign(vec.begin(), vec.end());
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

inline uint64_t inlineVectorSpillCount() {
#if FLASHCPP_TRACK_INLINE_VECTOR_SPILLS
	return gInlineVectorSpillCount;
#else
	return 0;
#endif
}

inline uint64_t inlineVectorSpillCount(InlineVectorSpillFamily family) {
#if FLASHCPP_TRACK_INLINE_VECTOR_SPILLS
	if (static_cast<std::size_t>(family) >= gInlineVectorSpillFamilyCounts.size()) {
		return 0;
	}
	return gInlineVectorSpillFamilyCounts[static_cast<std::size_t>(family)];
#else
	(void)family;
	return 0;
#endif
}

inline const std::array<uint64_t, static_cast<std::size_t>(InlineVectorSpillFamily::Count)>&
inlineVectorSpillFamilyCounts() {
#if FLASHCPP_TRACK_INLINE_VECTOR_SPILLS
	return gInlineVectorSpillFamilyCounts;
#else
	static const std::array<uint64_t, static_cast<std::size_t>(InlineVectorSpillFamily::Count)> empty{};
	return empty;
#endif
}

} // namespace FlashCpp

namespace FlashCpp {

void* allocatePersistentArena(std::size_t size, std::size_t alignment);
uint64_t persistentArenaUsedBytes();
uint64_t persistentArenaPeakBytes();
uint64_t persistentArenaReservedBytes();
uint64_t persistentArenaPeakReservedBytes();

template <typename T> class ArenaVector {
public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using iterator = T*;
	using const_iterator = const T*;

	ArenaVector() = default;

	ArenaVector(std::initializer_list<T> init) {
		for (const T& value : init) { emplace_back(value); }
	}

	ArenaVector(std::span<const T> values) {
		for (const T& value : values) { emplace_back(value); }
	}

	template <std::input_iterator InputIt> ArenaVector(InputIt first, InputIt last) {
		for (; first != last; ++first) { emplace_back(*first); }
	}

	template <typename Range>
		requires(!std::same_as<std::remove_cvref_t<Range>, ArenaVector>)
	ArenaVector(Range&& range) {
		for (auto&& value : range) { emplace_back(value); }
	}

	ArenaVector(const ArenaVector& other) {
		for (const T& value : other) { emplace_back(value); }
	}

	ArenaVector(ArenaVector&& other) noexcept : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
		other.data_ = nullptr;
		other.size_ = 0;
		other.capacity_ = 0;
	}

	~ArenaVector() { destroyElements(); }

	ArenaVector& operator=(const ArenaVector& other) {
		if (this != &other) {
			destroyElements();
			data_ = nullptr;
			size_ = 0;
			capacity_ = 0;
			for (const T& value : other) { emplace_back(value); }
		}
		return *this;
	}

	ArenaVector& operator=(ArenaVector&& other) noexcept {
		if (this != &other) {
			destroyElements();
			data_ = other.data_;
			size_ = other.size_;
			capacity_ = other.capacity_;
			other.data_ = nullptr;
			other.size_ = 0;
			other.capacity_ = 0;
		}
		return *this;
	}

	void push_back(const T& value) { emplace_back(value); }
	void push_back(T&& value) { emplace_back(std::move(value)); }

	template <typename... Args> T& emplace_back(Args&&... args) {
		if (size_ == capacity_) { reserve(capacity_ == 0 ? 4 : capacity_ * 2); }
		T* slot = data_ + size_;
		::new (static_cast<void*>(slot)) T(std::forward<Args>(args)...);
		++size_;
		return *slot;
	}

	[[nodiscard]] size_t size() const noexcept { return size_; }
	[[nodiscard]] bool empty() const noexcept { return size_ == 0; }
	void pop_back() {
		assert(size_ > 0);
		--size_;
		data_[size_].~T();
	}
	void clear() noexcept {
		destroyElements();
		size_ = 0;
	}
	[[nodiscard]] size_t capacity() const noexcept { return capacity_; }

	template <std::input_iterator InputIt> void assign(InputIt first, InputIt last) {
		clear();
		for (; first != last; ++first) { emplace_back(*first); }
	}

	void assign(size_t count, const T& value) {
		clear();
		for (size_t index = 0; index < count; ++index) { emplace_back(value); }
	}

	void assign(std::initializer_list<T> init) { assign(init.begin(), init.end()); }

	void reserve(size_t capacity) {
		if (capacity <= capacity_) { return; }
		T* new_data = allocateRaw(capacity);
		size_t constructed = 0;
		try {
			for (; constructed < size_; ++constructed) {
				::new (static_cast<void*>(new_data + constructed)) T(std::move_if_noexcept(data_[constructed]));
			}
		} catch (...) {
			for (size_t index = 0; index < constructed; ++index) { new_data[index].~T(); }
			throw;
		}
		destroyElements();
		data_ = new_data;
		capacity_ = capacity;
	}

	void shrink_to_fit() {}

	void resize(size_t count) {
		while (size_ > count) { pop_back(); }
		while (size_ < count) { emplace_back(); }
	}

	void resize(size_t count, const T& value) {
		while (size_ > count) { pop_back(); }
		while (size_ < count) { emplace_back(value); }
	}

	[[nodiscard]] T* data() noexcept { return data_; }
	[[nodiscard]] const T* data() const noexcept { return data_; }

	T& operator[](size_t index) { return data_[index]; }
	const T& operator[](size_t index) const { return data_[index]; }
	T& at(size_t index) { return data_[index]; }
	const T& at(size_t index) const { return data_[index]; }
	T& front() { return data_[0]; }
	const T& front() const { return data_[0]; }
	T& back() { return data_[size_ - 1]; }
	const T& back() const { return data_[size_ - 1]; }

	bool operator==(const ArenaVector& other) const { return size_ == other.size_ && std::equal(begin(), end(), other.begin()); }

	bool operator!=(const ArenaVector& other) const { return !(*this == other); }

	iterator begin() noexcept { return data_; }
	iterator end() noexcept { return data_ + size_; }
	const_iterator begin() const noexcept { return data_; }
	const_iterator end() const noexcept { return data_ + size_; }
	const_iterator cbegin() const noexcept { return data_; }
	const_iterator cend() const noexcept { return data_ + size_; }

	iterator insert_at(size_t index, const T& value) {
		T inserted(value);
		return insertPrepared(index, std::move(inserted));
	}

	iterator insert_at(size_t index, T&& value) {
		if (referencesElements(value)) {
			T inserted(value);
			return insertPrepared(index, std::move(inserted));
		}
		T inserted(std::move(value));
		return insertPrepared(index, std::move(inserted));
	}

	iterator insert(const_iterator pos, const T& value) { return insert_at(static_cast<size_t>(pos - begin()), value); }
	iterator insert(iterator pos, const T& value) { return insert_at(static_cast<size_t>(pos - begin()), value); }
	iterator insert(const_iterator pos, T&& value) { return insert_at(static_cast<size_t>(pos - begin()), std::move(value)); }
	iterator insert(iterator pos, T&& value) { return insert_at(static_cast<size_t>(pos - begin()), std::move(value)); }

	iterator insert(const_iterator pos, size_t count, const T& value) {
		size_t index = static_cast<size_t>(pos - begin());
		ArenaVector<T> inserted;
		inserted.reserve(count);
		for (size_t item = 0; item < count; ++item) { inserted.emplace_back(value); }
		return insertRangeAt(index, inserted.begin(), inserted.end());
	}

	iterator insert(iterator pos, size_t count, const T& value) { return insert(static_cast<const_iterator>(pos), count, value); }

	iterator insert(const_iterator pos, std::initializer_list<T> init) {
		return insertRangeAt(static_cast<size_t>(pos - begin()), init.begin(), init.end());
	}

	iterator insert(iterator pos, std::initializer_list<T> init) { return insert(static_cast<const_iterator>(pos), init.begin(), init.end()); }

	template <typename Iter> iterator insert_range_at(size_t index, Iter first, Iter last) {
		ArenaVector<T> inserted(first, last);
		return insertRangeAt(index, inserted.begin(), inserted.end());
	}

	template <typename Iter> iterator insertRangeAt(size_t index, Iter first, Iter last) {
		const size_t count = static_cast<size_t>(last - first);
		if (count == 0) { return begin() + index; }
		const size_t new_size = size_ + count;
		const size_t new_capacity = capacity_ >= new_size ? capacity_ : new_size * 2;
		T* new_data = allocateRaw(new_capacity);
		size_t out = 0;
		for (size_t source = 0; source < index; ++source) { ::new (static_cast<void*>(new_data + out++)) T(std::move(data_[source])); }
		for (Iter item = first; item != last; ++item) { ::new (static_cast<void*>(new_data + out++)) T(*item); }
		for (size_t source = index; source < size_; ++source) { ::new (static_cast<void*>(new_data + out++)) T(std::move(data_[source])); }
		destroyElements();
		data_ = new_data;
		size_ = new_size;
		capacity_ = new_capacity;
		return data_ + index;
	}

	template <typename... Args> iterator emplace(const_iterator pos, Args&&... args) {
		return insert_at(static_cast<size_t>(pos - begin()), T(std::forward<Args>(args)...));
	}

	template <typename... Args> iterator emplace(iterator pos, Args&&... args) {
		return emplace(static_cast<const_iterator>(pos), std::forward<Args>(args)...);
	}

	iterator erase(const_iterator pos) { return erase(pos, pos + 1); }
	iterator erase(iterator pos) { return erase(static_cast<const_iterator>(pos)); }

	iterator erase(const_iterator first, const_iterator last) {
		const size_t index = static_cast<size_t>(first - begin());
		const size_t count = static_cast<size_t>(last - first);
		if (count == 0) { return begin() + index; }
		const size_t new_size = size_ - count;
		T* new_data = allocateRaw(capacity_);
		size_t out = 0;
		for (size_t source = 0; source < index; ++source) { ::new (static_cast<void*>(new_data + out++)) T(std::move(data_[source])); }
		for (size_t source = index + count; source < size_; ++source) { ::new (static_cast<void*>(new_data + out++)) T(std::move(data_[source])); }
		destroyElements();
		data_ = new_data;
		size_ = new_size;
		return data_ + index;
	}

	iterator erase(iterator first, iterator last) { return erase(static_cast<const_iterator>(first), static_cast<const_iterator>(last)); }

private:
	static T* allocateRaw(size_t capacity) {
		if (capacity == 0) { return nullptr; }
		if (capacity > std::numeric_limits<size_t>::max() / sizeof(T)) { throw std::bad_array_new_length(); }
		return static_cast<T*>(allocatePersistentArena(sizeof(T) * capacity, alignof(T)));
	}

	bool referencesElements(const T& value) const noexcept {
		const auto address = reinterpret_cast<std::uintptr_t>(std::addressof(value));
		const auto begin_address = reinterpret_cast<std::uintptr_t>(data_);
		const auto end_address = begin_address + size_ * sizeof(T);
		return address >= begin_address && address < end_address;
	}

	iterator insertPrepared(size_t index, T&& value) {
		const size_t new_size = size_ + 1;
		const size_t new_capacity = capacity_ >= new_size ? capacity_ : new_size * 2;
		T* new_data = allocateRaw(new_capacity);
		size_t out = 0;
		for (size_t source = 0; source < index; ++source) { ::new (static_cast<void*>(new_data + out++)) T(std::move(data_[source])); }
		::new (static_cast<void*>(new_data + out++)) T(std::move(value));
		for (size_t source = index; source < size_; ++source) { ::new (static_cast<void*>(new_data + out++)) T(std::move(data_[source])); }
		destroyElements();
		data_ = new_data;
		size_ = new_size;
		capacity_ = new_capacity;
		return data_ + index;
	}

	void destroyElements() noexcept {
		for (size_t index = 0; index < size_; ++index) { data_[index].~T(); }
	}

	T* data_ = nullptr;
	size_t size_ = 0;
	size_t capacity_ = 0;
};

// Compact owning vector for large element types on parser and template paths.
// Storage is out of line so locals and optional copies do not embed inline
// elements; std::span views remain valid only until the next mutation.
template <typename T> class OutOfLineVector {
public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using iterator = typename ArenaVector<T>::iterator;
	using const_iterator = typename ArenaVector<T>::const_iterator;

	OutOfLineVector() = default;

	OutOfLineVector(std::initializer_list<T> init) : values_(init) {}

	OutOfLineVector(std::span<const T> values) { values_.assign(values.begin(), values.end()); }

	template <std::input_iterator InputIt> OutOfLineVector(InputIt first, InputIt last) { values_.assign(first, last); }

	OutOfLineVector(std::vector<T>&& values) { values_.assign(values.begin(), values.end()); }

	template <typename Range>
		requires(!std::same_as<std::remove_cvref_t<Range>, OutOfLineVector>)
	OutOfLineVector(Range&& range) {
		values_.assign(std::begin(range), std::end(range));
	}

	OutOfLineVector& operator=(std::span<const T> values) {
		values_.assign(values.begin(), values.end());
		return *this;
	}

	OutOfLineVector& operator=(std::vector<T>&& values) {
		values_.assign(values.begin(), values.end());
		return *this;
	}

	OutOfLineVector& operator=(std::initializer_list<T> init) {
		values_.assign(init.begin(), init.end());
		return *this;
	}

	template <typename Range>
		requires(!std::same_as<std::remove_cvref_t<Range>, OutOfLineVector>)
	OutOfLineVector& operator=(Range&& range) {
		values_.assign(std::begin(range), std::end(range));
		return *this;
	}

	[[nodiscard]] std::vector<T> toVector() const& { return std::vector<T>(values_.begin(), values_.end()); }
	[[nodiscard]] std::vector<T> toVector() && { return std::vector<T>(values_.begin(), values_.end()); }

	void push_back(const T& value) { values_.push_back(value); }
	void push_back(T&& value) { values_.push_back(std::move(value)); }

	template <typename... Args> void emplace_back(Args&&... args) { values_.emplace_back(std::forward<Args>(args)...); }

	[[nodiscard]] size_t size() const noexcept { return values_.size(); }
	[[nodiscard]] bool empty() const noexcept { return values_.empty(); }
	void pop_back() { values_.pop_back(); }
	void clear() noexcept { values_.clear(); }
	[[nodiscard]] size_t capacity() const noexcept { return values_.capacity(); }

	template <std::input_iterator InputIt> void assign(InputIt first, InputIt last) { values_.assign(first, last); }

	void assign(size_t count, const T& value) { values_.assign(count, value); }
	void assign(std::initializer_list<T> init) { values_.assign(init.begin(), init.end()); }

	void reserve(size_t capacity) { values_.reserve(capacity); }
	void shrink_to_fit() { values_.shrink_to_fit(); }
	void resize(size_t count) { values_.resize(count); }
	void resize(size_t count, const T& value) { values_.resize(count, value); }

	[[nodiscard]] T* data() noexcept { return values_.data(); }
	[[nodiscard]] const T* data() const noexcept { return values_.data(); }

	operator std::span<T>() noexcept { return std::span<T>(data(), size()); }
	operator std::span<const T>() const noexcept { return std::span<const T>(data(), size()); }

	T& operator[](size_t index) { return values_[index]; }
	const T& operator[](size_t index) const { return values_[index]; }
	T& at(size_t index) { return values_.at(index); }
	const T& at(size_t index) const { return values_.at(index); }
	T& front() { return values_.front(); }
	const T& front() const { return values_.front(); }
	T& back() { return values_.back(); }
	const T& back() const { return values_.back(); }

	bool operator==(const OutOfLineVector& other) const { return values_ == other.values_; }
	bool operator!=(const OutOfLineVector& other) const { return values_ != other.values_; }

	iterator begin() noexcept { return values_.begin(); }
	iterator end() noexcept { return values_.end(); }
	const_iterator begin() const noexcept { return values_.begin(); }
	const_iterator end() const noexcept { return values_.end(); }
	const_iterator cbegin() const noexcept { return values_.cbegin(); }
	const_iterator cend() const noexcept { return values_.cend(); }

	iterator insert_at(size_t index, const T& value) { return values_.insert(values_.begin() + static_cast<difference_type>(index), value); }

	iterator insert(const_iterator pos, const T& value) { return values_.insert(pos, value); }
	iterator insert(iterator pos, const T& value) { return values_.insert(pos, value); }

	iterator insert_at(size_t index, T&& value) { return values_.insert(values_.begin() + static_cast<difference_type>(index), std::move(value)); }

	iterator insert(const_iterator pos, T&& value) { return values_.insert(pos, std::move(value)); }
	iterator insert(iterator pos, T&& value) { return values_.insert(pos, std::move(value)); }

	iterator insert(const_iterator pos, size_t count, const T& value) { return values_.insert(pos, count, value); }
	iterator insert(iterator pos, size_t count, const T& value) { return values_.insert(pos, count, value); }

	iterator insert(const_iterator pos, std::initializer_list<T> init) { return values_.insert(pos, init); }
	iterator insert(iterator pos, std::initializer_list<T> init) { return values_.insert(pos, init); }

	template <typename Iter> iterator insert_range_at(size_t index, Iter first, Iter last) {
		ArenaVector<T> inserted(first, last);
		return values_.insert_range_at(index, inserted.begin(), inserted.end());
	}

	iterator insert(const_iterator pos, const_iterator first, const_iterator last) {
		return insert_range_at(static_cast<size_t>(pos - begin()), first, last);
	}

	iterator insert(iterator pos, iterator first, iterator last) { return insert_range_at(static_cast<size_t>(pos - begin()), first, last); }

	template <typename... Args> iterator emplace(const_iterator pos, Args&&... args) { return values_.emplace(pos, std::forward<Args>(args)...); }

	template <typename... Args> iterator emplace(iterator pos, Args&&... args) { return values_.emplace(pos, std::forward<Args>(args)...); }

	iterator erase(const_iterator pos) { return values_.erase(pos); }
	iterator erase(iterator pos) { return values_.erase(pos); }
	iterator erase(const_iterator first, const_iterator last) { return values_.erase(first, last); }
	iterator erase(iterator first, iterator last) { return values_.erase(first, last); }

private:
	ArenaVector<T> values_;
};

template <typename T, size_t N = 4>
using OverloadVector = InlineVector<T, InlineVectorSpillFamily::OverloadResolution, N>;

template <typename T, size_t N = 4>
using TemplateVector = InlineVector<T, InlineVectorSpillFamily::TemplateArgument, N>;

template <typename T, size_t N = 4>
using QualifiedLookupVector = InlineVector<T, InlineVectorSpillFamily::QualifiedLookup, N>;

} // namespace FlashCpp

// Make InlineVector available outside namespace for convenience
using FlashCpp::InlineVector;
using FlashCpp::OverloadVector;
using FlashCpp::TemplateVector;
