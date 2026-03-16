// Regression test: C++20 [except.handle]/15 - constructor function-try-block
// implicit rethrow. If control reaches the end of a catch handler in a
// constructor function-try-block, the current exception is implicitly rethrown.

int g_log = 0;

struct Foo {
	int x;
	// Constructor function-try-block: exception caught, g_log updated, then rethrown
	Foo(int v)
	try : x(v) {
		if (v == 0) throw 42;
	} catch (int e) {
		g_log = e;
		// Falls off end: C++20 [except.handle]/15 requires implicit rethrow here
	}
};

struct Bar {
	int x;
	// Normal path: no exception thrown - catch never entered, g_log stays 0
	Bar(int v)
	try : x(v) {
	} catch (...) {
		g_log = -1;
	}
};

int main() {
	// Constructor function-try-block: exception caught then implicitly rethrown
	try {
		Foo f(0);
		return 1;  // should not be reached
	} catch (int e) {
		if (e != 42) return 2;
		if (g_log != 42) return 3;
	}

	// Verify normal path: no exception means catch block not entered
	g_log = 0;
	{
		Foo f2(5);
		if (f2.x != 5) return 4;
	}
	if (g_log != 0) return 5;

	// Verify constructor try-block normal path: no rethrow when no exception
	g_log = 0;
	{
		Bar b(7);
		if (b.x != 7) return 6;
	}
	if (g_log != 0) return 7;

	return 0;
}
