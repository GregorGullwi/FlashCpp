// Test constexpr variables

constexpr int a = 10;
static_assert(a == 10, "a should be 10");

constexpr int b = a + 5;
static_assert(b == 15, "b should be 15");

constexpr bool c = (a > 5) && (b < 20);
static_assert(c, "c should be true");

// Test type conversions
constexpr double d = 3.14;
constexpr int e = (int)d;
static_assert(e == 3, "e should be 3");

// Test arithmetic
constexpr int f = 10 * 2 + 5;
static_assert(f == 25, "f should be 25");

// Test overflow detection (should fail compilation if uncommented)
// constexpr int max_int = 2147483647;
// constexpr int overflow = max_int + 1; 

void test() {
    // Local constexpr variables
    constexpr int local_a = 100;
    static_assert(local_a == 100, "local_a should be 100");
}

int main() {
    // If all static_asserts pass, return sum of values
    return a + b + e + f; // 10 + 15 + 3 + 25 = 53
}
