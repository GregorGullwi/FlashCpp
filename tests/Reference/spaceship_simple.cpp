// Simple spaceship operator test
struct Point {
    int x;
    int y;
    
    // Manual spaceship operator
    int operator<=>(const Point& other) const {
        if (x != other.x) return x - other.x;
        return y - other.y;
    }
};

int main() {
    Point p1;
    p1.x = 1;
    p1.y = 2;
    
    Point p2;
    p2.x = 1;
    p2.y = 3;
    
    int result = p1 <=> p2;
    return result;
}
