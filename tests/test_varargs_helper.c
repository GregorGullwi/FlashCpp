// Test variadic function support

// Forward declare printf - will be declared in the parent file's extern "C" block

// Proper varargs implementation for FlashCpp with System V AMD64 ABI support
// Uses __builtin_va_start and __builtin_va_arg intrinsics

// __builtin_va_start is a compiler intrinsic - no need for forward declaration
#define va_start(ap, param) __builtin_va_start(ap, param)

// Use __builtin_va_arg for proper type-aware va_arg implementation
// This supports both integer and floating-point types correctly on System V AMD64
#define va_arg(ap, type) __builtin_va_arg(ap, type)

// Platform-specific va_end implementation
// System V AMD64 (Linux/ELF): va_list is typically char*
// MS x64 (Windows/COFF): va_list is typically char*
// Both can use simple null assignment for cleanup
#ifdef _WIN32
#define va_end(ap) ((void)(ap = 0))
#else
#define va_end(ap) ((void)(ap = 0))
#endif

// External variadic function compiled with gcc
int sum_ints(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int sum = 0;
    for (int i = 0; i < count; i++) {
        sum += va_arg(args, int);
    }
    
    va_end(args);
    return sum;
}

// Variadic function with mixed types
double sum_mixed(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        double val = va_arg(args, double);  // floats promoted to double in varargs
        printf("  arg[%d] = %.1f\n", i, val);
        sum += val;
    }
    
    va_end(args);
    return sum;
}
