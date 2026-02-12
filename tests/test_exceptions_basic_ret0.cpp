// Basic exception handling test - try/catch/throw

int test_simple_throw_catch() {
    try {
        throw 42;
    } catch (int e) {
        return e;  // Should return 42
    }
    return 0;  // Should not reach here
}

int test_no_exception() {
    try {
        return 100;
    } catch (int e) {
        return e;
    }
    return 0;  // Should not reach here
}

int main() {
    int result1 = test_simple_throw_catch();
    
    int result2 = test_no_exception();
    
    // Test catching different types
	int result3 = 0;
    try {
        throw 123;
    } catch (int x) {
		result3 = x;
    }
    
    return result1 + result2 + result3 - 42 - 100 - 123;
}
