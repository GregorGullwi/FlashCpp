// Regression test: local variable destructors must be called when goto
// exits one or more scopes, for both forward gotos and backward gotos.

int g_dtor_count = 0;

struct Guard {
	int id;
	Guard(int i) : id(i) {}
	~Guard() { g_dtor_count += id; }
};

// Test 1: forward goto exits a block scope that contains a Guard.
// The Guard must be destroyed before execution continues at the label.
int test_forward_goto_exits_block() {
	int prev = g_dtor_count;
	{
		Guard g(1);
		try {
			goto fwd_label;
		} catch (...) {
			return -1;
		}
	}
fwd_label:
	return (g_dtor_count - prev) == 1 ? 0 : (g_dtor_count - prev);
}

// Test 2: backward goto exits an inner block; inner Guard must be destroyed
// on each back-edge, outer Guard destroyed once on normal exit.
int test_backward_goto_exits_inner_block() {
	int prev = g_dtor_count;
	int count = 0;
	{
		Guard outer(10);
	retry:
		if (count >= 2) goto done;
		{
			Guard inner(1);
			count++;
			goto retry;  // inner Guard must be destroyed before jumping back
		}
	done:;
	}
	// outer (10) + 2 × inner (1 each) = 12
	return (g_dtor_count - prev) == 12 ? 0 : (g_dtor_count - prev);
}

int main() {
	g_dtor_count = 0;
	if (test_forward_goto_exits_block() != 0) return 1;

	g_dtor_count = 0;
	if (test_backward_goto_exits_inner_block() != 0) return 2;

	return 0;
}
