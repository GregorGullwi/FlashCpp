// Test accessing type aliases from template specializations
// This is a critical feature for <type_traits> and other standard library headers

// Test 1: Basic type alias from full specialization
template<bool B>
struct enable_if {
    using type = void;
};

template<>
struct enable_if<true> {
    using type = int;
};

// Test 2: Type alias with different types in primary and specialization
template<typename T>
struct type_identity {
    using type = T;
};

template<>
struct type_identity<bool> {
    using type = int;  // Map bool to int
};

// Test 3: Multiple full specializations with different type aliases
template<int N>
struct int_to_type {
    using type = int;
};

template<>
struct int_to_type<0> {
    using type = char;
};

template<>
struct int_to_type<1> {
    using type = short;
};

// Test 4: Nested type aliases
template<typename T>
struct wrapper {
    using value_type = T;
};

template<>
struct wrapper<bool> {
    using value_type = int;
};

// Expected return: 0
int main() {
    // Test 1: Access type alias from specialization
    enable_if<true>::type x1 = 10;        // Should be int
    enable_if<false>::type* x2 = nullptr; // Should be void*
    
    // Test 2: Type identity mapping
    type_identity<bool>::type x3 = 15;  // Should be int (mapped from bool)
    type_identity<int>::type x4 = 20;  // Should be int (unchanged)
    
    // Test 3: Multiple full specializations
    int_to_type<0>::type x5 = 'A';   // Should be char (65)
    int_to_type<1>::type x6 = 25;   // Should be short
    int_to_type<2>::type x7 = 30;   // Should be int
    
    // Test 4: Type wrapper
    wrapper<bool>::value_type x8 = 35;  // Should be int (mapped from bool)
    wrapper<int>::value_type x9 = 40;   // Should be int (unchanged)
    
    // Verify correct types by doing operations
    int result = 0;
    result += x1;  // 10
    result += x3;  // 15
    result += x4;  // 20
    result += x5;  // 65 ('A')
    result += x6;  // 25
    result += x7;  // 30
    result += x8;  // 35
    result += x9;  // 40
    
    // Expected: 10 + 15 + 20 + 65 + 25 + 30 + 35 + 40 = 240
    return (result == 240) ? 0 : 1;
}
