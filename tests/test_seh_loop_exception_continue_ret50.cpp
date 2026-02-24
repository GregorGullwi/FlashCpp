// Test __try/__except inside a loop where exception happens every iteration
// and code continues executing after the except block in the loop body

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	int result = 0;
	for (int i = 0; i < 5; i++) {
		int caught = 0;
		__try {
			int* p = 0;
			*p = 42;
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			caught = 1;
		}
		// Code after __try/__except but still in loop
		if (caught) {
			result = result + 10;
		}
	}
	return result;  // expect 50
}
