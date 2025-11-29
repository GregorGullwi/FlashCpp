int test_prefix_increment() {
    int a = 5;
    int b = ++a;  // a = 6, b = 6
    return a + b; // Should return 12
}

int main() {
    return test_prefix_increment();
}

