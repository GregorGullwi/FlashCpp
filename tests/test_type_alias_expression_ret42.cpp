// Test type alias usage in expression contexts
// This tests the fix for type alias resolution in expressions

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type = T;
    using type = integral_constant;
};

// Type aliases
using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// Template that uses type aliases in expression context
template<typename T>
struct is_const : false_type { };

template<typename T>
struct is_const<const T> : true_type { };

int main() {
    // Use type aliases in expressions
    bool b1 = true_type::value;   // Should work
    bool b2 = false_type::value;  // Should work
    
    // Test with is_const
    bool b3 = is_const<int>::value;        // Should be false
    bool b4 = is_const<const int>::value;  // Should be true
    
    // Calculate result
    int result = (b1 ? 10 : 0) + (b2 ? 20 : 0) + (b3 ? 1 : 0) + (b4 ? 32 : 0);
    return result;  // Should return 42 (10 + 0 + 0 + 32)
}
