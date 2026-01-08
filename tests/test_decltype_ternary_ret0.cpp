// Test decltype with ternary operator - this works!
// As of January 8, 2026, decltype can evaluate ternary operators

template<typename T>
T declval();

// Common type pattern using decltype with ternary operator
template<typename Tp, typename Up>
struct common_type {
    using type = decltype(true ? declval<Tp>() : declval<Up>());
};

// Helper to test the common type
template<typename T1, typename T2>
using common_t = typename common_type<T1, T2>::type;

int main() {
    // Test common_type deduction
    // common_type<int, double> should be double
    common_t<int, double> x = 42.0;
    
    // common_type<int, int> should be int
    common_t<int, int> y = 42;
    
    return 0;
}
