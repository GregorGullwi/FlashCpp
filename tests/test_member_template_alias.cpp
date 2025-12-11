// Test member template aliases - template aliases inside structs
// This is critical for standard library headers like <type_traits>

struct TypeTraits {
    // Simple member template alias
    template<typename T>
    using Ptr = T*;
    
    template<typename T>
    using Ref = T&;
    
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
    
    // Test 1: Simple pointer alias
    TypeTraits::Ptr<int> p1 = &x;
    int result1 = *p1;  // 100
    
    // Test 2: Const pointer alias
    TypeTraits::ConstPtr<int> p2 = &x;
    int result2 = *p2;  // 100
    
    // Test 3: Reference alias
    TypeTraits::Ref<int> r = x;
    r = 50;  // Modify through reference
    int result3 = x;  // Should be 50 now
    
    // Test 4: Multiple type parameter alias (just use int, int)
    Utilities::FirstType<int, int> val1 = 10;
    
    // Return sum: 100 + 100 + 50 + 10 = 260
    return result1 + result2 + result3 + val1;
}
