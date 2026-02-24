// Test __try with break inside a loop
// break should exit the loop, __finally should still run

int main() {
	int result = 0;
	for (int i = 0; i < 5; i++) {
		__try {
			result = result + 10;
			if (i == 1) {
				break;  // break on second iteration
			}
		}
		__finally {
			result = result + 0;  // just to have a finally
		}
	}
	// i=0: result = 0+10 = 10
	// i=1: result = 10+10 = 20, then break
	return result;  // expect 20
}
