// Comprehensive test for struct passing by value, const reference, and reference
// Tests both register passing (first 4 args) and stack passing (5th+ args)

struct Point {
    int x;
    int y;
};

struct LargeStruct {
    int a;
    int b;
    int c;
    int d;
    int e;
};

// ============================================================================
// Test 1: Pass by value (should copy the struct)
// ============================================================================
int passByValue(Point p) {
    return p.x + p.y;
}

int testPassByValue() {
    Point p;
    p.x = 10;
    p.y = 20;
    return passByValue(p);  // Should return 30
}

// ============================================================================
// Test 2: Pass by const reference (read-only access, no copy)
// ============================================================================
int passByConstRef(const Point& p) {
    return p.x + p.y;
}

int testPassByConstRef() {
    Point p;
    p.x = 15;
    p.y = 25;
    return passByConstRef(p);  // Should return 40
}

// ============================================================================
// Test 3: Pass by reference (can modify the struct - "out" parameter)
// ============================================================================
void modifyByRef(Point& p) {
    p.x = 100;
    p.y = 200;
}

int testModifyByRef() {
    Point p;
    p.x = 1;
    p.y = 2;
    modifyByRef(p);
    return p.x + p.y;  // Should return 300
}

// ============================================================================
// Test 4: Mix of value and reference parameters (first 4 args - registers)
// ============================================================================
int mixedArgs1(Point p1, const Point& p2, Point& p3, Point p4) {
    p3.x = 50;
    p3.y = 60;
    return p1.x + p2.y + p3.x + p4.y;
}

int testMixedArgs1() {
    Point a, b, c, d;
    a.x = 1; a.y = 2;
    b.x = 3; b.y = 4;
    c.x = 5; c.y = 6;
    d.x = 7; d.y = 8;
    
    int result = mixedArgs1(a, b, c, d);
    // result = a.x + b.y + c.x(modified to 50) + d.y = 1 + 4 + 50 + 8 = 63
    // c should now be {50, 60}
    return result + c.y;  // 63 + 60 = 123
}

// ============================================================================
// Test 5: 5+ arguments to test stack passing
// ============================================================================
int manyArgs(Point p1, Point p2, const Point& p3, Point& p4, Point p5, const Point& p6) {
    // First 4 args should be in registers (RCX, RDX, R8, R9)
    // Args 5-6 should be on stack
    p4.x = 77;
    p4.y = 88;
    return p1.x + p2.y + p3.x + p4.y + p5.x + p6.y;
}

int testManyArgs() {
    Point a, b, c, d, e, f;
    a.x = 1; a.y = 2;
    b.x = 3; b.y = 4;
    c.x = 5; c.y = 6;
    d.x = 7; d.y = 8;
    e.x = 9; e.y = 10;
    f.x = 11; f.y = 12;
    
    int result = manyArgs(a, b, c, d, e, f);
    // result = a.x + b.y + c.x + d.y(modified to 88) + e.x + f.y
    //        = 1   + 4   + 5   + 88              + 9   + 12  = 119
    // d should now be {77, 88}
    return result + d.x;  // 119 + 77 = 196
}

// ============================================================================
// Test 6: Large struct by reference (should pass pointer, not copy)
// ============================================================================
int sumLargeStruct(const LargeStruct& ls) {
    return ls.a + ls.b + ls.c + ls.d + ls.e;
}

void modifyLargeStruct(LargeStruct& ls) {
    ls.a = 100;
    ls.b = 200;
    ls.c = 300;
    ls.d = 400;
    ls.e = 500;
}

int testLargeStruct() {
    LargeStruct ls;
    ls.a = 1;
    ls.b = 2;
    ls.c = 3;
    ls.d = 4;
    ls.e = 5;
    
    int sum1 = sumLargeStruct(ls);  // 1+2+3+4+5 = 15
    modifyLargeStruct(ls);
    int sum2 = sumLargeStruct(ls);  // 100+200+300+400+500 = 1500
    
    return sum1 + sum2;  // 15 + 1500 = 1515
}

// ============================================================================
// Test 7: Return struct by value (verify struct returns still work)
// ============================================================================
Point createPoint(int x, int y) {
    Point p;
    p.x = x;
    p.y = y;
    return p;
}

int testStructReturn() {
    Point p = createPoint(33, 44);
    return p.x + p.y;  // 33 + 44 = 77
}

// ============================================================================
// Main test function
// ============================================================================
int main() {
    int total = 0;
    
    total += testPassByValue();        // 30
    total += testPassByConstRef();     // 40
    total += testModifyByRef();        // 300
    total += testMixedArgs1();         // 123
    total += testManyArgs();           // 196
    total += testLargeStruct();        // 1515
    total += testStructReturn();       // 77
    
    // Total: 30 + 40 + 300 + 123 + 196 + 1515 + 77 = 2281
    return total;
}
