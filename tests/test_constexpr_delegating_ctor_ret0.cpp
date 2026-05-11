// Test that constexpr evaluation correctly handles delegating constructors
// (C++11/C++20: a constructor that delegates to another constructor in its
// member-initializer list: S() : S(42) {}).
//
// Previously the delegating initializer was completely ignored, so the
// default constructor always left members zero-initialized instead of
// delegating to the target constructor.

struct S {
    int x;
    constexpr S(int v) : x(v) {}
    constexpr S() : S(42) {}
};

constexpr S s{};
static_assert(s.x == 42);

constexpr S s2{7};
static_assert(s2.x == 7);

// Two-level delegation chain: T() -> T(3) -> T(3, 6)
struct T {
    int a;
    int b;
    constexpr T(int a_, int b_) : a(a_), b(b_) {}
    constexpr T(int v) : T(v, v * 2) {}
    constexpr T() : T(3) {}
};

constexpr T t{};
static_assert(t.a == 3);
static_assert(t.b == 6);

constexpr T t2{5};
static_assert(t2.a == 5);
static_assert(t2.b == 10);

int main() { return 0; }
