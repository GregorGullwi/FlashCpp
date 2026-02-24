// Test nested __try blocks inside a loop

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	int result = 0;
	for (int i = 0; i < 3; i++) {
		__try {
			__try {
				if (i == 1) {
					int* p = 0;
					*p = 42;  // fault on i=1
				}
				result = result + 2;  // i=0: +2, i=2: +2
			}
			__except(EXCEPTION_EXECUTE_HANDLER) {
				result = result + 5;  // i=1: +5
			}
			result = result + 3;  // always: i=0: +3, i=1: +3, i=2: +3
		}
		__finally {
			result = result + 4;  // always: +4 each
		}
	}
	// i=0: result = 0+2+3+4 = 9
	// i=1: result = 9+5+3+4 = 21
	// i=2: result = 21+2+3+4 = 30
	// Hmm 30 not 42, let me adjust
	return result;  // expect 30
}
