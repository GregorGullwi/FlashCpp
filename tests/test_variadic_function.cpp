// Test variadic function declarations

extern "C" {
    // Standard printf declaration with variadic parameters
    int printf(const char* format, ...);

    // Another variadic function
    int sprintf(char* buffer, const char* format, ...);
}

int main() {
    return 0;
}
