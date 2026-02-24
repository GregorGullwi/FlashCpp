// Test multiple __try blocks in a single function
// Each __try has its own scope table entry

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	int result = 0;

	// First __try block - catches access violation
	__try {
		int* p = 0;
		*p = 42;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		result = result + 10;
	}

	// Second __try block - catches divide by zero
	__try {
		int x = 1;
		int y = 0;
		int z = x / y;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		result = result + 12;
	}

	// Third __try block - no exception
	__try {
		result = result + 20;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		result = 999;  // should not execute
	}

	return result;  // 10 + 12 + 20 = 42
}
