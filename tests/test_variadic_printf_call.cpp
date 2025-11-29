// Test variadic function with actual printf call
// This verifies that variadic functions can be called and linked properly

extern "C" {
    int printf(const char* format, ...);
}

int main() {
    // Call printf with variadic arguments
    printf("Hello from FlashCpp!\n");
    printf("Number: %d\n", 42);
    printf("Two numbers: %d and %d\n", 10, 20);
    
    return 0;
}

