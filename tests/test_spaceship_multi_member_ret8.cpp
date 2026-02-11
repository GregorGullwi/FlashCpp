// Test: defaulted spaceship operator with multiple members
// C++20: memberwise three-way comparison in declaration order
struct Point {
    int x;
    int y;
    auto operator<=>(const Point&) const = default;
};

int main() {
    Point a{1, 2};
    Point b{1, 3};  // same x, different y
    Point c{2, 0};  // different x
    Point d{1, 2};  // same as a
    
    // a vs b: x equal, y: 2 < 3 → a < b
    int r1 = (a < b) ? 1 : 0;    // true → 1
    int r2 = (b > a) ? 1 : 0;    // true → 1
    
    // a vs c: x: 1 < 2 → a < c (y doesn't matter)
    int r3 = (a < c) ? 1 : 0;    // true → 1
    
    // a vs d: all members equal
    int r4 = (a == d) ? 1 : 0;   // true → 1
    int r5 = !(a < d) ? 1 : 0;   // not less → 1
    int r6 = !(a > d) ? 1 : 0;   // not greater → 1
    int r7 = (a <= d) ? 1 : 0;   // true → 1
    int r8 = (a >= d) ? 1 : 0;   // true → 1
    
    return r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8;  // Should be 8
}
