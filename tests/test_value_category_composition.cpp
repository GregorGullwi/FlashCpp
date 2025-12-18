// Test for composed value category tracking
// Validates that metadata propagates through nested expressions

struct Point {
    int x;
    int y;
};

Point makePoint() {
    Point p;
    p.x = 10;
    p.y = 20;
    return p;
}

int main() {
    // Test 1: Function return is prvalue
    Point p1 = makePoint();
    
    // Test 2: Array element is lvalue
    Point arr[3];
    arr[0].x = 5;
    arr[1].y = 15;
    
    // Test 3: Member access on lvalue
    p1.x = 30;
    p1.y = 40;
    
    // Test 4: Arithmetic is prvalue
    int result = p1.x + arr[0].x + arr[1].y;
    
    return result;  // Should return 30 + 5 + 15 = 50
}
