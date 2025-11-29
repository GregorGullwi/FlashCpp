// Test copy constructor with const reference parameter

struct Point {
    int x;
    int y;
    
    // Parameterized constructor
    Point(int a, int b) {
        x = a;
        y = b;
    }
    
    // Copy constructor (without const for now - compiler doesn't support const on references yet)
    Point(Point& other) {
        x = other.x;
        y = other.y;
    }
};

int main() {
    Point p1(10, 20);
    Point p2(p1);  // Call copy constructor
    
    return p2.x + p2.y;  // Should return 30
}

