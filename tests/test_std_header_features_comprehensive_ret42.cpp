// Comprehensive test for FlashCpp standard library compatibility features
// This test validates multiple features that were marked as completed in 
// tests/std/STANDARD_HEADERS_MISSING_FEATURES.md (December 2025)
//
// NOTE: This test avoids a known interaction bug where using conversion operators
// AND structured bindings together corrupts subsequent template specialization lookups.
//
// Returns 42 to indicate all tests passed

// ===== 1. Structured Bindings (C++17) =====
struct Point {
    int x;
    int y;
};

// ===== 2. Custom integral_constant pattern =====
template<typename T, T v>
struct integral_constant {
    static constexpr T value = v;
    using value_type = T;
    using type = integral_constant;
    constexpr operator value_type() const noexcept { return value; }
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// ===== 3. is_same type trait pattern =====
template<typename T, typename U>
struct is_same : false_type {};

template<typename T>
struct is_same<T, T> : true_type {};

// ===== 4. Constexpr functions =====
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

// ===== 5. Fold expressions =====
template<typename... Args>
constexpr int sum(Args... args) {
    return (args + ... + 0);
}

// Main function that validates runtime tests
int main() {
    // Test 1: Type traits intrinsics at runtime
    if (!__is_same(int, int)) {
        return 1;  // Error
    }
    if (__is_same(int, float)) {
        return 2;  // Error
    }
    if (!__is_class(Point)) {
        return 3;  // Error
    }
    if (__is_class(int)) {
        return 4;  // Error
    }
    
    // Test 2: Custom is_same type trait
    if (!is_same<int, int>::value) {
        return 5;  // Error
    }
    if (is_same<int, float>::value) {
        return 6;  // Error
    }
    
    // Test 3: Constexpr function at runtime
    if (factorial(5) != 120) {
        return 7;  // Error
    }
    
    // Test 4: Fold expression at runtime
    if (sum(1, 2, 3, 4, 5) != 15) {
        return 8;  // Error
    }
    
    int result = 0;
    
    // Test 5: Structured bindings (separate from conversion operator to avoid interaction bug)
    Point p = {30, 12};
    auto [x, y] = p;
    result += x + y;  // +42
    
    // If all tests pass, result should be 42
    return result;
}

