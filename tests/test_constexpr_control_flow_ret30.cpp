// Test constexpr for loops (C++14 feature)
// Comprehensive test of control flow in constexpr functions

// Test 1: Simple for loop with sum
constexpr int sum_loop(int n) {
    int sum = 0;
    for (int i = 1; i <= n; ++i) {
        sum += i;
    }
    return sum;
}

static_assert(sum_loop(5) == 15, "sum 1-5 should be 15");
static_assert(sum_loop(10) == 55, "sum 1-10 should be 55");

// Test 2: For loop with product
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial(5) == 120, "5! should be 120");

// Test 3: For loop with if statement
constexpr int sum_even(int n) {
    int sum = 0;
    for (int i = 1; i <= n; ++i) {
        if (i % 2 == 0) {
            sum += i;
        }
    }
    return sum;
}

static_assert(sum_even(10) == 30, "sum of even numbers 1-10 should be 30");

// Test 4: While loop
constexpr int sum_while(int n) {
    int sum = 0;
    int i = 1;
    while (i <= n) {
        sum += i;
        ++i;
    }
    return sum;
}

static_assert(sum_while(5) == 15, "while sum 1-5 should be 15");

int main() {
    return sum_loop(5) + sum_while(5);  // Should return 30
}
