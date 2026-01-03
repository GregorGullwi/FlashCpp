// Combined basic spaceship operator tests
struct Point1 {
    int x, y;
    int operator<=>(const Point1& other) const {
        if (x != other.x) return x - other.x;
        return y - other.y;
    }
};

struct Point2 {
    int x, y;
    int operator<=>(const Point2& other) const {
        if (x < other.x) return -1;
        if (x > other.x) return 1;
        if (y < other.y) return -1;
        if (y > other.y) return 1;
        return 0;
    }
};

struct SimpleOrdering {
    int value;
    SimpleOrdering(int v) : value(v) {}
};

struct Point3 {
    int x, y;
    SimpleOrdering operator<=>(const Point3& other) const {
        if (x < other.x) return SimpleOrdering(-1);
        if (x > other.x) return SimpleOrdering(1);
        if (y < other.y) return SimpleOrdering(-1);
        if (y > other.y) return SimpleOrdering(1);
        return SimpleOrdering(0);
    }
};

int main() {
    Point1 p1; p1.x=1; p1.y=2;
    Point1 p1b; p1b.x=1; p1b.y=3;
    int r1 = p1 <=> p1b;
    
    Point2 p2{1,2};
    Point2 p2b{1,3};
    int r2 = p2 <=> p2b;
    
    Point3 p3{1,2};
    Point3 p3b{1,3};
    SimpleOrdering r3 = p3 <=> p3b;
    int r3v = r3.value;
    
    return r1 + r2 + r3v;  // -1 + -1 + -1 = -3
}