// Test: braced initializer lists in return statements
struct Point {
    int x;
    int y;
};

Point makePoint(int a, int b) {
    return {a, b};
}

Point makeDesignated() {
    return {.x = 20, .y = 22};
}

int main() {
    int result = 0;
    
    // Test return with positional brace init
    Point p1 = makePoint(10, 32);
    if (p1.x == 10 && p1.y == 32) result += 1;
    
    // Test return with designated brace init
    Point p2 = makeDesignated();
    if (p2.x == 20 && p2.y == 22) result += 2;
    
    // result should be 1+2 = 3
    return result == 3 ? 42 : 0;
}
