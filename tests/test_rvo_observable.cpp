// Test RVO with observable copy constructor to verify copy elision
// When RVO is working, copy constructor should NOT be called

extern "C" int printf(const char*, ...);

static int copy_count = 0;
static int move_count = 0;
static int ctor_count = 0;

struct Point {
    int x;
    int y;
    
    // Regular constructor
    Point(int x_val, int y_val) : x(x_val), y(y_val) {
        ctor_count++;
        printf("Point(%d, %d) - constructor called (count=%d)\n", x_val, y_val, ctor_count);
    }
    
    // Copy constructor
    Point(const Point& other) : x(other.x), y(other.y) {
        copy_count++;
        printf("Point(const Point&) - copy constructor called (count=%d)\n", copy_count);
    }
    
    // Move constructor
    Point(Point&& other) : x(other.x), y(other.y) {
        move_count++;
        printf("Point(Point&&) - move constructor called (count=%d)\n", move_count);
    }
};

// RVO: returning temporary object (prvalue)
// With RVO: should call constructor once directly in return location
// Without RVO: would call constructor + copy constructor
Point makePoint() {
    return Point(3, 4);
}

int main() {
    printf("=== Testing RVO ===\n");
    Point p = makePoint();
    
    printf("\nResults:\n");
    printf("  Constructors: %d (expected: 1)\n", ctor_count);
    printf("  Copies: %d (expected: 0 with RVO)\n", copy_count);
    printf("  Moves: %d (expected: 0 with RVO)\n", move_count);
    printf("  Values: x=%d, y=%d (expected: 3, 4)\n", p.x, p.y);
    
    // Success if: exactly 1 constructor call, 0 copies, 0 moves, correct values
    if (ctor_count == 1 && copy_count == 0 && move_count == 0 && p.x == 3 && p.y == 4) {
        printf("\nRVO TEST PASSED\n");
        return 0;
    } else {
        printf("\nRVO TEST FAILED\n");
        return 1;
    }
}
