// Test break inside __try with __finally in a loop
// __finally MUST execute even when break is used

int main() {
	int result = 0;
	for (int i = 0; i < 10; i++) {
		__try {
			result = result + 10;
			if (i == 1) {
				break;  // break on second iteration
			}
		} __finally {
			result = result + 5;	 // must run even on break
		}
	}
	return result;  // expect 30
}
