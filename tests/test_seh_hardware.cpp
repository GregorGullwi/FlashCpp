// Test SEH with actual hardware/Windows exceptions
// Tests access violations, divide-by-zero, etc.

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_CONTINUE_EXECUTION -1

// Windows exception codes
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094
#define EXCEPTION_ILLEGAL_INSTRUCTION 0xC000001D

// Test 1: Catch access violation (null pointer dereference)
int test_access_violation() {
    __try {
        int* null_ptr = 0;
        *null_ptr = 42;  // This will cause an access violation
        return 0;  // Should not reach here
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return 100;  // Should execute this
    }
}

// Test 2: Catch divide by zero
int test_divide_by_zero() {
    __try {
        int x = 10;
        int y = 0;
        int z = x / y;  // This will cause divide by zero
        return z;  // Should not reach here
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return 200;  // Should execute this
    }
}

// Test 3: Nested exception handling
int test_nested_exceptions() {
    int result = 0;
    __try {
        result = 1;
        __try {
            int* null_ptr = 0;
            *null_ptr = 42;  // Inner exception
            result = 2;  // Should not reach
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            result = 3;  // Should execute this
        }
        result = result + 10;  // Should execute: 3 + 10 = 13
    }
    __finally {
        result = result + 100;  // Should execute: 13 + 100 = 113
    }
    return result;  // Should return 113
}

// Test 4: Exception in __finally block (should propagate)
int test_exception_in_finally() {
    int result = 0;
    __try {
        __try {
            result = 5;
        }
        __finally {
            // This exception should propagate to outer handler
            int* null_ptr = 0;
            *null_ptr = 42;
            result = 6;  // Should not reach
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return 300;  // Should catch the exception from __finally
    }
    return result;  // Should not reach
}

// Test 5: Multiple exceptions with __leave
int test_leave_before_exception() {
    int result = 0;
    __try {
        result = 7;
        __leave;  // Exit before exception
        int* null_ptr = 0;
        *null_ptr = 42;  // Should not execute
        result = 8;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        result = 9;  // Should not execute
    }
    return result;  // Should return 7
}

// Test 6: No exception - handler should not execute
int test_no_exception() {
    int result = 0;
    __try {
        result = 50;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        result = 60;  // Should not execute
    }
    return result;  // Should return 50
}

int main() {
    int t1 = test_access_violation();      // Expected: 100
    int t2 = test_divide_by_zero();        // Expected: 200
    int t3 = test_nested_exceptions();     // Expected: 113
    int t4 = test_exception_in_finally();  // Expected: 300
    int t5 = test_leave_before_exception(); // Expected: 7
    int t6 = test_no_exception();          // Expected: 50
    
    // Return sum: 100 + 200 + 113 + 300 + 7 + 50 = 770
    return t1 + t2 + t3 + t4 + t5 + t6 == 770 ? 0 : 1;
}

