// Test basic Windows SEH (Structured Exception Handling) support
// This tests the parser's ability to handle __try/__except/__finally/__leave

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION -1

// Test 1: Basic __try/__except
int test_try_except() {
    __try {
        int x = 42;
        return x;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Test 2: Basic __try/__finally
int test_try_finally() {
    int result = 0;
    __try {
        result = 100;
    }
    __finally {
        result = result + 1;
    }
    return result;
}

// Test 3: __try/__except with filter expression
int test_filter_expression(int* ptr) {
    __try {
        int value = *ptr;
        return value;
    }
    __except(ptr == 0 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return -1;
    }
}

// Test 4: __leave statement
int test_leave() {
    int result = 0;
    __try {
        result = 10;
        __leave;
        result = 20;  // Should not execute
    }
    __finally {
        result = result + 5;
    }
    return result;  // Should return 15
}

// Test 5: Nested __try blocks
int test_nested() {
    __try {
        __try {
            return 1;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            return 2;
        }
    }
    __finally {
        int cleanup = 0;
    }
}

int main() {
    int result = 0;

    // Test 1: Basic __try/__except - should return 42
    result += test_try_except();  // result = 42

    // Test 2: Basic __try/__finally - should return 101
    result += test_try_finally();  // result = 42 + 101 = 143

    // Test 3: __try/__except with filter - should return -1 (null pointer)
    result += test_filter_expression(0);  // result = 143 + (-1) = 142

    // Test 4: __leave statement - should return 15
    result += test_leave();  // result = 142 + 15 = 157

    // Test 5: Nested __try blocks - should return 1
    result += test_nested();  // result = 157 + 1 = 158

    // Expected final result: 42 + 101 + (-1) + 15 + 1 = 158
    return result;
}

