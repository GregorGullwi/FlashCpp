// Test: Implicit designated initializers as function arguments
// Verifies that func({.x=1, .y=2}) works (C++20 feature)
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

int main() {
    int result = 0;
    
    // Test basic implicit designated init as function arg
    if (getX({.x = 42, .y = 10}) == 42) result += 1;
    
    // Test with multiple member access
    if (getSum({.x = 20, .y = 22}) == 42) result += 2;
    
    // Test explicit for comparison
    if (getX(Point{.x = 42, .y = 0}) == 42) result += 4;
    
    // result should be 1+2+4 = 7
    return result == 7 ? 42 : 0;
}
