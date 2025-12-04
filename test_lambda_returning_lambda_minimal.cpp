// Minimal test for lambda returning lambda issue

int test_lambda_returning_lambda() {
    auto maker = [](int offset) {
        return [offset](int base) { return base + offset; };
    };
    auto add2 = maker(2);
    return add2(3);  // Should return 5
}

int main() {
    return test_lambda_returning_lambda();
}
