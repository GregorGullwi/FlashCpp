// Test global namespace scope resolution with :: operator
// This tests both:
// 1. using ::name; declarations (importing from global namespace)
// 2. ::Func() calls (calling functions from global namespace)

// Global namespace functions and variables
int globalFunc() {
    return 42;
}

int globalValue = 100;

// Namespace with same-named functions to test disambiguation
namespace MyNamespace {
    int globalFunc() {
        return 99;  // Different from global
    }
    
    int globalValue = 200;  // Different from global
    
    // Test 1: Using declaration from global namespace
    int test_using_global_func() {
        using ::globalFunc;  // Import global namespace function
        return globalFunc();  // Should call ::globalFunc(), returning 42
    }
    
    // Test 2: Explicit global namespace call
    int test_explicit_global_call() {
        return ::globalFunc();  // Should call global namespace function, returning 42
    }
    
    // Test 3: Local namespace call (no ::)
    int test_local_call() {
        return globalFunc();  // Should call MyNamespace::globalFunc(), returning 99
    }
    
    // Test 4: Using declaration for global variable
    int test_using_global_var() {
        using ::globalValue;  // Import global namespace variable
        return globalValue;  // Should access ::globalValue, returning 100
    }
    
    // Test 5: Explicit global variable access
    int test_explicit_global_var() {
        return ::globalValue;  // Should access global namespace variable, returning 100
    }
    
    // Test 6: Local variable access (no ::)
    int test_local_var() {
        return globalValue;  // Should access MyNamespace::globalValue, returning 200
    }
}

// Test 7: Using global namespace in nested context
namespace Outer {
    int value = 50;
    
    namespace Inner {
        int value = 75;
        
        int test_nested_global_access() {
            return ::globalValue;  // Should access global namespace, returning 100
        }
        
        int test_nested_using_global() {
            using ::globalValue;
            return globalValue;  // Should return 100
        }
    }
}

// Test 8: Multiple using declarations from global namespace
namespace TestMultiple {
    int test_multiple_using() {
        using ::globalFunc;
        using ::globalValue;
        return globalFunc() + globalValue;  // Should return 42 + 100 = 142
    }
}

// Test 9: Global namespace in std-like pattern (common in real code)
namespace std {
    // In real C++, std often imports types from global namespace
    // This mimics patterns like: namespace std { using ::size_t; }
    using ::globalValue;
    
    int test_std_pattern() {
        return globalValue;  // Should access ::globalValue through using declaration
    }
}

int main() {
    // Test calling from global scope
    int result1 = MyNamespace::test_using_global_func();  // Should be 42
    int result2 = MyNamespace::test_explicit_global_call();  // Should be 42
    int result3 = MyNamespace::test_local_call();  // Should be 99
    int result4 = MyNamespace::test_using_global_var();  // Should be 100
    int result5 = MyNamespace::test_explicit_global_var();  // Should be 100
    int result6 = MyNamespace::test_local_var();  // Should be 200
    int result7 = Outer::Inner::test_nested_global_access();  // Should be 100
    int result8 = Outer::Inner::test_nested_using_global();  // Should be 100
    int result9 = TestMultiple::test_multiple_using();  // Should be 142
    int result10 = std::test_std_pattern();  // Should be 100
    
    // Sum all results: 42 + 42 + 99 + 100 + 100 + 200 + 100 + 100 + 142 + 100 = 1025
    return result1 + result2 + result3 + result4 + result5 + 
           result6 + result7 + result8 + result9 + result10;
}

