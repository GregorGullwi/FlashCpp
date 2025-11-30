// Test noexcept specifier on functions
// Using extern "C" printf declaration instead of <cstdio>
extern "C" int printf(const char*, ...);

int safe_function() noexcept {
    return 42;
}

int another_safe() noexcept(true) {
    return 100;
}

int main() {
    int result = safe_function();
    printf("safe_function: %d (expected 42)\n", result);
    
    int result2 = another_safe();
    printf("another_safe: %d (expected 100)\n", result2);
    
    return result + result2 - (42 + 100);  // Should return 0
}
