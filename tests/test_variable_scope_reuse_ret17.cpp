// Test case for issue #371: Reusing variable names in different sub-scopes
// This test verifies that variables can be reused in sequential for loops and blocks
int main() {
    int result = 0;
    
    // Test 1: Reusing variable in sequential for loops
    for (int i = 0; i < 3; i++) {
        result += i;
    }
    // First loop adds: 0 + 1 + 2 = 3
    
    for (int i = 0; i < 3; i++) {
        result += i * 2;
    }
    // Second loop adds: 0 + 2 + 4 = 6
    // Total so far: 3 + 6 = 9
    
    // Test 2: Reusing variable in sequential blocks
    {
        int x = 5;
        result += x;
    }
    // Adds 5, total: 9 + 5 = 14
    
    {
        int x = 3;
        result += x;
    }
    // Adds 3, total: 14 + 3 = 17
    
    return result;  // Expected: 17
}
