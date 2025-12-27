// Test if with comparison in condition
constexpr int test_comparison(int a) {
    if (a > 5) {
        return 1;
    }
    return 0;
}

static_assert(test_comparison(10) == 1, "test_comparison(10) failed");
static_assert(test_comparison(3) == 0, "test_comparison(3) failed");

int main() {
    return 42;
}
