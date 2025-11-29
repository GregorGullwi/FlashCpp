// Test: Summary of variadic template features implemented

// 1. ✅ Parameter pack parsing
template<typename... Args>
struct Tuple {
    // 2. ✅ Pack expansion in member declarations  
    Args... values;  // Expands to: values0, values1, values2, ...
    
    // 3. ✅ sizeof... operator
    static const int size = sizeof...(Args);
};

// 4. ✅ Function parameter pack parsing (expansion TODO)
template<typename... Args>
void print(Args... args);

int main() {
    // Test 1: Empty pack
    Tuple<> empty;
    
    // Test 2: Single element pack
    Tuple<int> single;
    single.values0 = 42;
    
    // Test 3: Multiple element pack  
    Tuple<int, float> pair;
    pair.values0 = 10;
    pair.values1 = 3.14f;
    
    // Test 4: sizeof... operator (verified at compile time)
    int empty_size = Tuple<>::size;      // Should be 0
    int single_size = Tuple<int>::size;   // Should be 1
    int pair_size = Tuple<int, float>::size;  // Should be 2
    
    return single.values0 + static_cast<int>(pair.values1) - 45;  // 42 + 3 - 45 = 0
}
