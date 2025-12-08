// Test variadic function support
#include <stdio.h>
#include <stdarg.h>

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
