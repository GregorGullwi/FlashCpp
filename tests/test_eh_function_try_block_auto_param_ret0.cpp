// Regression test: function-try-block in C++20 abbreviated function templates.
// 'void f(auto x) try { ... } catch(...) { ... }' implicitly creates a template function.

int g_caught = 0;

// Abbreviated function template (auto parameter) with function-try-block
void safe_proc(auto x)
try {
	if (x == 0) throw 42;
} catch (int e) {
	g_caught = e;
}

// auto parameter with explicit return type
int safe_negate(auto x)
try {
	if (x == 0) throw -1;
	return -x;
} catch (int e) {
	return e;
}

int main() {
	// No exception path
	safe_proc(1);
	if (g_caught != 0) return 1;

	// Exception caught path
	safe_proc(0);
	if (g_caught != 42) return 2;

	// Explicit return type with no exception
	if (safe_negate(3) != -3) return 3;

	// Explicit return type with exception
	if (safe_negate(0) != -1) return 4;

	return 0;
}
