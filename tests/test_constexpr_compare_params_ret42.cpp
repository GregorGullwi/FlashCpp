// Test comparison with parameters
constexpr bool compare_params(int a, int b) {
    return a > b;
}

static_assert(compare_params(10, 5) == true, "compare_params failed");

int main() {
    return 42;
}
