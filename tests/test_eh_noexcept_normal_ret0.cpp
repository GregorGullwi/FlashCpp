// Test that noexcept functions work normally when they don't throw.
// Also test that noexcept can contain inner try/catch without issue.

int g_x = 0;

int safe_add(int a, int b) noexcept {
	return a + b;
}

int safe_with_inner_catch(int x) noexcept {
	// A noexcept function CAN contain try/catch internally.
	// Exceptions caught internally are fine; only uncaught exceptions terminate.
	try {
		if (x < 0)
			throw -1;
		return x * 2;
	} catch (int e) {
		return e;
	}
}

int main() {
	g_x = safe_add(20, 22);
	if (g_x != 42)
		return 1;

	int r1 = safe_with_inner_catch(10);	// → 20
	if (r1 != 20)
		return 2;

	int r2 = safe_with_inner_catch(-5);	// → -1
	if (r2 != -1)
		return 3;

	return 0;
}
