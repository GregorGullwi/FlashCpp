// Comprehensive test of struct parameter passing fixes
// This test validates that small structs (≤8 bytes) are passed by value in registers
// according to the x64 Windows calling convention

struct Point {
    int x;
    int y;
};

// Regular function with struct parameter
int addPoint(Point p) {
    return p.x + p.y;
}

// Regular function with multiple struct parameters
int sumPoints(Point p1, Point p2) {
    return p1.x + p1.y + p2.x + p2.y;
}

class Calculator {
public:
    // Member function with struct parameter
    int sum(Point p) {
        return p.x + p.y;
    }
    
    // Member function with multiple struct parameters
    int sumTwo(Point p1, Point p2) {
        return p1.x + p1.y + p2.x + p2.y;
    }
};

int main() {
    Point p1;
    p1.x = 5;
    p1.y = 7;
    
    Point p2;
    p2.x = 3;
    p2.y = 4;
    
    // Test 1: Regular function, single struct parameter
    int r1 = addPoint(p1);  // 5 + 7 = 12
    
    // Test 2: Regular function, multiple struct parameters
    int r2 = sumPoints(p1, p2);  // 5+7+3+4 = 19
    
    // Test 3: Member function, single struct parameter
    Calculator calc;
    int r3 = calc.sum(p1);  // 5 + 7 = 12
    
    // Test 4: Member function, multiple struct parameters
    int r4 = calc.sumTwo(p1, p2);  // 5+7+3+4 = 19
    
    // Total: 12 + 19 + 12 + 19 = 62
    return r1 + r2 + r3 + r4;
}
