// Test anonymous template parameters (both type and non-type)
// This feature is required by C++20 standard library headers like <type_traits>

// Test 1: Anonymous non-type parameter (bool)
template<bool, typename T = void>
struct enable_if_anon {
    using type = T;
};

// Test 2: Anonymous type parameter
template<typename _Tp, typename>
struct pair_first {
    _Tp value;
};

// Test 3: Multiple anonymous parameters
template<bool, bool, typename T>
struct multi_anon {
    T data;
};

// Test 4: Mix of named and anonymous
template<typename T, bool, typename>
struct mixed {
    T x;
};

int main() {
    // Test anonymous non-type parameter
    enable_if_anon<true, int> e1;
    
    // Test anonymous type parameter
    pair_first<int, double> p1;
    p1.value = 42;
    
    // Test multiple anonymous parameters
    multi_anon<true, false, int> m1;
    m1.data = 10;
    
    // Test mixed parameters
    mixed<int, true, double> x1;
    x1.x = 5;
    
    return p1.value + m1.data + x1.x;  // Should return 57
}
