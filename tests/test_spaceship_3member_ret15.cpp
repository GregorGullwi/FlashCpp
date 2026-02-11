// Test: three-member struct ordering priority
// C++20: defaulted operator<=> compares members in declaration order, short-circuits
struct Coord {
    int x;
    int y;
    int z;
    auto operator<=>(const Coord&) const = default;
};

int main() {
    Coord a{1, 2, 3};
    Coord b{1, 2, 4};  // differs at z
    Coord c{1, 3, 0};  // differs at y (y matters before z)
    Coord d{2, 0, 0};  // differs at x (x matters first)

    int score = 0;
    if (a < b) score += 1;    // z: 3 < 4 -> true
    if (a < c) score += 2;    // y: 2 < 3 -> true (z ignored)
    if (a < d) score += 4;    // x: 1 < 2 -> true (y,z ignored)
    if (!(d < a)) score += 8; // d is greater -> true

    return score;  // expect 15
}
