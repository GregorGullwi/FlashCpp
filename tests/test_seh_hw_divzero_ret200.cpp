// Test SEH: catch divide-by-zero hardware exception
// Expected return: 200

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	__try {
		int x = 10;
		int y = 0;
		int z = x / y;  // Divide by zero
		return z;  // Should not reach here
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		return 200;
	}
}
