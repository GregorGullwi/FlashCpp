// Test to verify constexpr variable reference evaluation

constexpr int x = 42;

// This should work if variable references are supported
constexpr int y = x + 10;

static_assert(y == 52, "y should be 52");

void test() {
    // Test passed if this compiles
}
