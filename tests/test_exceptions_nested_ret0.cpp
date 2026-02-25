// Nested exception handling test

int test_nested_try() {
    try {
        try {
            throw 10;
        } catch (int e) {
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
    int result2 = test_multiple_catches();
    
    return result1 + result2 == 42 + 15 ? 0 : 1;
}
