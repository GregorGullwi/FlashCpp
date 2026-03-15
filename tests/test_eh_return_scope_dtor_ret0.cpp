// Regression test: local variable destructors must be called before a
// return statement, for variables in all scopes (function scope, loop scope,
// try-block scope, etc.).

int g_dtor_count = 0;

struct Guard {
	~Guard() { g_dtor_count++; }
};

// Test 1: return from a function with a function-scope local.
// The return VALUE must be the pre-destruction value; the destructor runs after.
int test_return_value_before_dtor() {
	Guard g;
	// g_dtor_count is 0 here; return it before the destructor increments it
	return g_dtor_count;  // should return 0
}

// Test 2: return from inside a try block (inner + outer scope).
int test_return_from_try() {
	Guard outer;
	try {
		Guard inner;
		return g_dtor_count;  // should return 0; both inner and outer destroyed after
	} catch (...) {}
	return -1;  // unreachable
}

// Test 3: return from inside a loop (loop-scope variable).
int test_return_from_loop() {
	int prev = g_dtor_count;
	for (int i = 0; i < 3; i++) {
		Guard loop_g;
		if (i == 1) return (g_dtor_count - prev);  // Guard for i=0 destroyed at loop-body end, Guard for i=1 destroyed before return
	}
	return -1;  // unreachable
}

int main() {
	g_dtor_count = 0;

	// Test 1: function-scope Guard
	int ret = test_return_value_before_dtor();
	if (ret != 0) return 1;           // return value must be 0 (pre-dtor)
	if (g_dtor_count != 1) return 2;  // Guard must have been destroyed

	g_dtor_count = 0;

	// Test 2: return from inside try block
	ret = test_return_from_try();
	if (ret != 0) return 3;           // return value must be 0 (pre-dtor)
	if (g_dtor_count != 2) return 4;  // both inner and outer must be destroyed

	g_dtor_count = 0;

	// Test 3: return from inside a loop
	ret = test_return_from_loop();
	if (ret != 1) return 5;           // 1 dtor for i=0 (before return at i=1)
	if (g_dtor_count != 2) return 6;  // loop_g for i=0 (normal exit) + i=1 (before return)

	return 0;
}
