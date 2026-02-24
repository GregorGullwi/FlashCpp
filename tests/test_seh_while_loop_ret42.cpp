// Test __try/__except inside a while loop

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	int result = 0;
	int i = 0;
	while (i < 6) {
		__try {
			result = result + 7;
			if (i == 3) {
				int* p = 0;
				*p = 42;  // fault on iteration 3
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			// On fault: don't add 7 again, it was already added before fault
			// but the +7 above DID execute before the fault
		}
		i = i + 1;
	}
	// All 6 iterations add 7: result = 42
	return result;  // expect 42
}
