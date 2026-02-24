// Stress test: non-constant __except filter + nested __finally during unwind
// Expected flow:
// 1) Inner __try triggers access violation
// 2) Inner __finally executes during unwind (result += 40)
// 3) Outer non-constant filter evaluates true (p == 0)
// 4) Outer __except runs (result += 2)
// Final result = 42

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0

int main() {
	int result = 0;
	int* p = 0;

	__try {
		__try {
			*p = 123; // access violation
			result = 99;
		}
		__finally {
			result += 40;
		}

		result = 99;
	}
	__except(p == 0 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
		result += 2;
	}

	return result;
}
