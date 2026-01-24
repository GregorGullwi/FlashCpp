// Test: Member variable template parsing
// This tests parsing of static constexpr variable templates inside classes
// Pattern: template<typename T> static constexpr bool var = ...;
// The fix was about PARSING these declarations, codegen may not be complete

struct type_traits {
    // Test 1: Simple member variable template
    template<typename T>
    static constexpr bool is_integral_v = false;
    
    // Test 2: Variable template with value computation
    template<typename T>
    static constexpr int size_of_v = sizeof(T);
    
    // Test 3: Variable template with non-type parameter
    template<int N>
    static constexpr int times_two = N * 2;
};

// Test 4: Member variable template in template class
template<typename U>
struct container {
    template<typename T>
    static constexpr int combined_size = sizeof(T) + sizeof(U);
};

int main() {
    // Test actual usage of member variable templates
    // This tests both parsing AND codegen/instantiation
    // Note: is_integral_v is intentionally hardcoded to false to test the template mechanism
    constexpr bool b = type_traits::is_integral_v<int>;  // Returns false (not a complete type trait impl)
    constexpr int s = type_traits::size_of_v<int>;       // Returns 4
    constexpr int t = type_traits::times_two<19>;        // Returns 38
    
    // Should return: 0 + 4 + 38 = 42
    // The test is about template instantiation, not accurate type traits
    return (b ? 1 : 0) + s + t;
}