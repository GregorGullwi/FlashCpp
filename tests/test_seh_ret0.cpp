// Comprehensive SEH (Structured Exception Handling) tests for FlashCpp
// Tests __try, __except, __finally, and __leave keywords

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION -1

// Test 1: Basic __try/__except with constant filter
int test_basic_except() {
    __try {
        int x = 42;
        return x;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Test 2: Basic __try/__finally
int test_basic_finally() {
    int result = 0;
    __try {
        result = 100;
    }
    __finally {
        result = result + 1;
    }
    return result;  // Should return 101
}

// Test 3: __leave statement with __finally
int test_leave_finally() {
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

// Test 4: __leave statement with __except
int test_leave_except() {
    int result = 0;
    __try {
        result = 30;
        __leave;
        result = 40;  // Should not execute
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        result = 50;  // Should not execute
    }
    return result;  // Should return 30
}

// Test 5: Nested __try blocks
int test_nested_try() {
    int result = 0;
    __try {
        result = 1;
        __try {
            result = 2;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            result = 3;
        }
        result = result + 10;
    }
    __finally {
        result = result + 100;
    }
    return result;  // Should return 112 (2 + 10 + 100)
}

// Test 6: Multiple __except handlers (nested)
int test_multiple_except() {
    int result = 0;
    __try {
        __try {
            result = 5;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            result = 6;
        }
        result = result + 1;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        result = 7;
    }
    return result;  // Should return 6 (5 + 1)
}

// Test 7: __try/__finally with early return
int test_finally_with_return() {
    int cleanup = 0;
    __try {
        return 99;
    }
    __finally {
        cleanup = 1;  // Should still execute
    }
    return 0;  // Should not reach here
}

// Test 8: __try/__except with early return
int test_except_with_return() {
    __try {
        return 88;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return 77;  // Should not execute
    }
    return 0;  // Should not reach here
}

int main() {
    // Test 1: Should return 42
    int t1 = test_basic_except();

    // Test 2: Should return 101
    int t2 = test_basic_finally();

    // Test 3: Should return 15
    int t3 = test_leave_finally();

    // Test 4: Should return 30
    int t4 = test_leave_except();

    // Test 5: Should return 112
    int t5 = test_nested_try();

    // Test 6: Should return 6
    int t6 = test_multiple_except();

    // Test 7: Should return 99
    int t7 = test_finally_with_return();

    // Test 8: Should return 88
    int t8 = test_except_with_return();

    // Return sum to verify all tests ran
    return (t1 + t2 + t3 + t4 + t5 + t6 + t7 + t8) == 493 ? 0 : 1;
}

