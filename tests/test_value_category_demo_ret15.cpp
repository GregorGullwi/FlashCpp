// Test to demonstrate value category tracking infrastructure
// This test validates that the value category system compiles and works

int main() {
    // Simple variable - should be lvalue
    int x = 42;
    
    // Assignment - x is lvalue
    x = 10;
    
    // Expression - prvalue
    int y = x + 5;
    
    return y;  // Should return 15
}
