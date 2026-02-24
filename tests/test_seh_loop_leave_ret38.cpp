// Test __try with __leave inside a loop
// __leave exits the __try block, continues the loop

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	int result = 0;
	for (int i = 0; i < 5; i++) {
		__try {
			result = result + 10;
			if (i >= 3) {
				__leave;  // skip the subtraction on iterations 3,4
			}
			result = result - 4;  // iterations 0,1,2: net +6 each
		}
		__except(EXCEPTION_EXECUTE_HANDLER) {
			result = 999;  // should never execute
		}
	}
	// iterations 0,1,2: +6 each = 18
	// iterations 3,4: +10 each = 20
	// total = 38... let me recalculate
	// i=0: result=0+10-4=6
	// i=1: result=6+10-4=12
	// i=2: result=12+10-4=18
	// i=3: result=18+10=28 (__leave skips -4)
	// i=4: result=28+10=38 (__leave skips -4)
	// Hmm, let me adjust the expected value
	return result;  // expect 38
}
