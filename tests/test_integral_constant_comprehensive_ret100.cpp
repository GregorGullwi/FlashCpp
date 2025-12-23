// Comprehensive test for std::integral_constant pattern
// Tests both static constexpr member access and conversion operators

template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type = T;
    using type = integral_constant;
    
    // Conversion operator
    constexpr operator value_type() const { 
        return value; 
    }
};

// Type aliases
template<bool B>
using bool_constant = integral_constant<bool, B>;

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

// Simple type trait using integral_constant
template<typename T, typename U>
struct is_same : false_type {};

template<typename T>
struct is_same<T, T> : true_type {};

int main() {
    int result = 0;
    
    // Test 1: Direct static member access
    constexpr int val = integral_constant<int, 42>::value;
    if (val == 42) result += 10;
    
    // Test 2: Accessing through type alias
    constexpr bool t = true_type::value;
    constexpr bool f = false_type::value;
    if (t && !f) result += 20;
    
    // Test 3: Using conversion operator
    integral_constant<int, 30> ic;
    int converted = ic;  // Uses operator int()
    if (converted == 30) result += 30;
    
    // Test 4: Type trait with static member
    constexpr bool same_int_int = is_same<int, int>::value;
    constexpr bool same_int_float = is_same<int, float>::value;
    if (same_int_int && !same_int_float) result += 40;
    
    return result;  // Expected: 100
}
