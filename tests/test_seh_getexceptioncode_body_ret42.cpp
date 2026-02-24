// Test: GetExceptionCode() used inside the __except body (not just in the filter expression)
// The exception code should be saved during filter evaluation and be readable in the body.

#define EXCEPTION_EXECUTE_HANDLER 1

unsigned long GetExceptionCode();

// Test 1: GetExceptionCode() in __except body after access violation
int test_code_in_body_av() {
	int* p = 0;
	unsigned long code = 0;
	__try {
		*p = 1;  // Access violation: 0xC0000005
	}
	__except(GetExceptionCode() == 0xC0000005 ? 1 : 0) {
		code = GetExceptionCode();  // Should still be 0xC0000005
	}
	return code == 0xC0000005 ? 20 : 0;
}

// Test 2: GetExceptionCode() in __except body after divide-by-zero
int test_code_in_body_divzero() {
	volatile int x = 5;
	volatile int y = 0;
	unsigned long code = 0;
	__try {
		x = x / y;  // Integer divide by zero: 0xC0000094
	}
	__except(GetExceptionCode() == 0xC0000094 ? 1 : 0) {
		code = GetExceptionCode();  // Should still be 0xC0000094
	}
	return code == 0xC0000094 ? 22 : 0;
}

int main() {
	int r1 = test_code_in_body_av();      // 20
	int r2 = test_code_in_body_divzero(); // 22
	return r1 + r2;  // 20 + 22 = 42
}
