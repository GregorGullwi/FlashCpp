// Test implementing a variadic function using va_start, va_arg, va_end
// This tests the actual implementation of variadic argument handling

// va_list type and macros
typedef char* va_list;

// __va_start is a compiler intrinsic that initializes va_list
// On x64 Windows, it sets va_list to point to the first variadic argument
extern "C" void __cdecl __va_start(va_list*, ...);

// va_arg macro - extracts the next argument
// On x64 Windows, this advances the pointer by 8 bytes and dereferences
#define va_arg(ap, type) (*(type*)((ap += 8) - 8))

// va_end macro - cleans up va_list
#define va_end(ap) (ap = (va_list)0)

// Simple variadic function that sums integers
// First parameter is the count of variadic arguments
int sum_ints(int count, ...) {
    va_list args;
    __va_start(&args, count);
    
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

