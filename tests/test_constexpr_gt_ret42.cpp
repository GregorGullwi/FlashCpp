// Test if comparison itself works
constexpr bool test_gt() {
    return 10 > 5;
}

static_assert(test_gt() == true, "test_gt failed");

int main() {
    return 42;
}
