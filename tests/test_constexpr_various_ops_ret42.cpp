// Test different comparison operators in if
constexpr int test_eq(int a) {
    if (a == 10) {
        return 1;
    }
    return 0;
}

constexpr int test_lt(int a) {
    if (a < 5) {
        return 1;
    }
    return 0;
}

static_assert(test_eq(10) == 1, "test_eq(10) failed");
static_assert(test_eq(5) == 0, "test_eq(5) failed");
static_assert(test_lt(3) == 1, "test_lt(3) failed");
static_assert(test_lt(10) == 0, "test_lt(10) failed");

int main() {
    return 42;
}
