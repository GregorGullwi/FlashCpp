// Test noexcept specifier on functions
#include <cstdio>

int safe_function() noexcept {
    return 42;
}

int conditional_noexcept(bool b) noexcept(false) {
    if (b) {
        return 100;
    }
    throw 1;
}

int main() {
    int result = safe_function();
    printf("safe_function: %d (expected 42)\n", result);
    
    int result2 = conditional_noexcept(true);
    printf("conditional_noexcept(true): %d (expected 100)\n", result2);
    
    try {
        conditional_noexcept(false);
    } catch (int e) {
        printf("Caught exception: %d (expected 1)\n", e);
    }
    
    return 0;
}
