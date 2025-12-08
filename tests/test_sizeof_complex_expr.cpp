// Test case for sizeof with complex expressions in constexpr contexts
// This tests the compiler's ability to handle sizeof with various expression types
// Some of these may not be evaluable as compile-time constants, demonstrating
// the limitation where full type inference would be needed

// Test 1: sizeof with binary expression
// Note: This may work via IR generation fallback
int test1() {
    int a = 10;
    int b = 20;
    return sizeof(a + b);
}

// Test 2: sizeof with function call
int get_value() { return 42; }

int test2() {
    return sizeof(get_value());
}

// Test 3: sizeof with member access
struct Point {
    int x;
    int y;
};

int test3() {
    Point p{10, 20};
    return sizeof(p.x);
}

// Test 4: sizeof with array subscript
int test4() {
    int arr[5] = {1, 2, 3, 4, 5};
    return sizeof(arr[0]);
}

// Test 5: sizeof with ternary operator
int test5() {
    int a = 10;
    int b = 20;
    return sizeof(true ? a : b);
}

// Test 6: sizeof with cast expression
int test6() {
    double d = 3.14;
    return sizeof(static_cast<int>(d));
}

// Test 7: sizeof with dereference
int test7() {
    int value = 42;
    int* ptr = &value;
    return sizeof(*ptr);
}

int main() {
    // These should all compile and work via IR generation
    int s1 = test1();
    int s2 = test2();
    int s3 = test3();
    int s4 = test4();
    int s5 = test5();
    int s6 = test6();
    int s7 = test7();
    
    return s1 + s2 + s3 + s4 + s5 + s6 + s7;
}

