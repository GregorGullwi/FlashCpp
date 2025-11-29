// Final validation test - combining both fixes:
// 1. Implicit constructor handling (no spurious calls to auto-generated ctors)
// 2. Struct parameter passing (small structs passed by value in registers)

struct Point {
    int x;
    int y;
    // No explicit constructor - compiler will auto-generate one
};

struct Circle {
    int x;
    int y;
    
    // User-defined constructor
    Circle() {
        x = 5;
        y = 5;
    }
};

class Geometry {
public:
    // Member function accepting struct without explicit constructor
    int sumPoint(Point p) {
        return p.x + p.y;
    }
    
    // Member function accepting struct with explicit constructor
    int sumCircle(Circle c) {
        return c.x + c.y;
    }
};

int main() {
    Geometry geo;
    
    // Test 1: Struct without explicit constructor (implicit handling)
    Point p;
    p.x = 5;
    p.y = 7;
    int r1 = geo.sumPoint(p);  // 12 - tests struct param passing
    
    // Test 2: Struct with explicit constructor
    Circle c;  // Calls user-defined constructor
    int r2 = geo.sumCircle(c);  // 10 - tests both fixes
    
    // Total: 12 + 10 = 22
    return r1 + r2;
}
