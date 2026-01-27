// Nested exception handling test
extern "C" int printf(const char*, ...);

int test_nested_try() {
    try {
        try {
            throw 10;
        } catch (int e) {
            printf("Inner catch: %d\n", e);
            throw e + 5;  // Rethrow with modification
        }
    } catch (int e) {
        return e;  // Should return 15
    }
    return 0;
}

int test_multiple_catches() {
    try {
        throw 42;
    } catch (char c) {
        return 1;
    } catch (int i) {
        return i;  // Should match this
    } catch (...) {
        return 2;
    }
    return 0;
}

int main() {
    int result1 = test_nested_try();
    printf("test_nested_try: %d (expected 15)\n", result1);
    
    int result2 = test_multiple_catches();
    printf("test_multiple_catches: %d (expected 42)\n", result2);
    
    return 0;
}
