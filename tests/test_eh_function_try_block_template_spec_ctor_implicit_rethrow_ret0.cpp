// Regression test: C++20 [except.handle]/15 implicit rethrow in constructor
// function-try-blocks inside full template specializations.
//
// Bug: src/Parser_Templates_Class.cpp calls parse_function_body() without
// passing is_ctor_or_dtor=true at two sites:
//
//   1. Line ~1825 — immediate constructor body parse inside template<> struct.
//      parse_function_body() is called without true, so TryStatementNode does
//      not get is_ctor_dtor_function_try_ set.  The IR generator skips the
//      implicit Rethrow, and the exception is silently swallowed.
//
//   2. Line ~2409 — delayed body parse loop for member functions/destructors
//      of full specializations.  Same issue: parse_function_body() called
//      without propagating is_constructor/is_destructor.
//
// Both paths are exercised below.  Without the fix, main() returns non-zero.

int g_log = 0;

// --- Primary template (needed so template<> specialization is valid) ---
template<typename T>
struct Wrapper {
	T value;
	Wrapper(T v) : value(v) {}
	~Wrapper() {}
};

// --- Full specialization: exercises path 1 (immediate ctor body parse) ---
// The constructor uses a function-try-block.  Per C++20 [except.handle]/15,
// falling off the end of the catch handler must implicitly rethrow.
template<>
struct Wrapper<int> {
	int value;

	// Path 1: constructor body parsed immediately at line ~1825
	Wrapper(int v)
	try : value(v) {
		if (v == 0) throw 42;
	} catch (int e) {
		g_log = e;
		// Falls off end → implicit rethrow required
	}

	// Path 2: destructor body deferred, parsed at line ~2409
	~Wrapper()
	try {
		if (value == -1) throw 99;
	} catch (int e) {
		g_log = e;
		// Falls off end → implicit rethrow required
	}
};

int main() {
	// ---- Path 1: constructor implicit rethrow ----

	// Exception path: catch handler runs, then exception must propagate
	g_log = 0;
	try {
		Wrapper<int> w(0);
		return 1;  // should not be reached
	} catch (int e) {
		if (e != 42) return 2;
		if (g_log != 42) return 3;
	}

	// Normal path: no exception, constructor succeeds
	g_log = 0;
	{
		Wrapper<int> w(5);
		if (w.value != 5) return 4;
	}
	if (g_log != 0) return 5;

	// ---- Path 2: destructor implicit rethrow ----

	// Destructor exception path: catch handler runs, then exception must propagate
	g_log = 0;
	try {
		Wrapper<int> w(10);
		w.value = -1;  // trigger throw in destructor
		// destructor runs at end of scope
	} catch (int e) {
		if (e != 99) return 6;
		if (g_log != 99) return 7;
	}

	return 0;
}
