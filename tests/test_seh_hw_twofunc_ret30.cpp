// Test SEH with hardware exceptions in two functions
// Expected return: 30

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

int main() {
	int r1 = test_av();  // 10
	__try {
		int* p = 0;
		*p = 42;
		return 0;
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		return r1 + 20;  // 10 + 20 = 30
	}
}
