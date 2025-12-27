// Simple test: Template parameter used as base class
// This tests the basic C++20 pattern where a template parameter is used as a base class

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
};

// Type aliases
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Test 1: Simple template with parameter as base class
template<typename T>
struct wrapper : T {
};

// Test 2: Template specialization with parameter as base
template<typename... Ts>
struct my_or;

// Specialization: no arguments = false
template<>
struct my_or<> : false_type {};

// Specialization: template parameter as base class
// This is the key test - T is a template parameter used as a base class
template<typename T>
struct my_or<T> : T {};

int main() {
    // Test 1: wrapper inherits from true_type - use static access
    constexpr bool test1 = wrapper<true_type>::value;  // Should be true (1)
    
    // Test 2: my_or<> should be false
    constexpr bool test2 = my_or<>::value;  // Should be false (0)
    
    // Test 3: my_or<true_type> should be true  
    constexpr bool test3 = my_or<true_type>::value;  // Should be true (1)
    
    // Test 4: my_or<false_type> should be false
    constexpr bool test4 = my_or<false_type>::value;  // Should be false (0)
    
    // Return 42 if all tests pass
    if (test1 && !test2 && test3 && !test4) {
        return 42;
    }
    
    return 0;  // Failed
}
