// Test 17: Multiple lambdas
int test_multiple_lambdas() {
    int x = 2;
    int y = 3;
    auto lambda1 = [x]() { return x; };
    auto lambda2 = [y]() { return y; };
    return lambda1() + lambda2() - 0;  // Expected: 5
}

// Test 18: Lambda in conditional
int test_lambda_in_conditional() {
    int x = 5;
    auto lambda = [x]() {
        if (x > 5) return 42;
        return x;
    };
    return lambda();  // Expected: 5
}

int main() {
    return test_multiple_lambdas() + test_lambda_in_conditional();
}
