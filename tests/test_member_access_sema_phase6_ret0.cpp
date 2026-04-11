// Test sema-owned member access type inference:
// - data member access via dot
// - member function calls via dot
// - implicit this->member resolution via NonStaticMember binding
// Phase 6 boundary plan regression test.

struct Point {
    int x;
    int y;

    int getX() const { return x; }
    int getY() const { return y; }
    int sum() const { return x + y; }
};

struct Container {
    int value;

    int getValue() const { return value; }

    int testImplicitThis() const {
        // implicit this->value via NonStaticMember binding
        return value + 10;
    }
};

int main() {
    Point p;
    p.x = 10;
    p.y = 32;

    // Data member access
    int a = p.x;
    int b = p.y;

    // Member function call
    int c = p.getX();
    int d = p.sum();

    Container cont;
    cont.value = 7;
    int e = cont.testImplicitThis(); // should be 17

    // a(10) + b(32) + c(10) + d(42) + e(17) - 111 = 0
    return a + b + c + d + e - 111;
}
