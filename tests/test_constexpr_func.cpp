// Test constexpr functions

constexpr int add(int a, int b) {
    return a + b;
}

constexpr int square(int x) {
    return x * x;
}

constexpr int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

// Test usage in static_assert
static_assert(add(10, 20) == 30, "add(10, 20) should be 30");
static_assert(square(5) == 25, "square(5) should be 25");
static_assert(factorial(5) == 120, "factorial(5) should be 120");

// Test usage in constexpr variable initialization
constexpr int result = add(5, 5);
static_assert(result == 10, "result should be 10");
