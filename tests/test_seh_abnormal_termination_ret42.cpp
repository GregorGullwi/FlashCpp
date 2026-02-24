// Test: _abnormal_termination() in __finally
// _abnormal_termination() returns 0 when __finally runs during normal control flow,
// and non-zero when it runs due to exception unwinding.

#define EXCEPTION_EXECUTE_HANDLER 1

int g_normal_count = 0;
int g_abnormal_count = 0;

int _abnormal_termination();

// Normal flow: __finally runs because the __try block completed normally
void test_normal_finally() {
	__try {
		// no exception
	}
	__finally {
		if (_abnormal_termination()) {
			g_abnormal_count++;
		} else {
			g_normal_count++;
		}
	}
}

// Exception flow: __finally runs because of an exception being unwound
// Wrapped in outer except to prevent crash
int test_exception_finally() {
	__try {
		__try {
			int* p = 0;
			*p = 1;  // Access violation
		}
		__finally {
			if (_abnormal_termination()) {
				g_abnormal_count++;
			} else {
				g_normal_count++;
			}
		}
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		// Exception caught here after __finally ran
	}
	return 0;
}

// __leave flow: __finally still runs with AbnormalTermination() == 0
void test_leave_finally() {
	__try {
		__leave;
	}
	__finally {
		if (_abnormal_termination()) {
			g_abnormal_count++;
		} else {
			g_normal_count++;
		}
	}
}

int main() {
	test_normal_finally();    // g_normal_count = 1, g_abnormal_count = 0
	test_exception_finally(); // g_normal_count = 1, g_abnormal_count = 1
	test_leave_finally();     // g_normal_count = 2, g_abnormal_count = 1

	// 2 normal + 1 abnormal = sum check, return 42 if correct
	if (g_normal_count == 2 && g_abnormal_count == 1) {
		return 42;
	}
	return 0;
}
