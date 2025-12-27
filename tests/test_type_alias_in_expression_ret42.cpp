// Test: Type aliases should be usable in expression contexts
// This tests the fix for type alias resolution issue from STANDARD_HEADERS_MISSING_FEATURES.md

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type = T;
    using type = integral_constant;
    constexpr operator value_type() const { return value; }
};

// Create type aliases (like std::true_type and std::false_type)
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Test 1: Type alias as base class (this already works)
template<bool B>
struct test_base : integral_constant<bool, B> {};

// Test 2: Type alias in expression context (this is what fails)
template<typename T>
struct is_void {
    // Using type alias in expression - should resolve to integral_constant<bool, false>
    // The ::value access should work
    static constexpr bool value = false_type::value;  // This is the critical test case
};

int main() {
    // Test the type alias resolution
    constexpr bool result = is_void<int>::value;  // Should be false (0)
    
    // Return 42 if it works (result should be false/0, so we add 42)
    return result + 42;
}
