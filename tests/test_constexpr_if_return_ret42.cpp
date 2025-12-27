// Test constexpr if-else with explicit return in each branch

constexpr int max_explicit(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;  // Explicit return after if, not in else
}

static_assert(max_explicit(10, 5) == 10, "max_explicit failed");

constexpr int max_with_else(int a, int b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

static_assert(max_with_else(10, 5) == 10, "max_with_else failed");

int main() {
    return 42;
}
