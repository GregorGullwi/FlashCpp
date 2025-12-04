// Demonstration of lambda features now supported by FlashCpp
// This test demonstrates the fix for init-captures and mutable lambdas

// Test 1: Basic init-capture with expression
int test1() {
    int base = 3;
    auto lambda = [x = base + 2]() { return x; };
    return lambda();  // Returns 5
}

// Test 2: Init-capture with literal
int test2() {
    auto lambda = [value = 42]() { return value; };
    return lambda();  // Returns 42
}

// Test 3: Init-capture by reference
int test3() {
    int counter = 0;
    auto increment = [&ref = counter]() { ref += 10; };
    increment();
    return counter;  // Returns 10
}

// Test 4: Mutable lambda
int test4() {
    int x = 5;
    auto lambda = [x]() mutable {
        x += 3;
        return x;
    };
    return lambda();  // Returns 8
}

// Test 5: Combined - init-capture with mutable
int test5() {
    auto lambda = [count = 0]() mutable {
        return ++count;
    };
    lambda();  // count becomes 1
    lambda();  // count becomes 2
    return lambda();  // Returns 3
}

int main() {
    int result = 0;
    result += test1();  // 5
    result += test2();  // 42
    result += test3();  // 10
    result += test4();  // 8
    result += test5();  // 3
    return result;      // Total: 68
}
