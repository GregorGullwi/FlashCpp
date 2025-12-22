// Test mixing variadic and non-variadic function declarations

extern "C" {
    // Non-variadic function
    int add(int a, int b);
    
    // Variadic function with one parameter
    int sum(int count, ...);
    
    // Another non-variadic function
    int multiply(int a, int b);
    
    // Variadic function with multiple parameters
    void log_error(const char* file, int line, const char* format, ...);
    
    // Non-variadic function with pointer parameter
    int strlen(const char* str);
}

// Non-variadic function definition
int add(int a, int b) {
    return a + b;
}

// Another non-variadic function definition
int multiply(int a, int b) {
    return a * b;
}

int main() {
    int x = add(5, 3);
    int y = multiply(4, 2);
    return x + y;
}

