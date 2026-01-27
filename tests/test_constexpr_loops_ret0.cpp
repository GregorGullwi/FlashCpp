// Test loops in constexpr (if supported) or recursion

constexpr int sum_recursive(int n) {
    return n <= 0 ? 0 : n + sum_recursive(n - 1);
}

static_assert(sum_recursive(10) == 55, "sum_recursive(10) should be 55");

// Test loop support (C++14 feature)
constexpr int sum_loop(int n) {
    int sum = 0;
    for (int i = 1; i <= n; ++i) {
        sum += i;
    }
    return sum;
}

// Now supported! Loops work in constexpr functions
static_assert(sum_loop(10) == 55, "sum_loop(10) should be 55");

int main() {
    return 0;
}
