// Test case for member template function calls with explicit template arguments
// Pattern: Helper<T>::Check<U>()

template<typename T>
struct Helper {
    template<typename U>
    static constexpr bool Check() { return true; }
    
    template<typename U>
    static constexpr int GetValue() { return 42; }
};

int main() {
    // Test 1: Simple member template call
    bool x = Helper<int>::Check<int>();
    
    // Test 2: Different template arguments
    bool y = Helper<float>::Check<double>();
    
    // Test 3: Member template returning int
    int z = Helper<int>::GetValue<char>();
    
    // Return 0 if all tests pass
    return (x && y && z == 42) ? 0 : 1;
}
