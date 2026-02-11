// Test: Designated initializers as function arguments and braced initializer return statements
struct Point {
    int x;
    int y;
};

int getX(Point p) {
    return p.x;
}

int getSum(Point p) {
    return p.x + p.y;
}

Point makePoint(int a, int b) {
    return {a, b};
}

Point makeDesignated() {
    return {.x = 20, .y = 22};
}

int main() {
    int result = 0;
    
    // Test implicit designated init as function arg
    if (getX({.x = 42, .y = 10}) == 42) result += 1;
    
    // Test with multiple member access
    if (getSum({.x = 20, .y = 22}) == 42) result += 2;
    
    // Test explicit type for comparison
    if (getX(Point{.x = 42, .y = 0}) == 42) result += 4;
    
    // Test return with positional brace init
    Point p1 = makePoint(10, 32);
    if (p1.x == 10 && p1.y == 32) result += 8;
    
    // Test return with designated brace init
    Point p2 = makeDesignated();
    if (p2.x == 20 && p2.y == 22) result += 16;
    
    // result should be 1+2+4+8+16 = 31
    return result == 31 ? 42 : 0;
}
