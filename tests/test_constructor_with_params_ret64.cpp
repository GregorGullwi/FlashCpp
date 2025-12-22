// Test constructor with parameters and constructor overloading

struct Point {
    int x;
    int y;

    // Default constructor
    Point() {
        x = 0;
        y = 0;
    }

    // Single parameter constructor (sets both x and y to same value)
    Point(int v) {
        x = v;
        y = v;
    }

    // Two parameter constructor
    Point(int a, int b) {
        x = a;
        y = b;
    }
};

int main() {
    // Test brace initialization with two-parameter constructor
    Point p1 = {10, 20};

    // Test direct initialization with two parameters
    Point p2(5, 15);

    // Test direct initialization with one parameter
    Point p3(7);

    // Test default constructor
    Point p4;

    // Calculate result: (10+20) + (5+15) + (7+7) + (0+0) = 30 + 20 + 14 + 0 = 64
    return p1.x + p1.y + p2.x + p2.y + p3.x + p3.y + p4.x + p4.y;  // Should return 64
}

