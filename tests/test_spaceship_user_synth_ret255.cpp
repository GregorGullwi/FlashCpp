// Test: all 6 synthesized operators from user-defined operator<=> returning int
// C++20: user-defined operator<=> synthesizes ==, !=, <, >, <=, >=
struct Point {
    int x, y;
    int operator<=>(const Point& other) const {
        if (x != other.x) return x - other.x;
        return y - other.y;
    }
};

int main() {
    Point a{1, 2};
    Point b{1, 3};
    Point c{1, 2};

    int score = 0;

    // Equality operators
    if (a == c) score += 1;
    if (a != b) score += 2;

    // Relational operators
    if (a < b) score += 4;
    if (b > a) score += 8;
    if (a <= b) score += 16;
    if (b >= a) score += 32;
    if (a <= c) score += 64;
    if (a >= c) score += 128;

    return score;  // expect 255
}
