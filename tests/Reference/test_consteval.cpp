// Test consteval - C++20 immediate functions

// consteval functions MUST be evaluated at compile time
consteval int square(int x) {
    return x * x;
}

consteval int cube(int x) {
    return x * x * x;
}

// Use in static_assert
static_assert(square(5) == 25, "square(5) should be 25");
static_assert(cube(3) == 27, "cube(3) should be 27");

// Use in constexpr variable initialization
constexpr int result1 = square(10);
static_assert(result1 == 100, "result1 should be 100");

constexpr int result2 = cube(4);
static_assert(result2 == 64, "result2 should be 64");

// consteval can call constexpr
constexpr int add(int a, int b) {
    return a + b;
}

consteval int add_squares(int a, int b) {
    return add(square(a), square(b));
}

static_assert(add_squares(3, 4) == 25, "3^2 + 4^2 should be 25");

// consteval with loops (C++20)
consteval int factorial(int n) {
    int result = 1;
    for (int i = 1; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial(5) == 120, "factorial(5) should be 120");

// consteval with if statements
consteval int abs(int x) {
    if (x < 0) {
        return -x;
    } else {
        return x;
    }
}

static_assert(abs(-5) == 5, "abs(-5) should be 5");
static_assert(abs(7) == 7, "abs(7) should be 7");

// consteval can be recursive
consteval int fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

static_assert(fibonacci(10) == 55, "fibonacci(10) should be 55");

void test() {
    // All consteval tests passed!
}
