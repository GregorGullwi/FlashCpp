// Test variadic function with float/double arguments (System V AMD64)
// Tests fp_offset path in va_arg implementation
// Returns: 0 on success

typedef char* va_list;

#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) ((void)(ap = 0))

// Sum doubles - test the fp_offset path
double sum_doubles(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double total = 0.0;
    for (int i = 0; i < count; i++) {
        double val = va_arg(args, double);
        total = total + val;
    }
    
    va_end(args);
    return total;
}

// Sum many doubles - tests overflow to stack
double sum_many_doubles(int count, ...) {
    va_list args;
    va_start(args, count);
    
    double total = 0.0;
    for (int i = 0; i < count; i++) {
        double val = va_arg(args, double);
        total = total + val;
    }
    
    va_end(args);
    return total;
}

int main() {
    // Test 1: Basic float varargs (3 args, all in XMM regs)
    // Sum: 1.0 + 2.0 + 3.0 = 6.0
    double result1 = sum_doubles(3, 1.0, 2.0, 3.0);
    if (result1 != 6.0) {
        return 1;
    }
    
    // Test 2: Float varargs with overflow (10 args, 8 in XMM regs, 2 on stack)
    // Sum: 1+2+3+4+5+6+7+8+9+10 = 55.0
    double result2 = sum_many_doubles(10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0);
    if (result2 != 55.0) {
        return 2;
    }
    
    return 0;
}
