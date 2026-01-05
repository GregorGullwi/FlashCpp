// Test file for C++20 Grammar Audit compliance fixes
// This test validates the following fixes:
// 1. Declaration-starting keywords correctly route to parse_declaration_or_function_definition
// 2. Comma-separated variable declarations work correctly in blocks
// 3. Range-based for with init-statement (C++20)

// Test 1: Static function at top-level (should work)
static int static_helper() {
    return 10;
}

// Test 2: Comma-separated declarations in function body
int test_comma_decl() {
    int a = 5, b = 3, c = 8;  // All three should be accessible
    return a + b + c;  // Should return 16
}

// Test 3: Range-based for loop (basic)
int test_range_for_basic() {
    int arr[3] = {1, 2, 3};
    int sum = 0;
    for (int x : arr) {
        sum += x;
    }
    return sum;  // Should return 6
}

// Test 4: Range-based for with init-statement (C++20)
int test_range_for_with_init() {
    int arr[5] = {1, 2, 3, 4, 5};
    int result = 0;
    
    // C++20 range-based for with init-statement
    for (int multiplier = 2; auto x : arr) {
        result += x * multiplier;
    }
    
    return result;  // Should be (1+2+3+4+5) * 2 = 30
}

// Test 5: Multiple comma-separated declarations with different initializers
int test_multi_init() {
    int x = 10, y = 20, z = x + y;
    return z;  // Should return 30
}

// Test 6: Constexpr with comma (if supported)
int test_constexpr_comma() {
    constexpr int a = 5, b = 10;
    return a * b;  // Should return 50
}

int main() {
    int result = 0;
    result += static_helper();        // 10
    result += test_comma_decl();      // 16
    result += test_range_for_basic(); // 6
    result += test_range_for_with_init(); // 30
    result += test_multi_init();      // 30
    result += test_constexpr_comma(); // 50
    
    return result;  // Total: 10 + 16 + 6 + 30 + 30 + 50 = 142
}
