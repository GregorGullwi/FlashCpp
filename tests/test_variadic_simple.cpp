// Simple test for variadic function declarations
// This tests basic parsing and compilation of variadic functions

// Variadic function with one named parameter
extern "C" int sum_ints(int count, ...);

// Variadic function with multiple named parameters
extern "C" void log_message(const char* format, int level, ...);

// Variadic function with no named parameters (just ellipsis) - not valid in C/C++
// This should be rejected by the parser

int main() {
    // Just test that we can declare variadic functions
    // We don't call them since we don't have va_list implementation yet
    return 0;
}

