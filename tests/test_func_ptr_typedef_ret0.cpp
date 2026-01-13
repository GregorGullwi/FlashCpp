// Test function pointer typedef parsing
// This pattern is used in <new> header: typedef void (*new_handler)();

typedef void (*new_handler)();

// Test with parameters
typedef int (*func_ptr_with_params)(int, float);

// Test with const and multiple pointers
typedef const char* (*string_provider)();

int main() {
    // The typedef should compile without error
    // We can't actually call these pointers without proper implementations
    // But the parsing should work
    return 0;
}
