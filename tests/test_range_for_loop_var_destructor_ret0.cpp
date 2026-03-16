// Regression test: range-for by-value loop variable with destructor.
// The loop variable is constructed each iteration from *__begin and must be
// destroyed at the end of each iteration via exitScope() + ScopeEnd IR.
// If the codegen scope around the loop variable is missing enterScope()/
// exitScope(), the destructor will never fire and g_dtor_count stays 0.

int g_dtor_count = 0;

struct Wrapper {
	int value;

	Wrapper() : value(0) {}
	Wrapper(int v) : value(v) {}
	Wrapper(const Wrapper& other) : value(other.value) {}

	~Wrapper() {
		++g_dtor_count;
	}
};

int main() {
	int arr[3] = {10, 20, 30};

	g_dtor_count = 0;

	// Each iteration creates a by-value Wrapper from *__begin.
	// The Wrapper must be destroyed at the end of each iteration
	// (3 iterations -> 3 destructor calls).
	for (Wrapper w : arr) {
		if (w.value < 0) {
			return 99;
		}
	}

	// Expected: 3 destructor calls (one per iteration)
	// Bug: 0 destructor calls (exitScope() never called for loop var scope)
	return (g_dtor_count == 3) ? 0 : 1;
}
