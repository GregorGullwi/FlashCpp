// Test variadic function support

// Forward declare printf - will be declared in the parent file's extern "C" block

// Hybrid varargs implementation for FlashCpp
// Uses __va_start builtin (declared in parent file) with macro-based va_arg/va_end
// Note: va_list and __va_start should be declared at file scope before including this file

// __builtin_va_start is a compiler intrinsic - no need for forward declaration
#define va_start(ap, param) __builtin_va_start(ap, param)

// MSVC-style macros for va_arg and va_end
#define va_arg(ap, type) (*(type*)((ap += 8) - 8))
#define va_end(ap) (ap = (va_list)0)

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
