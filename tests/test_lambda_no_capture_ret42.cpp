// Test lambdas without captures

int test_lambda_simple() {
    auto lambda = []() { return 42; };
    return lambda();
}

int test_lambda_with_params() {
    auto lambda = [](int x, int y) { return x + y; };
    return lambda(10, 20);
}

int test_lambda_return_type() {
    auto lambda = []() -> int { return 100; };
    return lambda();
}


int main() {
    return test_lambda_simple();
}
