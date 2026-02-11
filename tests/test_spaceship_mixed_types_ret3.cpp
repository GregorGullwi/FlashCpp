// Test: defaulted operator<=> with mixed member types (int, char, short)
// C++20: memberwise comparison works across different integral types
struct Mixed {
    int a;
    char b;
    short c;
    auto operator<=>(const Mixed&) const = default;
};

int main() {
    Mixed x{10, 'a', 100};
    Mixed y{10, 'b', 100};
    Mixed z{10, 'a', 100};

    int score = 0;
    if (x < y) score += 1;   // a same, b: 'a' < 'b' -> true
    if (x == z) score += 2;  // all same -> true

    return score;  // expect 3
}
