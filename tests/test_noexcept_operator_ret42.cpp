// Test noexcept as an operator (compile-time expression)
// noexcept(expr) returns true if expr is noexcept, false otherwise

struct Test {
    void foo() {}
};

int main() {
    int x = 5;
    // noexcept(expr) returns a compile-time boolean
    constexpr bool b = noexcept(x + 1);  // Simple arithmetic doesn't throw
    return b ? 42 : 0;  // Should return 42
}
