// Comprehensive test for base class and delegating constructors

struct Base {
    int value;
    
    Base() : value(10) {}
    Base(int v) : value(v) {}
};

struct Derived : Base {
    int extra;
    
    // Delegating constructor
    Derived() : Derived(100) {}
    
    // Base class constructor with member init
    Derived(int e) : Base(50), extra(e) {}
};

struct Point {
    int x;
    int y;
    
    // Delegating constructors
    Point() : Point(0, 0) {}
    Point(int v) : Point(v, v) {}
    Point(int x, int y) : x(x), y(y) {}
};

int main() {
    Derived d1;      // Delegates to Derived(100), which calls Base(50)
    Derived d2(200); // Calls Base(50), sets extra=200
    
    Point p1;        // Delegates to Point(0,0)
    Point p2(3);     // Delegates to Point(3,3)
    Point p3(5, 7);  // Direct initialization
    
    // d1: Base::value=50, extra=100  → 50+100 = 150
    // d2: Base::value=50, extra=200  → 50+200 = 250
    // p1: x=0, y=0                   → 0+0 = 0
    // p2: x=3, y=3                   → 3+3 = 6
    // p3: x=5, y=7                   → 5+7 = 12
    // Total: 150 + 250 + 0 + 6 + 12 = 418
    
    return d1.value + d1.extra + d2.value + d2.extra + p1.x + p1.y + p2.x + p2.y + p3.x + p3.y;
}
