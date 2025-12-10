// EXPECTED FAIL TEST: Standard C++20 Header Support
// This file is designed to test FlashCpp's ability to handle standard library headers
// It should be expected to fail compilation (hence the _fail.cpp suffix)
// The goal is to document what features FlashCpp is missing to support std headers

// This test intentionally uses features commonly found in standard headers
// to help identify missing compiler features

// 1. Test: Template alias with inheritance (common in type_traits)
template<typename T>
struct remove_const_base { using type = T; };

template<typename T>
struct remove_const_base<const T> { using type = T; };

template<typename T>
using remove_const_t = typename remove_const_base<T>::type;

// 2. Test: Simple type aliases for true/false types
// Note: FlashCpp has issues with non-type bool template parameters accessing value
// So we use separate explicit types
struct std_true_type {
    static constexpr bool value = true;
};

struct std_false_type {
    static constexpr bool value = false;
};

// 3. Test: Conditional type selection (std::conditional)
template<bool B, typename T, typename F>
struct conditional_impl { using type = T; };

template<typename T, typename F>
struct conditional_impl<false, T, F> { using type = F; };

template<bool B, typename T, typename F>
using conditional = typename conditional_impl<B, T, F>::type;

// 4. Test: SFINAE-friendly enable_if
template<bool B, typename T = void>
struct enable_if_impl {};

template<typename T>
struct enable_if_impl<true, T> { using type = T; };

template<bool B, typename T = void>
using enable_if = typename enable_if_impl<B, T>::type;

// 5. Test: Type property checking
template<typename T>
struct is_pointer_impl : std_false_type {};

template<typename T>
struct is_pointer_impl<T*> : std_true_type {};

template<typename T>
constexpr bool is_pointer_v = is_pointer_impl<T>::value;

// 6. Test: Reference traits
template<typename T>
struct is_lvalue_reference_impl : std_false_type {};

template<typename T>
struct is_lvalue_reference_impl<T&> : std_true_type {};

template<typename T>
constexpr bool is_lvalue_reference_v = is_lvalue_reference_impl<T>::value;

// 7. Test: Const traits
template<typename T>
struct is_const_impl : std_false_type {};

template<typename T>
struct is_const_impl<const T> : std_true_type {};

template<typename T>
constexpr bool is_const_v = is_const_impl<T>::value;

int main() {
    // Test basic usage
    using plain_int = remove_const_t<const int>;
    
    // Test type traits base types
    constexpr bool is_true = std_true_type::value;
    constexpr bool is_false = std_false_type::value;
    
    // Test conditional
    using result1 = conditional<true, int, double>;   // should be int
    using result2 = conditional<false, int, double>;  // should be double
    
    // Test pointer detection
    constexpr bool ptr_check1 = is_pointer_v<int*>;   // should be true
    constexpr bool ptr_check2 = is_pointer_v<int>;    // should be false
    
    // Test reference detection
    constexpr bool ref_check1 = is_lvalue_reference_v<int&>;  // should be true
    constexpr bool ref_check2 = is_lvalue_reference_v<int>;   // should be false
    
    // Test const detection
    constexpr bool const_check1 = is_const_v<const int>;  // should be true
    constexpr bool const_check2 = is_const_v<int>;        // should be false
    
    // Verify all values
    if (!is_true) return 1;
    if (is_false) return 2;
    if (!ptr_check1) return 3;
    if (ptr_check2) return 4;
    if (!ref_check1) return 5;
    if (ref_check2) return 6;
    if (!const_check1) return 7;
    if (const_check2) return 8;
    
    return 0;
}

/*
 * MISSING FEATURES IDENTIFIED FOR STANDARD HEADER SUPPORT:
 * 
 * Based on attempting to include standard C++20 headers:
 * 
 * 1. INTEGRAL_CONSTANT INHERITANCE:
 *    - Headers like <type_traits> use: template<typename T, T v> struct integral_constant
 *    - This requires non-type template parameters of arbitrary type
 *    - Specializations often inherit from integral_constant
 * 
 * 2. CONVERSION OPERATORS:
 *    - constexpr operator value_type() const - implicit conversion operator
 *    - FlashCpp may need better support for user-defined conversion operators
 * 
 * 3. FUNCTION CALL OPERATOR:
 *    - constexpr value_type operator()() const
 *    - Used extensively in type traits
 * 
 * 4. COMPLEX TEMPLATE METAPROGRAMMING:
 *    - Standard headers use extensive SFINAE patterns
 *    - Multiple levels of template specialization inheritance
 *    - Partial specializations with complex patterns
 * 
 * 5. PREPROCESSOR COMPLEXITY:
 *    - Headers have extensive preprocessor conditionals
 *    - Platform-specific code paths
 *    - Macro-generated template specializations
 * 
 * 6. BUILTIN COMPILER SUPPORT:
 *    - __is_same, __is_base_of, __is_trivial, etc.
 *    - These are compiler intrinsics needed for type traits
 * 
 * 7. NAMESPACE COMPLEXITY:
 *    - Inline namespaces
 *    - Nested namespace aliases
 *    - Anonymous namespaces with template specializations
 * 
 * The compiler may hang or fail on:
 * - <vector>: complex allocator patterns, iterator traits
 * - <utility>: std::pair, std::move, std::forward - requires perfect forwarding
 * - <type_traits>: extensive template metaprogramming
 * - <memory>: smart pointers with complex RAII patterns
 * - <algorithm>: requires iterator concepts and constraints
 */
