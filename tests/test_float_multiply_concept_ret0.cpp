// Test floating-point arithmetic with concepts
// Regression test for bug where float multiply results weren't stored to memory
// As of January 8, 2026, this bug has been fixed

template<typename T>
concept FloatingPoint = __is_floating_point(T);

template<typename T>
requires FloatingPoint<T>
T multiply(T a, T b) {
    return a * b;
}

int main() {
    // Test float multiplication
    float result = multiply(21.0f, 2.0f);
    int int_result = (int)result;
    
    // Expected: 42
    if (int_result != 42) {
        return 1;
    }
    
    return 0;
}
