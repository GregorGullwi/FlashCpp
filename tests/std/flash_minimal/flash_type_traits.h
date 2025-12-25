// flash_type_traits.h - Minimal type traits for FlashCpp
// This is a simplified, working implementation that uses FlashCpp's compiler intrinsics
// instead of the full standard library complexity.

#ifndef FLASH_TYPE_TRAITS_H
#define FLASH_TYPE_TRAITS_H

namespace flash_std {

// ===== integral_constant =====
// Core building block for type traits
template<typename T, T v>
struct integral_constant {
	static constexpr T value = v;
	using value_type = T;
	using type = integral_constant<T, v>;
	
	constexpr operator T() const noexcept { return value; }
	constexpr T operator()() const noexcept { return value; }
};

// Boolean specializations
template<bool B>
using bool_constant = integral_constant<bool, B>;

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

// ===== Type Relationships =====

// is_same - Check if two types are identical
template<typename T, typename U>
struct is_same : false_type {};

template<typename T>
struct is_same<T, T> : true_type {};

template<typename T, typename U>
inline constexpr bool is_same_v = is_same<T, U>::value;

// is_base_of - Check inheritance relationship
template<typename Base, typename Derived>
struct is_base_of : bool_constant<__is_base_of(Base, Derived)> {};

template<typename Base, typename Derived>
inline constexpr bool is_base_of_v = is_base_of<Base, Derived>::value;

// ===== Primary Type Categories =====

// is_void
template<typename T>
struct is_void : bool_constant<__is_void(T)> {};

template<typename T>
inline constexpr bool is_void_v = is_void<T>::value;

// is_integral
template<typename T>
struct is_integral : bool_constant<__is_integral(T)> {};

template<typename T>
inline constexpr bool is_integral_v = is_integral<T>::value;

// is_floating_point
template<typename T>
struct is_floating_point : bool_constant<__is_floating_point(T)> {};

template<typename T>
inline constexpr bool is_floating_point_v = is_floating_point<T>::value;

// is_array
template<typename T>
struct is_array : bool_constant<__is_array(T)> {};

template<typename T>
inline constexpr bool is_array_v = is_array<T>::value;

// is_pointer
template<typename T>
struct is_pointer : bool_constant<__is_pointer(T)> {};

template<typename T>
inline constexpr bool is_pointer_v = is_pointer<T>::value;

// is_reference
template<typename T>
struct is_reference : bool_constant<__is_reference(T)> {};

template<typename T>
inline constexpr bool is_reference_v = is_reference<T>::value;

// is_lvalue_reference
template<typename T>
struct is_lvalue_reference : bool_constant<__is_lvalue_reference(T)> {};

template<typename T>
inline constexpr bool is_lvalue_reference_v = is_lvalue_reference<T>::value;

// is_rvalue_reference
template<typename T>
struct is_rvalue_reference : bool_constant<__is_rvalue_reference(T)> {};

template<typename T>
inline constexpr bool is_rvalue_reference_v = is_rvalue_reference<T>::value;

// is_class
template<typename T>
struct is_class : bool_constant<__is_class(T)> {};

template<typename T>
inline constexpr bool is_class_v = is_class<T>::value;

// is_enum
template<typename T>
struct is_enum : bool_constant<__is_enum(T)> {};

template<typename T>
inline constexpr bool is_enum_v = is_enum<T>::value;

// is_union
template<typename T>
struct is_union : bool_constant<__is_union(T)> {};

template<typename T>
inline constexpr bool is_union_v = is_union<T>::value;

// ===== Type Properties =====

// is_const
template<typename T>
struct is_const : bool_constant<__is_const(T)> {};

template<typename T>
inline constexpr bool is_const_v = is_const<T>::value;

// is_volatile
template<typename T>
struct is_volatile : bool_constant<__is_volatile(T)> {};

template<typename T>
inline constexpr bool is_volatile_v = is_volatile<T>::value;

// is_signed
template<typename T>
struct is_signed : bool_constant<__is_signed(T)> {};

template<typename T>
inline constexpr bool is_signed_v = is_signed<T>::value;

// is_unsigned
template<typename T>
struct is_unsigned : bool_constant<__is_unsigned(T)> {};

template<typename T>
inline constexpr bool is_unsigned_v = is_unsigned<T>::value;

// is_pod
template<typename T>
struct is_pod : bool_constant<__is_pod(T)> {};

template<typename T>
inline constexpr bool is_pod_v = is_pod<T>::value;

// is_trivially_copyable
template<typename T>
struct is_trivially_copyable : bool_constant<__is_trivially_copyable(T)> {};

template<typename T>
inline constexpr bool is_trivially_copyable_v = is_trivially_copyable<T>::value;

// is_polymorphic
template<typename T>
struct is_polymorphic : bool_constant<__is_polymorphic(T)> {};

template<typename T>
inline constexpr bool is_polymorphic_v = is_polymorphic<T>::value;

// is_abstract
template<typename T>
struct is_abstract : bool_constant<__is_abstract(T)> {};

template<typename T>
inline constexpr bool is_abstract_v = is_abstract<T>::value;

// is_final
template<typename T>
struct is_final : bool_constant<__is_final(T)> {};

template<typename T>
inline constexpr bool is_final_v = is_final<T>::value;

// is_aggregate
template<typename T>
struct is_aggregate : bool_constant<__is_aggregate(T)> {};

template<typename T>
inline constexpr bool is_aggregate_v = is_aggregate<T>::value;

// Note: __has_virtual_destructor is not available in FlashCpp yet
// has_virtual_destructor would require implementation of __has_virtual_destructor intrinsic

// ===== Type Modifications =====

// remove_const
template<typename T>
struct remove_const { typedef T type; };

template<typename T>
struct remove_const<const T> { typedef T type; };

template<typename T>
using remove_const_t = typename remove_const<T>::type;

// remove_volatile
template<typename T>
struct remove_volatile { typedef T type; };

template<typename T>
struct remove_volatile<volatile T> { typedef T type; };

template<typename T>
using remove_volatile_t = typename remove_volatile<T>::type;

// remove_cv
template<typename T>
struct remove_cv {
	typedef typename remove_volatile<typename remove_const<T>::type>::type type;
};

template<typename T>
using remove_cv_t = typename remove_cv<T>::type;

// remove_reference
template<typename T>
struct remove_reference { typedef T type; };

template<typename T>
struct remove_reference<T&> { typedef T type; };

template<typename T>
struct remove_reference<T&&> { typedef T type; };

template<typename T>
using remove_reference_t = typename remove_reference<T>::type;

// remove_pointer
template<typename T>
struct remove_pointer { typedef T type; };

template<typename T>
struct remove_pointer<T*> { typedef T type; };

template<typename T>
using remove_pointer_t = typename remove_pointer<T>::type;

// add_const
template<typename T>
struct add_const;

template<typename T>
struct add_const { typedef const T type; };

template<typename T>
using add_const_t = typename add_const<T>::type;

// add_volatile
template<typename T>
struct add_volatile { typedef volatile T type; };

template<typename T>
using add_volatile_t = typename add_volatile<T>::type;

// add_cv
template<typename T>
struct add_cv { typedef const volatile T type; };

template<typename T>
using add_cv_t = typename add_cv<T>::type;

// Note: add_lvalue_reference, add_rvalue_reference, add_pointer not supported yet
// FlashCpp parser has issues with pointer/reference types in template type aliases

// ===== Miscellaneous Transformations =====

// conditional - Select type based on boolean condition
template<bool B, typename T, typename F>
struct conditional { typedef T type; };

template<typename T, typename F>
struct conditional<false, T, F> { typedef F type; };

template<bool B, typename T, typename F>
using conditional_t = typename conditional<B, T, F>::type;

// enable_if - SFINAE helper
template<bool B, typename T = void>
struct enable_if {};

template<typename T>
struct enable_if<true, T> { typedef T type; };

template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

// void_t - C++17 void_t pattern
template<typename...>
using void_t = void;

// ===== Composite Type Categories =====

// is_arithmetic - Note: Simplified implementation without using expressions in template args
template<typename T>
struct is_arithmetic {
	static constexpr bool value = is_integral_v<T> || is_floating_point_v<T>;
};

template<typename T>
inline constexpr bool is_arithmetic_v = is_arithmetic<T>::value;

// is_fundamental
template<typename T>
struct is_fundamental {
	static constexpr bool value = is_arithmetic_v<T> || is_void_v<T>;
};

template<typename T>
inline constexpr bool is_fundamental_v = is_fundamental<T>::value;

// is_compound
template<typename T>
struct is_compound {
	static constexpr bool value = !is_fundamental_v<T>;
};

template<typename T>
inline constexpr bool is_compound_v = is_compound<T>::value;

// is_object
template<typename T>
struct is_object {
	static constexpr bool value = !is_reference_v<T> && !is_void_v<T>;
};

template<typename T>
inline constexpr bool is_object_v = is_object<T>::value;

// is_scalar
template<typename T>
struct is_scalar {
	static constexpr bool value = is_arithmetic_v<T> || is_enum_v<T> || is_pointer_v<T>;
};

template<typename T>
inline constexpr bool is_scalar_v = is_scalar<T>::value;

} // namespace flash_std

#endif // FLASH_TYPE_TRAITS_H
