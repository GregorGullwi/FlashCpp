// Regression test: local variable destructors must be called when continue
// restarts a loop iteration, even when inside a try block.

int g_dtor_count = 0;

struct Guard {
	~Guard() { g_dtor_count++; }
};

// Test 1: continue in plain loop — one Guard per iteration (i=0,1,2), each
// destroyed either before `continue` (i=1) or at the end of the loop body.
// Total: 3 destructor calls expected.
int test_continue_plain_loop() {
	int prev = g_dtor_count;
	for (int i = 0; i < 3; i++) {
		Guard g;
		if (i == 1) continue;
	}
	return (g_dtor_count - prev) == 3 ? 0 : (g_dtor_count - prev);
}

// Test 2: continue from inside a try block.
int test_continue_from_try_block() {
	int prev = g_dtor_count;
	for (int i = 0; i < 3; i++) {
		Guard g;
		try {
			if (i == 1) continue;
		} catch (...) {
			return -1;
		}
	}
	return (g_dtor_count - prev) == 3 ? 0 : (g_dtor_count - prev);
}

int main() {
	if (test_continue_plain_loop() != 0) return 1;
	if (test_continue_from_try_block() != 0) return 2;
	return 0;
}
