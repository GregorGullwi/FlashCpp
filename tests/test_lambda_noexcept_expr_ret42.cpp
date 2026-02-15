// Test lambda with noexcept(expr) form - expression should be evaluated
// noexcept(true) means noexcept, noexcept(false) means potentially throwing
// Expected return: 42

int main() {
    auto f = [](int x) noexcept(true) -> int { return x + 2; };
    auto g = [](int x) noexcept(false) -> int { return x; };
    return f(g(40));
}
