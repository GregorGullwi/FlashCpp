// Test C++20 != equality rewrite via member operator== (not free-function)
struct Point {
    int x, y;
    bool operator==(const Point& o) const {
        return x == o.x && y == o.y;
    }
    // No operator!= defined; C++20 must synthesize it from operator==
};

int main() {
    Point a{1, 2};
    Point b{1, 2};
    Point c{3, 4};

    if (a != b) return 1;   // should be false (equal)
    if (!(a != c)) return 2; // should be true (not equal)
    return 0;
}
