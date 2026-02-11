// Test: user-defined operator<=> returning custom ordering type
// C++20: operator<=> can return any type
struct SimpleOrdering {
    int value;
    SimpleOrdering(int v) : value(v) {}
};

struct Point {
    int x, y;
    SimpleOrdering operator<=>(const Point& other) const {
        if (x < other.x) return SimpleOrdering(-1);
        if (x > other.x) return SimpleOrdering(1);
        if (y < other.y) return SimpleOrdering(-1);
        if (y > other.y) return SimpleOrdering(1);
        return SimpleOrdering(0);
    }
};

int main() {
    Point a{1, 2};
    Point b{1, 3};

    SimpleOrdering cmp = a <=> b;
    int v = cmp.value;

    int score = 0;
    if (v < 0) score += 1;    // -1 < 0 -> true
    if (v != 0) score += 2;   // -1 != 0 -> true

    return score;  // expect 3
}
