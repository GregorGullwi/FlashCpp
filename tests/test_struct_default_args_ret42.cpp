// Test: struct default arguments
// Covers braced init lists, constructor calls, member function defaults,
// template functions with struct defaults, and multiple parameter defaults.

struct Point {
    int x;
    int y;
};

// Free function with braced init list default
int sumPoint(Point p = {20, 22}) {
    return p.x + p.y;
}

// Free function with explicit constructor default
int sumPoint2(Point p = Point{10, 32}) {
    return p.x + p.y;
}

// Multiple params, second is struct default
int addToPoint(int base, Point p = {10, 10}) {
    return base + p.x + p.y;
}

// Template function with struct default arg
template<typename T>
T addPointToVal(T val, Point p = {10, 32}) {
    return val + p.x + p.y;
}

// Struct with member function using struct default
struct Calculator {
    int offset;
    int compute(Point p = {21, 21}) {
        return p.x + p.y + offset;
    }
};

int main() {
    // 1. Braced init list default
    int r1 = sumPoint();            // {20, 22} -> 42
    if (r1 != 42) return 1;

    // 2. Explicit call (override default)
    Point p2{40, 2};
    int r2 = sumPoint(p2);          // 42
    if (r2 != 42) return 2;

    // 3. Constructor call default
    int r3 = sumPoint2();           // Point{10, 32} -> 42
    if (r3 != 42) return 3;

    // 4. Multiple params with struct default
    int r4 = addToPoint(22);        // 22 + {10, 10} -> 42
    if (r4 != 42) return 4;

    // 5. Override struct default when multiple params
    Point p5{10, 10};
    int r5 = addToPoint(22, p5);    // 22 + 10 + 10 = 42
    if (r5 != 42) return 5;

    // 6. Template function with struct default
    int r6 = addPointToVal<int>(0);    // 0 + 10 + 32 = 42
    if (r6 != 42) return 6;

    // 7. Member function with struct default
    Calculator c;
    c.offset = 0;
    int r7 = c.compute();           // {21, 21} + 0 = 42
    if (r7 != 42) return 7;

    // 8. Member function override default
    Point p8{20, 22};
    int r8 = c.compute(p8);         // 20 + 22 + 0 = 42
    if (r8 != 42) return 8;

    return 42;
}
