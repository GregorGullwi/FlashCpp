// Test: return from inside __try with __finally
// The __finally block should execute before the function returns.
// Expected: result = 10 (set in __try), __finally runs (sets global to 20),
// but return value is already captured as 10. Then we add the global (20) = 30.

int g_finally_ran = 0;

int test_return_from_try() {
	__try {
		g_finally_ran = 0;
		return 10;
	}
	__finally {
		g_finally_ran = 20;
	}
	return 0;  // Should never reach here
}

int main() {
	int result = test_return_from_try();
	// result should be 10 (the return value from __try)
	// g_finally_ran should be 20 (set by __finally before the return completed)
	return result + g_finally_ran;  // 10 + 20 = 30
}
