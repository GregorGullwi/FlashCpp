// Test SEH with hardware exceptions across multiple functions
// Expected return: 44

#define EXCEPTION_EXECUTE_HANDLER 1

int test_av() {
	__try {
		int* p = 0;
		*p = 42;
		return 0;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		return 10;
	}
}

int test_divzero() {
	__try {
		int a = 1;
		int b = 0;
		int c = a / b;
		return c;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		return 20;
	}
}

int test_no_exception() {
	__try {
		int x = 7;
		return x;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		return 99;
	}
}

int test_leave() {
	int result = 0;
	__try {
		result = 7;
		__leave;
		result = 99;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		result = 88;
	}
	return result;
}

int main() {
	int r1 = test_av();         // 10
	int r2 = test_divzero();    // 20
	int r3 = test_no_exception(); // 7
	int r4 = test_leave();      // 7
	return r1 + r2 + r3 + r4;  // 10 + 20 + 7 + 7 = 44
}
