struct Point {
    int x;
    int y;
};

// Test 1: Basic member access (working)
int test_member_access() {
    Point p;
    p.x = 10;
    p.y = 20;
    return p.x + p.y;  // Should return 30
}

// Test 2: Struct assignment (working)
int test_struct_assign() {
    Point p1;
    p1.x = 5;
    p1.y = 10;

    Point p2;
    p2 = p1;

    return p2.x + p2.y;  // Should return 15
}

// Test 3: Struct parameter passing (working)
int addPoint(Point p) {
    return p.x + p.y;
}

int test_struct_param() {
    Point p;
    p.x = 7;
    p.y = 8;
    return addPoint(p);  // Should return 15
}

// Test 4: Struct return values (working)
Point createPoint() {
    Point p;
    p.x = 5;
    p.y = 10;
    return p;
}

int test_struct_return() {
    Point p = createPoint();
    return p.x + p.y;  // Should return 15
}

// Test 5: Brace initialization - single member (WORKING!)
int test_brace_init_single() {
    Point p = {10};
    return p.x;  // Should return 10
}

// Test 6: Brace initialization - both members (WORKING!)
int test_brace_init_both() {
    Point p = {10, 20};
    return p.x + p.y;  // Should return 30
}

// Main test function - demonstrates all working struct features
// Tests: brace init, member access, function params, and return values
int test() {
    // Brace initialization with both members
    Point p1 = {5, 10};

    // Brace initialization with single member
    Point p2 = {20};

    // Brace init and pass to function
    Point p3 = {3, 4};

    // Get struct from function
    Point p4 = createPoint();  // Returns (5, 10)

    // Calculate result using all features:
    // p1: 5+10=15, p2: 20, addPoint(p3): 7, p4: 5+10=15
    // Total: 15 + 20 + 7 + 15 = 57
    return p1.x + p1.y + p2.x + addPoint(p3) + p4.x + p4.y;
}
