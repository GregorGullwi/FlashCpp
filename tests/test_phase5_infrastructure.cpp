// Test to validate Phase 5 infrastructure
// This test exercises the value category checks in IRConverter
// Run with --log-level=Codegen:debug to see the optimization logging

struct Point {
    int x;
    int y;
};

Point makePoint(int x, int y) {
    Point p;
    p.x = x;
    p.y = y;
    return p;  // PRValue return
}

int main() {
    // Test 1: Array access (lvalue) - should log is_lvalue=true
    int arr[5];
    arr[0] = 10;
    arr[1] = 20;
    
    // Test 2: Struct array access (lvalue) - should use LEA
    Point points[3];
    points[0].x = 5;
    points[1].y = 15;
    
    // Test 3: Function call (prvalue) - should log is_prvalue=true  
    Point p = makePoint(30, 40);
    
    // Test 4: Verify values
    int result = arr[0] + arr[1] + points[0].x + points[1].y + p.x + p.y;
    
    return result;  // Should return 10 + 20 + 5 + 15 + 30 + 40 = 120
}
