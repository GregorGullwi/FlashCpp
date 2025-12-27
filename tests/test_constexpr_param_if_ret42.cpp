// Test parameter binding in if condition
constexpr int test_param_binding(int x) {
    if (x) {
        return 1;
    }
    return 0;
}

static_assert(test_param_binding(1) == 1, "test_param_binding(1) failed");
static_assert(test_param_binding(0) == 0, "test_param_binding(0) failed");

int main() {
    return 42;
}
