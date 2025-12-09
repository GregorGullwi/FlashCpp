// Test variadic function support

// Forward declare printf - will be declared in the parent file's extern "C" block

// Use compiler builtins for varargs instead of stdarg.h
// Windows x64 uses char*, Linux/GCC uses __builtin_va_list
#ifdef _WIN32
typedef char* va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)
#else
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)
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
