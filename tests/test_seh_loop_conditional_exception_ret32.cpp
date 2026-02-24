// Test __try/__except in loop where only some iterations fault

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	int result = 0;
	for (int i = 0; i < 5; i++) {
		__try {
			if (i == 1 || i == 3 || i == 4) {
				int* p = 0;
				*p = 42;  // fault on iterations 1, 3, 4
			}
			result = result + 1;  // no-fault iterations: 0, 2
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			result = result + 10;  // fault iterations: 1, 3, 4
		}
	}
	// i=0: no fault, result = 0+1 = 1
	// i=1: fault, result = 1+10 = 11
	// i=2: no fault, result = 11+1 = 12
	// i=3: fault, result = 12+10 = 22
	// i=4: fault, result = 22+10 = 32
	// Hmm 32 not 30. Let me fix.
	return result;  // expect 32
}
