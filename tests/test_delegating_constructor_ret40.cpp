// Test delegating constructors (C++11 feature)

struct Point {
    int x;
    int y;
    
    Point() : Point(0, 0) {}  // Delegate to Point(int, int)
    Point(int val) : Point(val, val) {}  // Delegate to Point(int, int)
    Point(int x_val, int y_val) : x(x_val), y(y_val) {}  // Target constructor
};

int main() {
    Point p1;          // Uses delegating default constructor
    Point p2(5);       // Uses delegating single-arg constructor
    Point p3(10, 20);  // Uses target constructor directly
    
    return p1.x + p1.y + p2.x + p2.y + p3.x + p3.y;  // 0 + 0 + 5 + 5 + 10 + 20 = 40
}
