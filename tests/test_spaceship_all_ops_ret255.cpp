// Test: all 6 synthesized operators from defaulted operator<=> plus direct use
// C++20: auto operator<=>(const T&) const = default; synthesizes ==, !=, <, >, <=, >=
struct Wrapper {
    int value;
    auto operator<=>(const Wrapper&) const = default;
};

int main() {
    Wrapper a{5};
    Wrapper b{10};
    Wrapper c{5};

    int score = 0;
    // Relational operators
    if (a < b) score += 1;
    if (b > a) score += 2;
    if (a <= b) score += 4;
    if (b >= a) score += 8;
    if (a <= c) score += 16;
    if (a >= c) score += 32;
    // Equality operators
    if (a == c) score += 64;
    if (a != b) score += 128;

    return score;  // expect 255
}
