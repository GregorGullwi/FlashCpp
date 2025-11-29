// Basic exception handling test - try/catch/throw
#include <cstdio>

int test_simple_throw_catch() {
    try {
        throw 42;
    } catch (int e) {
        return e;  // Should return 42
    }
    return 0;  // Should not reach here
}

int test_no_exception() {
    try {
        return 100;
    } catch (int e) {
        return e;
    }
    return 0;  // Should not reach here
}

int main() {
    int result1 = test_simple_throw_catch();
    printf("test_simple_throw_catch: %d (expected 42)\n", result1);
    
    int result2 = test_no_exception();
    printf("test_no_exception: %d (expected 100)\n", result2);
    
    // Test catching different types
    try {
        throw 123;
    } catch (int x) {
        printf("Caught int: %d (expected 123)\n", x);
    }
    
    return 0;
}
