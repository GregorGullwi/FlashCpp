// Simple test for variadic argument implementation
// This will test our variadic function with parameters before ...

// Test that we can parse variadic functions with named parameters before ...
int test_func(int a, int b, ...) {
    return a + b;
}

int main() {
    int result = test_func(10, 20, 30, 40);
    return result;  // Should return 30
}

