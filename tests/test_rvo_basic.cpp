// Test basic Return Value Optimization (RVO)
// C++17 mandates copy elision for prvalues

struct Point {
    int x;
    int y;
    
    Point(int x_val, int y_val) : x(x_val), y(y_val) {}
};

// RVO: returning temporary object (prvalue)
Point makePoint() {
    return Point(3, 4);
}

int main() {
    Point p = makePoint();
    return (p.x == 3 && p.y == 4) ? 0 : 1;
}
