// flash_utility.h - Minimal utility functions for FlashCpp
// Provides std::move, std::forward, std::pair, and other utility functions

#ifndef FLASH_UTILITY_H
#define FLASH_UTILITY_H

#include "flash_type_traits.h"

namespace flash_std {

// ===== move and forward =====

// move - Cast to rvalue reference
template<typename T>
constexpr typename remove_reference<T>::type&& move(T&& t) noexcept {
	return static_cast<typename remove_reference<T>::type&&>(t);
}

// forward - Perfect forwarding for lvalue references
template<typename T>
constexpr T&& forward(typename remove_reference<T>::type& t) noexcept {
	return static_cast<T&&>(t);
}

// forward - Perfect forwarding for rvalue references
template<typename T>
constexpr T&& forward(typename remove_reference<T>::type&& t) noexcept {
	return static_cast<T&&>(t);
}

// ===== addressof =====

// addressof - Get actual address, bypassing operator&
template<typename T>
constexpr T* addressof(T& arg) noexcept {
	return __builtin_addressof(arg);
}

// ===== swap =====

// swap - Exchange values of two objects
template<typename T>
constexpr void swap(T& a, T& b) noexcept {
	T temp = move(a);
	a = move(b);
	b = move(temp);
}

// ===== pair =====

// pair - Store two values of possibly different types
template<typename T1, typename T2>
struct pair {
	using first_type = T1;
	using second_type = T2;
	
	T1 first;
	T2 second;
	
	// Default constructor
	constexpr pair() : first(), second() {}
	
	// Constructor from values
	constexpr pair(const T1& x, const T2& y) : first(x), second(y) {}
	
	// Copy constructor
	constexpr pair(const pair& other) = default;
	
	// Move constructor
	constexpr pair(pair&& other) noexcept
		: first(move(other.first)), second(move(other.second)) {}
	
	// Copy assignment
	constexpr pair& operator=(const pair& other) {
		first = other.first;
		second = other.second;
		return *this;
	}
	
	// Move assignment
	constexpr pair& operator=(pair&& other) noexcept {
		first = move(other.first);
		second = move(other.second);
		return *this;
	}
	
	// Swap
	constexpr void swap(pair& other) noexcept {
		flash_std::swap(first, other.first);
		flash_std::swap(second, other.second);
	}
};

// Comparison operators for pair
template<typename T1, typename T2>
constexpr bool operator==(const pair<T1, T2>& lhs, const pair<T1, T2>& rhs) {
	return lhs.first == rhs.first && lhs.second == rhs.second;
}

template<typename T1, typename T2>
constexpr bool operator!=(const pair<T1, T2>& lhs, const pair<T1, T2>& rhs) {
	return !(lhs == rhs);
}

template<typename T1, typename T2>
constexpr bool operator<(const pair<T1, T2>& lhs, const pair<T1, T2>& rhs) {
	if (lhs.first < rhs.first) return true;
	if (rhs.first < lhs.first) return false;
	return lhs.second < rhs.second;
}

template<typename T1, typename T2>
constexpr bool operator<=(const pair<T1, T2>& lhs, const pair<T1, T2>& rhs) {
	return !(rhs < lhs);
}

template<typename T1, typename T2>
constexpr bool operator>(const pair<T1, T2>& lhs, const pair<T1, T2>& rhs) {
	return rhs < lhs;
}

template<typename T1, typename T2>
constexpr bool operator>=(const pair<T1, T2>& lhs, const pair<T1, T2>& rhs) {
	return !(lhs < rhs);
}

// make_pair - Construct a pair object with automatic type deduction
template<typename T1, typename T2>
constexpr pair<T1, T2> make_pair(T1 first, T2 second) {
	return pair<T1, T2>(first, second);
}

// ===== declval =====

// Note: declval cannot be properly implemented without add_rvalue_reference
// This is a simplified version that just returns T&& directly
template<typename T>
T&& declval() noexcept;

// ===== exchange =====

// exchange - Replace the value of obj with new_value and return the old value
template<typename T, typename U = T>
constexpr T exchange(T& obj, U&& new_value) {
	T old_value = move(obj);
	obj = forward<U>(new_value);
	return old_value;
}

// ===== as_const =====

// as_const - Obtain a const lvalue reference
template<typename T>
constexpr typename add_const<T>::type& as_const(T& t) noexcept {
	return t;
}

// ===== Index sequences (for tuple-like access) =====

// integer_sequence - Compile-time sequence of integers
template<typename T, T... Ints>
struct integer_sequence {
	using value_type = T;
	static constexpr unsigned long long size() noexcept { return sizeof...(Ints); }
};

// index_sequence - Sequence of size_t
template<unsigned long long... Ints>
using index_sequence = integer_sequence<unsigned long long, Ints...>;

// Helper to make sequences (simplified version)
namespace detail {
	template<unsigned long long N, unsigned long long... Seq>
	struct make_index_sequence_impl {
		using type = typename make_index_sequence_impl<N - 1, N - 1, Seq...>::type;
	};
	
	template<unsigned long long... Seq>
	struct make_index_sequence_impl<0, Seq...> {
		using type = index_sequence<Seq...>;
	};
}

template<unsigned long long N>
using make_index_sequence = typename detail::make_index_sequence_impl<N>::type;

template<typename... T>
using index_sequence_for = make_index_sequence<sizeof...(T)>;

} // namespace flash_std

#endif // FLASH_UTILITY_H
