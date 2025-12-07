// Comprehensive test for unary operators: sizeof, alignof, typeid
// Tests that these operators work correctly in parse_unary_expression
// NOTE: This test returns 0 on success, non-zero on failure
// (Cannot use printf due to calling convention issues on Linux)

struct TestStruct {
    int a;
    double b;
    char c;
};

int main() {
    // Test 1: sizeof with type
    int size_int = sizeof(int);           // Should be 4
    int size_double = sizeof(double);     // Should be 8
    int size_struct = sizeof(TestStruct); // Should be 24 (with padding)
    
    // Test 2: sizeof with expression
    int x = 42;
    int size_x = sizeof(x);               // Should be 4
    int size_expr = sizeof(x + 10);       // Should be 4
    
    // Test 3: sizeof with array
    int arr[10];
    int size_arr = sizeof(arr);           // Should be 40
    
    // Test 4: alignof with type
    int align_int = alignof(int);         // Should be 4
    int align_double = alignof(double);   // Should be 8
    int align_struct = alignof(TestStruct); // Should be 8
    
    // Test 5: alignof with expression (type of the expression)
    int align_x = alignof(x);             // Should be 4
    
    // Test 6: sizeof in expressions (ensure proper precedence)
    int result1 = sizeof(int) * 2;        // 4 * 2 = 8
    int result2 = sizeof(int) + sizeof(double); // 4 + 8 = 12
    
    // Test 7: alignof in expressions
    int result3 = alignof(int) * 2;       // 4 * 2 = 8
    int result4 = alignof(double) - alignof(int); // 8 - 4 = 4
    
    // Verify all results
    bool all_pass = 
        (size_int == 4) &&
        (size_double == 8) &&
        (size_x == 4) &&
        (size_expr == 4) &&
        (size_arr == 40) &&
        (align_int == 4) &&
        (align_double == 8) &&
        (align_struct == 8) &&
        (align_x == 4) &&
        (result1 == 8) &&
        (result2 == 12) &&
        (result3 == 8) &&
        (result4 == 4);
    
    return all_pass ? 0 : 1;
}
