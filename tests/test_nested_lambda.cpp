int test_nested_lambda_calls() {
    int x = 5;
    auto outer = [x]() {
        auto inner = [x]() { return x; };
        return inner();
    };
    return outer();  // Expected: 5
}

int main() {
    return test_nested_lambda_calls();
}
