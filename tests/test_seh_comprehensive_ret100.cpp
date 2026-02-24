// Comprehensive SEH test: exercises GetExceptionCode() in body,
// _abnormal_termination() in finally, and interactions between them.

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005
#define EXCEPTION_INT_DIVIDE_BY_ZERO 0xC0000094

unsigned long GetExceptionCode();
int _abnormal_termination();

int g_score = 0;

// Test 1: GetExceptionCode() both in filter and body returns same value (+20)
void test_code_consistent() {
	int* p = 0;
	unsigned long filter_code = 0;
	unsigned long body_code = 0;
	__try {
		*p = 1;
	}
	__except((filter_code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
		body_code = GetExceptionCode();
	}
	if (filter_code == EXCEPTION_ACCESS_VIOLATION && body_code == EXCEPTION_ACCESS_VIOLATION) {
		g_score += 20;
	}
}

// Test 2: _abnormal_termination() = 0 on normal flow, non-zero on exception flow (+30)
void test_abnormal_termination() {
	int normal_ok = 0;
	int exception_ok = 0;

	// Normal flow
	__try {
		// nothing
	}
	__finally {
		if (!_abnormal_termination()) normal_ok = 1;
	}

	// Exception flow
	__try {
		__try {
			int* p = 0;
			*p = 1;  // access violation
		}
		__finally {
			if (_abnormal_termination()) exception_ok = 1;
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		// caught
	}

	if (normal_ok && exception_ok) g_score += 30;
}

// Test 3: GetExceptionCode() in nested except body (+50)
void test_nested_code_in_body() {
	int* p = 0;
	unsigned long outer_code = 0;
	unsigned long inner_code = 0;
	__try {
		*p = 1;  // access violation
	}
	__except(GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? 1 : 0) {
		outer_code = GetExceptionCode();
		// inner try/except should not interfere with outer's saved code
		__try {
			volatile int x = 1, y = 0;
			x = x / y;  // divide by zero
		}
		__except(GetExceptionCode() == EXCEPTION_INT_DIVIDE_BY_ZERO ? 1 : 0) {
			inner_code = GetExceptionCode();
		}
		// outer_code still accessible after inner nested try/except
		if (GetExceptionCode() != outer_code) outer_code = 0; // should still match
	}
	if (outer_code == EXCEPTION_ACCESS_VIOLATION && inner_code == EXCEPTION_INT_DIVIDE_BY_ZERO) {
		g_score += 50;
	}
}

int main() {
	test_code_consistent();       // +20
	test_abnormal_termination();  // +30
	test_nested_code_in_body();   // +50
	return g_score;               // 100
}
