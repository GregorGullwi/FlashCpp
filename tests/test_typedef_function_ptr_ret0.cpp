// T27: typedef'd function pointers - test that is_function_pointer() category()
// dispatch works correctly for function pointer type aliases (typedef and using).
// Note: function pointer in template struct member call is tracked as a known issue.

typedef int (*IntFn)(int);
using IntFn2 = int (*)(int);

// Struct with typedef'd function pointer member (non-template)
struct Dispatcher {
	IntFn fn;
	int dispatch(int v) const { return fn(v); }
};

int double_it(int x) { return x * 2; }
int triple_it(int x) { return x * 3; }

// Function taking typedef'd function pointer parameter (must pre-assign to local)
int call_fn(IntFn f, int v) { return f(v); }
int call_fn2(IntFn2 f, int v) { return f(v); }

int main() {
	// Direct usage of typedef'd function pointer
	IntFn f = double_it;
	if (f(5) != 10)
		return 1;

	IntFn2 g = triple_it;
	if (g(5) != 15)
		return 2;

	// Struct with typedef'd function pointer member
	Dispatcher d{double_it};
	if (d.dispatch(7) != 14)
		return 3;

	// Function pointer typedef as parameter type (via local variable)
	IntFn fn3 = triple_it;
	IntFn2 fn4 = double_it;
	if (call_fn(fn3, 4) != 12)
		return 4;
	if (call_fn2(fn4, 6) != 12)
		return 5;

	// Assignment and reassignment
	f = triple_it;
	if (f(3) != 9)
		return 6;
	f = double_it;
	if (f(3) != 6)
		return 7;

	return 0;
}
