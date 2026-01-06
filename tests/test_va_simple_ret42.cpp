// Test variadic function with simple char* va_list (System V AMD64 compatible)
// Returns: 42 (30 + 12)

typedef char* va_list;

#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) ((void)(ap = 0))

int sum2(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int a = va_arg(args, int);
    int b = va_arg(args, int);
    
    va_end(args);
    return a + b;
}

int main() {
    return sum2(2, 30, 12);  // Returns 42
}
