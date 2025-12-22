// Example to see how Clang handles lambdas

int test_simple_lambda() {
    auto lambda = []() { return 42; };
    return lambda();
}

int test_capture_lambda() {
    int x = 10;
    auto lambda = [x](int y) { return x + y; };
    return lambda(5);
}


int main() {
    return test_simple_lambda();
}
