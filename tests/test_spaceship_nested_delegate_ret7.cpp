// Test: defaulted operator<=> with nested struct members delegates to inner <=>
// C++20: memberwise three-way comparison calls member's operator<=> for struct members
struct Inner {
    int x;
    int y;
    auto operator<=>(const Inner&) const = default;
};

struct Outer {
    Inner first;
    int extra;
    auto operator<=>(const Outer&) const = default;
};

int main() {
    Outer a;
    a.first.x = 1; a.first.y = 10;
    a.extra = 100;

    Outer b;
    b.first.x = 1; b.first.y = 20;
    b.extra = 50;

    Outer c;
    c.first.x = 1; c.first.y = 10;
    c.extra = 100;

    int score = 0;
    // first.x same, first.y: 10 < 20 -> a < b (extra ignored because first differs)
    if (a < b) score += 1;
    // all members equal
    if (a == c) score += 2;
    // a < b -> not greater
    if (!(a > b)) score += 4;

    return score;  // expect 7
}
