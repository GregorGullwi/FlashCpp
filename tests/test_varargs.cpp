// Test calling variadic functions from FlashCpp

extern "C" {
   typedef char* va_list;
   
   #include "test_varargs_helper.c"
   
	extern int printf(const char* fmt, ...);
}

int main() {
    // Test integer varargs
    int result1 = sum_ints(3, 10, 20, 30);
    if (result1 != 60) {
        return 1;
    }
    
    // Test mixed varargs (floats promoted to double)  
    double result2 = sum_mixed(3, 1.5, 2.5, 3.0);
    printf("result2 = %.10f\n", result2);
    
    // Simplified test: just check if it's 7.0
    if (result2 == 7.0) {
        printf("SUCCESS!\n");
        return 0;
    } else {
        printf("FAILED: expected 7.0\n");
        return 2;
    }
}
