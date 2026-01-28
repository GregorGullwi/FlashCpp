// Test variadic function with printf-style declaration
// This is a common use case for variadic functions

extern "C" {
    // Standard printf declaration with variadic parameters
    int printf(const char* format, ...);

    // sprintf declaration
    int sprintf(char* buffer, const char* format, ...);

    // snprintf declaration
    int snprintf(char* buffer, int size, const char* format, ...);
}

int main() {
    // Test that we can declare these functions
    // Actual calls would require va_list implementation
    return 0;
}

