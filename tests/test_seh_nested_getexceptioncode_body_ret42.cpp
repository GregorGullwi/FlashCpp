// Test: GetExceptionCode() in __except body with nested __try/__except
// The outer except body uses GetExceptionCode() after an inner __try/__except
// completes - verifies the saved exception code is not clobbered by inner nesting.

#define EXCEPTION_EXECUTE_HANDLER 1

unsigned long GetExceptionCode();

// Test: outer __except body has GetExceptionCode() after inner __try/__except completes
int test_nested_except_body() {
	int* p = 0;
	int inner_ran = 0;
	unsigned long outer_code = 0;
	__try {
		*p = 1;  // Access violation: 0xC0000005
	}
	__except(GetExceptionCode() == 0xC0000005 ? 1 : 0) {
		// Inner __try/__except that should NOT affect outer's GetExceptionCode()
		__try {
			// no exception
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			inner_ran = 1;  // should not execute
		}
		// After inner try/except, outer GetExceptionCode() should still work
		outer_code = GetExceptionCode();
	}
	if (inner_ran == 0 && outer_code == 0xC0000005) {
		return 42;
	}
	return 0;
}

int main() {
	return test_nested_except_body();
}
