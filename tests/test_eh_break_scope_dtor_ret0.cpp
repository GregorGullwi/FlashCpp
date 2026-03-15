// Regression test: local variable destructors must be called when break
// exits a loop body, even when inside a try block.

int g_dtor_count = 0;

struct Guard {
	~Guard() { g_dtor_count++; }
};

// Test 1: break from plain loop — destructor must run for each constructed Guard.
int test_break_plain_loop() {
	int prev = g_dtor_count;
	for (int i = 0; i < 3; i++) {
		Guard g;
		if (i == 1) break;  // breaks after i=0 and i=1 iterations
	}
	// Guard was constructed for i=0 (destructor on normal loop-body exit) and
	// i=1 (destructor called before break); 2 destructor calls expected.
	return (g_dtor_count - prev) == 2 ? 0 : (g_dtor_count - prev);
}

// Test 2: break from inside a try block — destructor for loop-scope
// Guard must still be called before the jump.
int test_break_from_try_block() {
	int prev = g_dtor_count;
	for (int i = 0; i < 3; i++) {
		Guard g;
		try {
			if (i == 1) break;
		} catch (...) {
			return -1;
		}
	}
	return (g_dtor_count - prev) == 2 ? 0 : (g_dtor_count - prev);
}

// Test 3: nested loops — break from inner loop calls inner-scope dtor only.
int test_break_nested_loops() {
	int prev = g_dtor_count;
	for (int i = 0; i < 2; i++) {
		Guard outer_g;  // outer Guard, lives for full outer iteration
		for (int j = 0; j < 3; j++) {
			Guard inner_g;
			if (j == 1) break;  // inner Guard for j=0 (normal) and j=1 (break)
		}
		// Each outer iteration: outer_g + 2 inner_g = 3 dtors
	}
	// 2 outer iterations × (1 outer + 2 inner) = 6 dtors
	return (g_dtor_count - prev) == 6 ? 0 : (g_dtor_count - prev);
}

int main() {
	if (test_break_plain_loop() != 0) return 1;
	if (test_break_from_try_block() != 0) return 2;
	if (test_break_nested_loops() != 0) return 3;
	return 0;
}
