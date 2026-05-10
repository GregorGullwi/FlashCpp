// Test: calling operator() on locally-declared struct variables in constexpr
struct Add {
    int base;
    constexpr int operator()(int x) const { return base + x; }
};

constexpr int f1() {
    Add a{10};
    return a(5);  // 15
}
static_assert(f1() == 15);

struct Mul {
    int factor;
    constexpr Mul(int f) : factor(f) {}
    constexpr int operator()(int x) const { return factor * x; }
};

constexpr int f2() {
    Mul m(3);
    return m(7);  // 21
}
static_assert(f2() == 21);

// Void operator() on aggregate with mutable state
struct Counter {
    int value;
    constexpr void operator()(int n) { value += n; }
    constexpr int get() const { return value; }
};

constexpr int f3() {
    Counter c{0};
    c(3);
    c(7);
    return c.get();  // 10
}
static_assert(f3() == 10);

int main() { return 0; }
