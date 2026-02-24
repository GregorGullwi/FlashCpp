// Test: GetExceptionCode() intrinsic inside __except filter
// Triggers an access violation (0xC0000005) and checks the exception code in the filter.
// Expected: filter evaluates GetExceptionCode() == 0xC0000005, returns EXCEPTION_EXECUTE_HANDLER (1),
// __except body executes and returns 1.

// Declare the intrinsic (normally provided by <excpt.h>)
unsigned long GetExceptionCode();

int main() {
	int* p = 0;
	__try {
		*p = 42;  // Access violation
	}
	__except(GetExceptionCode() == 0xC0000005 ? 1 : -1) {
		return 1;  // Exception was caught with correct code
	}
	return 0;  // Should not reach here
}
