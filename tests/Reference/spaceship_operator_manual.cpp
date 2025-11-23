// Test for spaceship operator - manual implementation
struct SimpleOrdering {
    int value;
    
    SimpleOrdering(int v) : value(v) {}
};

struct Point {
    int x;
    int y;
    
    // Manual spaceship operator implementation
    SimpleOrdering operator<=>(const Point& other) const {
        if (x < other.x) return SimpleOrdering(-1);
        if (x > other.x) return SimpleOrdering(1);
        if (y < other.y) return SimpleOrdering(-1);
        if (y > other.y) return SimpleOrdering(1);
        return SimpleOrdering(0);
    }
};

int main() {
    Point p1{1, 2};
    Point p2{1, 3};
    SimpleOrdering result = p1 <=> p2;
    return result.value;
}
