// Test const member function with parameter
struct SimpleOrdering {
    int value;
    
    SimpleOrdering(int v) : value(v) {}
};

struct Point {
    int x;
    int y;
    
    SimpleOrdering test(const Point& other) const {
        if (x < other.x) return SimpleOrdering(-1);
        return SimpleOrdering(0);
    }
};

int main() {
    Point p1{1, 2};
    Point p2{2, 3};
    SimpleOrdering result = p1.test(p2);
    return result.value;
}
