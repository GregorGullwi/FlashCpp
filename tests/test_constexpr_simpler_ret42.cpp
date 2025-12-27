// Test simpler constexpr features

// Test 1: Basic constexpr function
constexpr int add(int a, int b) {
    return a + b;
}

static_assert(add(3, 4) == 7, "add failed");

// Test 2: Constexpr with ternary operator (instead of if-else)
constexpr int max_ternary(int a, int b) {
    return (a > b) ? a : b;
}

static_assert(max_ternary(10, 5) == 10, "max_ternary failed");

// Test 3: Constexpr constructor
struct Point {
    int x, y;
    constexpr Point(int x_, int y_) : x(x_), y(y_) {}
    constexpr int sum() const { return x + y; }
};

constexpr Point p(3, 4);
static_assert(p.sum() == 7, "Point failed");

// Test 4: Constexpr loop
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial(5) == 120, "factorial failed");

int main() {
    return 42;
}
