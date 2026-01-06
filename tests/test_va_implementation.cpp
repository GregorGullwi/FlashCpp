// Test implementing a variadic function using va_start, va_arg, va_end
// This tests the actual implementation of variadic argument handling
// Uses cross-platform __builtin_va_* intrinsics (works on both Linux and Windows)

// va_list type - simple char* on x64 (System V AMD64 ABI compatible)
typedef char* va_list;

// va_start macro - uses compiler intrinsic
#define va_start(ap, param) __builtin_va_start(ap, param)

// va_arg macro - uses compiler intrinsic
#define va_arg(ap, type) __builtin_va_arg(ap, type)

// va_end macro - cleans up va_list
#define va_end(ap) ((void)(ap = 0))

// Simple variadic function that sums integers
// First parameter is the count of variadic arguments
int sum_ints(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int total = 0;
    for (int i = 0; i < count; i++) {
        int value = va_arg(args, int);
        total += value;
    }
    
    va_end(args);
    return total;
}

int main() {
    // Test: sum_ints(3, 10, 20, 30) should return 60
    int result = sum_ints(3, 10, 20, 30);
    return result;  // Should return 60
}

