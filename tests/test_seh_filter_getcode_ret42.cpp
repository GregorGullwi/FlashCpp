// Test: GetExceptionCode() in filter with different exception types
// Tests that GetExceptionCode() correctly identifies different exception codes.
// Access violation = 0xC0000005, Integer divide by zero = 0xC0000094
// Expected: 42 (sum of results from two __try/__except blocks)

unsigned long GetExceptionCode();

int test_access_violation() {
	int* p = 0;
	__try {
		*p = 42;
	}
	__except(GetExceptionCode() == 0xC0000005 ? 1 : -1) {
		return 20;
	}
	return 0;
}

int test_divide_by_zero() {
	volatile int x = 1;
	volatile int y = 0;
	__try {
		x = x / y;
	}
	__except(GetExceptionCode() == 0xC0000094 ? 1 : -1) {
		return 22;
	}
	return 0;
}

int main() {
	return test_access_violation() + test_divide_by_zero();  // 20 + 22 = 42
}
