// Test comparison in return statement (not in if)
constexpr bool test_return_comparison(int a, int b) {
    return a > b;
}

static_assert(test_return_comparison(10, 5) == true, "test_return_comparison failed");

int main() {
    return 42;
}
