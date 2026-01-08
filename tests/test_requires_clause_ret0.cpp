// Test C++20 requires clauses - they work correctly!
// As of January 8, 2026, requires clauses are fully supported

template<typename T>
concept Integral = __is_integral(T);

template<typename T>
concept FloatingPoint = __is_floating_point(T);

// Function template with requires clause
template<typename T>
requires Integral<T>
T add(T a, T b) {
    return a + b;
}

// Function template with multiple concept requirements
template<typename T>
requires Integral<T> || FloatingPoint<T>
T multiply(T a, T b) {
    return a * b;
}

int main() {
    // Test with integral types
    int result1 = add(20, 22);
    if (result1 != 42) return 1;
    
    // Test with multiply
    int result2 = multiply(6, 7);
    if (result2 != 42) return 2;
    
    // Test with floating point
    float result3 = multiply(21.0f, 2.0f);
    if (result3 < 41.9f || result3 > 42.1f) return 3;
    
    return 0;
}
