// Test for bool conditional bug
// Expected: should return 0 (all tests pass)
// Bug: bool variables evaluate incorrectly in if() and ternary operators

int main() {
    // Test 1: Direct bool literal in if statement
    bool test1 = false;
    if (test1) {
        return 1; // Should NOT execute
    }
    
    // Test 2: Direct bool literal in ternary
    bool test2 = false;
    int result2 = test2 ? 1 : 0;
    if (result2 != 0) {
        return 2; // Should NOT execute
    }
    
    // Test 3: Bool variable set to true
    bool test3 = true;
    if (!test3) {
        return 3; // Should NOT execute
    }
    
    // Test 4: Bool from comparison
    bool test4 = (5 > 10);
    if (test4) {
        return 4; // Should NOT execute
    }
    
    // Test 5: Bool from comparison (true case)
    bool test5 = (10 > 5);
    if (!test5) {
        return 5; // Should NOT execute
    }
    
    return 0; // All tests passed
}
