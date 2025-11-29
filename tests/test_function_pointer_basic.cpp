// Test: Basic function pointer declaration
// This tests the MVP (Minimum Viable Product) for function pointers
// Phase 1: Parse basic function pointer declarations

int add(int a, int b) {
    return a + b;
}

int main() {
    // Basic function pointer declaration
    int (*fp)(int, int);
    
    // Assign function to pointer
    fp = add;
    
    // Call through function pointer
    int result = fp(10, 20);

    int (*fp2)(int, int) = add;
    
    return result + fp2(30, 40);  // Should return 100
}

