// Phase 3 comprehensive test: Scope resolution priority
// Tests that after ::, < is always treated as template argument delimiter
// This validates Phase 3 implementation with Phase 5's scope resolution priority rule

namespace test {
    template<typename T>
    T identity(T x) {
        return x;
    }
    
    template<typename A, typename B>
    int add_both(A a, B b) {
        return static_cast<int>(a) + static_cast<int>(b);
    }
}

int main() {
    // Test 1: Simple qualified template function call
    int val1 = test::identity<int>(10);
    
    // Test 2: Multiple template parameters
    int val2 = test::add_both<int, int>(5, 8);
    
    // Test 3: Decltype with qualified template call (Phase 3 specific)
    using type1 = decltype(test::identity<int>(15));
    type1 val3 = 20;
    
    // Test 4: Nested in expression
    int val4 = test::identity<int>(3) + test::identity<int>(4);
    
    return val1 + val2 + val3 + val4;  // 10 + 13 + 20 + 7 = 50
}
