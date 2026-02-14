// Test lambda with noexcept specifier (C++20)
// Expected return: 42

int main() {
    auto f = [](int x) noexcept -> int { return x; };
    return f(42);
}
