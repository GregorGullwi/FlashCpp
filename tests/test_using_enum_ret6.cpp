// Test C++20 using enum feature
enum class Color { Red = 1, Green = 2, Blue = 3 };

int test() {
    using enum Color;
    // Enumerators are now accessible without Color:: prefix
    return static_cast<int>(Red) + static_cast<int>(Green) + static_cast<int>(Blue);
}

int main() {
    return test();  // Should return 1+2+3 = 6
}
