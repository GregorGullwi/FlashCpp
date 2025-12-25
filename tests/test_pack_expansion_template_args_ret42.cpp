// Test parameter pack expansion in template arguments
// This tests the pattern from <type_traits> line 175

template<bool... Vals>
struct all_true {
    static constexpr bool value = (Vals && ...);
};

template<typename T>
struct has_value {
    static constexpr bool value = true;
};

// Type aliases to work around nested template issues
using IntType = has_value<int>;
using FloatType = has_value<float>;

// Test with parameter pack - pattern similar to <type_traits>
template<typename... Bn>
struct test {
    // Using pack expansion in template argument - this is the key feature being tested
    static constexpr bool result = all_true<!bool(Bn::value)...>::value;
};

// Create a simple function that uses the template to ensure it's instantiated
int get_result() {
    // Force instantiation of the test template with different parameter packs
    // The key is that the pack expansion in the template argument (!bool(Bn::value)...) 
    // is parsed and compiled correctly
    bool r1 = test<IntType>::result;              // Pack with 1 element
    bool r2 = test<IntType, FloatType>::result;   // Pack with 2 elements  
    bool r3 = test<>::result;                      // Empty pack
    
    // All should work if parsing is correct
    return 42;
}

int main() {
    return get_result();
}
