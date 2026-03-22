// Test: constexpr allocation that escapes (is not freed) must be rejected.
// C++20 [expr.const]/p5: all `new` allocations within a constant expression
// must be freed before the constant expression ends.

constexpr int leak() {
    int* p = new int(42);
    return *p;  // forgot delete p — ill-formed per C++20
}
static_assert(leak() == 42);

int main() { return 0; }
