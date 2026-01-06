// Test variadic function with mixed int and float arguments
// Tests that gp_offset and fp_offset work independently
// Returns: 0 on success

typedef char* va_list;

#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) ((void)(ap = 0))

// Sum alternating int and double values
// Pattern: int, double, int, double, ...
double sum_alternating(int pairs, ...) {
    va_list args;
    va_start(args, pairs);
    
    double total = 0.0;
    for (int i = 0; i < pairs; i++) {
        int ival = va_arg(args, int);
        double dval = va_arg(args, double);
        total = total + (double)ival + dval;
    }
    
    va_end(args);
    return total;
}

int main() {
    // Test with 3 pairs: (1, 0.5), (2, 1.5), (3, 2.5)
    // Sum: 1+0.5 + 2+1.5 + 3+2.5 = 10.5
    double result = sum_alternating(3, 1, 0.5, 2, 1.5, 3, 2.5);
    
    // Return 0 if result is 10.5
    if (result == 10.5) {
        return 0;
    }
    return 1;
}
