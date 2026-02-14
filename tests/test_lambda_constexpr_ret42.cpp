// Test lambda with constexpr specifier (C++17/C++20)
// Expected return: 42

int main() {
    auto f = [](int x) constexpr -> int { return x * 2; };
    return f(21);
}
