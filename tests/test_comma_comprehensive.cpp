// Comprehensive test for comma operator
int global = 0;

int increment() {
    global = global + 1;
    return global;
}

int main() {
    // Test 1: Comma operator in variable initialization
    int a = (increment(), increment(), global);  // Should be 2
    
    // Test 2: Comma operator with multiple expressions
    int b = (1 + 2, 3 * 4, 5 - 1);  // Should be 4
    
    // Test 3: Comma operator in return statement
    return (increment(), global);  // Should return 3
}

