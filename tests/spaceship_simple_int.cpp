// Simplest test - just return int
struct Point {
    int x;
    int y;
    
    int operator<=>(const Point& other) const {
        if (x < other.x) return -1;
        if (x > other.x) return 1;
        if (y < other.y) return -1;
        if (y > other.y) return 1;
        return 0;
    }
};

int main() {
    Point p1{1, 2};
    Point p2{1, 3};
    int result = p1 <=> p2;
    return result;
}
