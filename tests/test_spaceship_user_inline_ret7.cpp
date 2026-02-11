// Test: inline (a <=> b) with user-defined operator<=> returning int
// C++20: three-way comparison result used directly in expressions
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

    int cmp = a <=> b;

    int score = 0;
    if (cmp < 0) score += 1;
    if ((a <=> b) < 0) score += 2;
    if ((b <=> a) > 0) score += 4;

    return score;  // expect 7
}
