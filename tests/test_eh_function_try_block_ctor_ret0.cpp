// Regression test: constructor function-try-block (C++ [dcl.fct.def.general]).
// Constructors can use the 'try' keyword before the colon-initializer list.
// This allows catching exceptions thrown by member initializers.

int g_ctors = 0;
int g_caught = 0;

// Inline constructor function-try-block: try before member-initializer list
struct InlineTryCtor {
	int x;
	InlineTryCtor(int v)
	try : x(v) {
		g_ctors++;
	} catch (int e) {
		g_caught = e;
	}
};

// Out-of-line constructor with try : form
struct OolTryCtor {
	int value;
	OolTryCtor(int v);
};

OolTryCtor::OolTryCtor(int v)
try : value(v) {
	g_ctors++;
} catch (int e) {
	g_caught = e;
}

int main() {
	// Inline constructor: normal path
	InlineTryCtor a(5);
	if (a.x != 5) return 1;
	if (g_ctors != 1) return 2;

	// Out-of-line constructor: normal path
	OolTryCtor b(7);
	if (b.value != 7) return 3;
	if (g_ctors != 2) return 4;

	return 0;
}
