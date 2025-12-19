// Simple test: basic RVO case that SHOULD work
extern "C" int printf(const char*, ...);

struct Point {
    int x;
    int y;
    
    Point(int a, int b) : x(a), y(b) {
        printf("Point(%d, %d) constructor\n", a, b);
    }
};

// Simple RVO case - returning prvalue
Point makePoint() {
    return Point(3, 4);
}

int main() {
    printf("=== Simple RVO Test ===\n");
    Point p = makePoint();
    printf("Result: x=%d, y=%d\n", p.x, p.y);
    
    if (p.x == 3 && p.y == 4) {
        printf("TEST PASSED\n");
        return 0;
    } else {
        printf("TEST FAILED: Expected x=3, y=4 but got x=%d, y=%d\n", p.x, p.y);
        return 1;
    }
}
