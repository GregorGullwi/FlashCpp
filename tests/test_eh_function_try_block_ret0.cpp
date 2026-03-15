// Regression test: function-level try blocks (function-try-block, C++ [dcl.fct.def.general]).
// A function definition may use 'try' before its body; the result is equivalent to
// wrapping the entire function body in a try statement.

int g_cleanup = 0;

struct Guard {
	~Guard() { g_cleanup++; }
};

// Free function: function-try-block
int safe_divide(int a, int b)
try {
	if (b == 0) throw 42;
	return a / b;
} catch (int e) {
	return -e;
}

// Member function: inline function-try-block
struct Calculator {
	int value;
	Calculator() : value(0) {}

	void set(int v)
	try {
		if (v < 0) throw v;
		value = v;
	} catch (int e) {
		value = -e;
	}
};

// Stack unwinding still happens inside function-try-block
int unwinding_test()
try {
	Guard g;
	throw 99;
} catch (int) {
	return g_cleanup;  // Guard should have been destroyed before catch runs
}

int main() {
	// Free function: normal path
	if (safe_divide(10, 2) != 5) return 1;

	// Free function: exception path
	if (safe_divide(10, 0) != -42) return 2;

	// Inline member function: normal path
	Calculator c;
	c.set(7);
	if (c.value != 7) return 3;

	// Inline member function: exception path
	c.set(-3);
	if (c.value != 3) return 4;

	// Stack unwinding in function-try-block
	int r = unwinding_test();
	if (r != 1) return 5;

	return 0;
}
