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
    // Test passes if the declarations compile
    // The fix was about parsing these member variable templates
    // Usage/codegen may not be fully implemented yet
    return 42;
}