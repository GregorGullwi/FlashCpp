// Test lambda with mutable + noexcept specifiers together (C++20)
// Expected return: 42

int main() {
    int val = 40;
    auto f = [val]() mutable noexcept -> int {
        val += 2;
        return val;
    };
    return f();
}
