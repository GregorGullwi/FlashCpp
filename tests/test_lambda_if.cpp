int test_lambda_if() {
    int x = 5;
    auto lambda = [x]() {
        if (x > 5) return 42;
        return x;
    };
    return lambda();
}

int main() {
    return test_lambda_if();
}
