int test_prefix_increment() {
    int a = 5;
    int b = ++a;  // a = 6, b = 6
    return a + b; // Should return 12
}

int test_postfix_increment() {
    int a = 5;
    int b = a++;  // a = 6, b = 5
    return a + b; // Should return 11
}

int test_prefix_decrement() {
    int a = 5;
    int b = --a;  // a = 4, b = 4
    return a + b; // Should return 8
}

int test_postfix_decrement() {
    int a = 5;
    int b = a--;  // a = 4, b = 5
    return a + b; // Should return 9
}

int main() {
    int result = test_prefix_increment() + test_postfix_increment() + test_prefix_decrement() + test_postfix_decrement();
    // 12 + 11 + 8 + 9 = 40
    return result;
}

