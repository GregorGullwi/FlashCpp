// Test: inline spaceship operator result used in expressions
// C++20: (a <=> b) can be compared directly with 0 in ternary and if conditions
struct Pair {
    int a;
    int b;
    auto operator<=>(const Pair&) const = default;
};

int main() {
    Pair x{1, 2};
    Pair y{1, 3};

    // Direct use in ternary
    int r = (x <=> y) < 0 ? 10 : 20;

    // Direct use in if condition
    int result = 0;
    if ((x <=> y) < 0) result += 1;
    if ((y <=> x) > 0) result += 2;
    if ((x <=> x) == 0) result += 4;

    return (r == 10 && result == 7) ? 17 : 0;
}
