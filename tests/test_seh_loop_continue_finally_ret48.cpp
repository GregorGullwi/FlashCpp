// Test continue inside __try with __finally in a loop
// __finally MUST execute even when continue is used

int main() {
	int result = 0;
	for (int i = 0; i < 5; i++) {
		__try {
			result = result + 3;
			if (i == 2) {
				continue;  // skip rest on iteration 2
			}
			result = result + 2;  // skipped on i=2
		}
		__finally {
			result = result + 5;  // must always run
		}
	}
	// i=0: +3+2+5 = 10
	// i=1: 10+3+2+5 = 20
	// i=2: 20+3+5 = 28 (continue skips +2, but __finally runs)
	// i=3: 28+3+2+5 = 38
	// i=4: 38+3+2+5 = 48
	// total = 48... not 50
	return result;  // expect 48
}
