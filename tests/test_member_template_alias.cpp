// Test member template aliases - template aliases inside structs
// This is critical for standard library headers like <type_traits>

struct TypeTraits {
    // Simple member template alias
    template<typename T>
    using Ptr = T*;
    
    // Note: Reference aliases have a known runtime issue (pre-existing bug in the compiler)
    // template<typename T>
    // using Ref = T&;
    
    template<typename T>
    using ConstPtr = const T*;
};

// Test with non-template struct
struct Utilities {
    template<typename T, typename U>
    using FirstType = T;
    
    template<typename T, typename U>
    using SecondType = U;
};

int main() {
    int x = 100;
    int y = 25;
    
    // Test 1: Simple pointer alias
    TypeTraits::Ptr<int> p1 = &x;
    int result1 = *p1;  // 100
    
    // Test 2: Const pointer alias
    TypeTraits::ConstPtr<int> p2 = &x;
    int result2 = *p2;  // 100
    
    // Test 3: Pointer to different value
    TypeTraits::Ptr<int> p3 = &y;
    int result3 = *p3;  // 25
    
    // Test 4: Multiple type parameter alias (first type)
    Utilities::FirstType<int, float> val1 = 10;
    
    // Test 5: Multiple type parameter alias (second type)
    Utilities::SecondType<double, int> val2 = 15;
    
    // Return sum: 100 + 100 + 25 + 10 + 15 = 250
    return result1 + result2 + result3 + val1 + val2;
}
