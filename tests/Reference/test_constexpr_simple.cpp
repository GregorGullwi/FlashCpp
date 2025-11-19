// Simple constexpr test

constexpr int a = 10;
static_assert(a == 10, "a should be 10");

constexpr int b = 15;
static_assert(b == 15, "b should be 15");

// Test arithmetic
constexpr int c = 10 + 5;
static_assert(c == 15, "c should be 15");

constexpr int d = 20 - 5;
static_assert(d == 15, "d should be 15");

constexpr int e = 5 * 3;
static_assert(e == 15, "e should be 15");

// Test comparison
constexpr bool f = 10 > 5;
static_assert(f, "f should be true");

constexpr bool g = 10 == 10;
static_assert(g, "g should be true");
