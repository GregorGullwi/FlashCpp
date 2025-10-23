// Test simple lambda captures

int test_capture_by_value() {
    int x = 42;
    auto lambda = [x]() { return x; };
    return lambda();
}

int test_capture_by_value_multiple() {
    int x = 10;
    int y = 32;
    auto lambda = [x, y]() { return x + y; };
    return lambda();
}

int main() {
    int result = 0;
    result += test_capture_by_value();
    result += test_capture_by_value_multiple();
    return result;
}

