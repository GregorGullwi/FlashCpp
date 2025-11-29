// Test variadic function that can be linked and run
// This test declares variadic functions but doesn't call them
// (since we don't have va_start/va_arg implementation yet)

extern "C" {
    // Declare printf as variadic
    int printf(const char* format, ...);
}

int main() {
    // Just return success - we're testing that variadic declarations
    // compile, link, and the program runs
    return 0;
}

