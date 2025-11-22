// Advanced constexpr loop tests (C++14/C++20 features)

// Test 1: Simple for loop with local variable
constexpr int test_for_loop() {
    int sum = 0;
    for (int i = 0; i < 5; ++i) {
        sum += i;
    }
    return sum;
}
static_assert(test_for_loop() == 10, "for loop should work");

// Test 2: While loop
constexpr int test_while_loop() {
    int result = 1;
    int i = 0;
    while (i < 5) {
        result *= 2;
        ++i;
    }
    return result;
}
static_assert(test_while_loop() == 32, "while loop should work");

// Test 3: Nested loops
constexpr int test_nested_loops() {
    int sum = 0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            sum += i * j;
        }
    }
    return sum;
}
static_assert(test_nested_loops() == 18, "nested loops should work");

// Test 4: Loop with if statement
constexpr int test_loop_with_if() {
    int count = 0;
    for (int i = 0; i < 10; ++i) {
        if (i % 2 == 0) {
            count += i;
        }
    }
    return count;
}
static_assert(test_loop_with_if() == 20, "loop with if should work");

// Test 5: Factorial using loop
constexpr int factorial_loop(int n) {
    int result = 1;
    for (int i = 1; i <= n; ++i) {
        result *= i;
    }
    return result;
}
static_assert(factorial_loop(5) == 120, "factorial with loop should work");

// Test 6: Fibonacci using loop
constexpr int fibonacci_loop(int n) {
    if (n <= 1) return n;
    int a = 0;
    int b = 1;
    for (int i = 2; i <= n; ++i) {
        int temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}
static_assert(fibonacci_loop(10) == 55, "fibonacci with loop should work");

// Test 7: Multiple local variables
constexpr int test_multiple_vars() {
    int x = 5;
    int y = 10;
    int z = 0;
    for (int i = 0; i < 3; ++i) {
        z += x + y;
    }
    return z;
}
static_assert(test_multiple_vars() == 45, "multiple local variables should work");

// Test 8: Compound assignment operators in loop
constexpr int test_compound_assignments() {
    int val = 10;
    for (int i = 0; i < 5; ++i) {
        val += 2;
    }
    return val;
}
static_assert(test_compound_assignments() == 20, "compound assignments should work");

// Test 9: Early return from loop
constexpr int test_early_return() {
    for (int i = 0; i < 100; ++i) {
        if (i == 7) {
            return i * 2;
        }
    }
    return -1;
}
static_assert(test_early_return() == 14, "early return should work");

// Test 10: Postfix increment
constexpr int test_postfix_increment() {
    int sum = 0;
    int i = 0;
    while (i < 5) {
        sum += i++;
    }
    return sum;
}
static_assert(test_postfix_increment() == 10, "postfix increment should work");

void test() {
    // All tests passed!
}
