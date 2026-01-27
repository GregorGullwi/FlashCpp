// Test nested member access in constexpr evaluation
// Tests obj.inner.value style access

struct Inner {
    int value;
    constexpr Inner(int v) : value(v) {}
};

struct Outer {
    Inner inner;
    constexpr Outer(int v) : inner(v) {}
};

// Test basic nested member access
constexpr Outer obj(42);
static_assert(obj.inner.value == 42, "Nested member access should work");

// Test nested member access with different values
constexpr Outer obj2(100);
static_assert(obj2.inner.value == 100, "Nested member access with different value");

// Test nested member access with expressions
constexpr Outer obj3(5 * 10);
static_assert(obj3.inner.value == 50, "Nested member access with expression argument");

// Note: Multi-argument inner constructor member initializers are limited by parser
// The parser only stores the first argument for member initializers like inner(a, b)
// Use a workaround with computed values or separate structs

int main() {
    return 0;
}
