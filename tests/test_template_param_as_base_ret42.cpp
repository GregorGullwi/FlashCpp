// Test: Template parameter used as base class
// This tests the advanced C++20 pattern where a template parameter is used as a base class

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

// Type aliases
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Template with variadic parameters
template<typename... Ts>
struct my_or;

// Specialization: no arguments = false
template<>
struct my_or<> : false_type {};

// Specialization: template parameter as base class
// This is the key test - T is a template parameter used as a base class
template<typename T>
struct my_or<T> : T {};

// Specialization: multiple arguments (recursive pattern)
template<typename T, typename... Rest>
struct my_or<T, Rest...> : integral_constant<bool, T::value || my_or<Rest...>::value> {};

int main() {
    // Test 1: Empty my_or should be false
    constexpr bool test1 = my_or<>::value;  // Should be false (0)
    
    // Test 2: Single false_type should be false
    constexpr bool test2 = my_or<false_type>::value;  // Should be false (0)
    
    // Test 3: Single true_type should be true
    constexpr bool test3 = my_or<true_type>::value;  // Should be true (1)
    
    // Test 4: Multiple with at least one true
    constexpr bool test4 = my_or<false_type, true_type, false_type>::value;  // Should be true (1)
    
    // Test 5: All false
    constexpr bool test5 = my_or<false_type, false_type>::value;  // Should be false (0)
    
    // Return 42 if all tests pass
    if (!test1 && !test2 && test3 && test4 && !test5) {
        return 42;
    }
    
    return 0;  // Failed
}
