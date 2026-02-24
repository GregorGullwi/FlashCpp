// Test __try/__finally inside a loop
// __finally should execute every iteration

int main() {
	int result = 0;
	for (int i = 0; i < 5; i++) {
		__try {
			result = result + 3;
		}
		__finally {
			result = result + 7;  // +10 per iteration total
		}
	}
	return result;  // expect 50
}
