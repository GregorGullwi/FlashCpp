// Test: C++20 Template Argument Disambiguation - Pack Expansion
// Tests that template arguments with pack expansion work correctly

namespace ns {
    template<typename... Args>
    int sum() {
        return 0;
    }
    
    template<>
    int sum<int>() {
        return 10;
    }
    
    template<>
    int sum<int, int>() {
        return 20;
    }
}

// Test: function call with pack expansion in template arguments
template<typename... Args>
int callSum() {
    return ns::sum<Args...>();
}

int main() {
    // Test with explicit template arguments
    int result1 = ns::sum<int>();       // Should return 10
    int result2 = ns::sum<int, int>();  // Should return 20
    
    // Test with pack expansion
    int result3 = callSum<int>();       // Should return 10
    
    return result1 + result2 + result3;  // 10 + 20 + 10 = 40
}
