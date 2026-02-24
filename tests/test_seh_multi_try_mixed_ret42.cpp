// Test multiple __try blocks mixing __except and __finally in one function

#define EXCEPTION_EXECUTE_HANDLER 1

int main() {
	int result = 0;

	// First: __try/__except with exception
	__try {
		int* p = 0;
		*p = 42;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		result = result + 10;
	}

	// Second: __try/__finally, no exception
	__try {
		result = result + 7;
	}
	__finally {
		result = result + 5;  // 22
	}

	// Third: __try/__except, no exception
	__try {
		result = result + 20;  // 42
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		result = 999;  // should not execute
	}

	return result;  // 10 + 7 + 5 + 20 = 42
}
