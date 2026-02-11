// Test: defaulted operator<=> with negative values
// C++20: memberwise comparison handles signed integers correctly
struct Val {
    int x;
    auto operator<=>(const Val&) const = default;
};

int main() {
    Val a{-5};
    Val b{5};
    Val c{-5};

    int score = 0;
    if (a < b) score += 1;     // -5 < 5 -> true
    if (b > a) score += 2;     // 5 > -5 -> true
    if (a == c) score += 4;    // -5 == -5 -> true
    if (a <= c) score += 8;    // -5 <= -5 -> true
    if (a != b) score += 16;   // -5 != 5 -> true

    return score;  // expect 31
}
