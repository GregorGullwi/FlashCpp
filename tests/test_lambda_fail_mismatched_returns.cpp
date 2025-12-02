// Test that should FAIL to compile
// Different return types on different paths should produce an error

int test_mismatched_return_types() {
    int x = 5;
    auto lambda = [x]() {
        if (x > 3) {
            return 42;        // int
        } else {
            return 3.14;      // double - ERROR: type mismatch
        }
    };
    return lambda();
}

int main() {
    return test_mismatched_return_types();
}
