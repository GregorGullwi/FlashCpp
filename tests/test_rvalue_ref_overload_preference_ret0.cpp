// Regression test for C++20 [over.ics.rank] p3.3.1.4:
// binding an rvalue to T&& is preferred over binding it to const T&.
// Before the fix there was no rank demotion for rvalue→const T&, so it
// returned ExactMatch — the same rank as rvalue→T&&.  This caused ambiguity
// in overload resolution (both candidates tied).
// With the fix the rank for rvalue→const T& is demoted to Conversion(3),
// ensuring T&& (ExactMatch) always wins over const T& for rvalue arguments.

int f(int&&) { return 1; }
int f(const int&) { return 2; }

struct S { int val; };
int g(S&&) { return 10; }
int g(const S&) { return 20; }

int main() {
	int x = 5;
	// Cast to rvalue: should prefer f(int&&)
	int r1 = f((int&&)x);
	if (r1 != 1)
		return r1;

	// Lvalue: should prefer f(const int&)
	int r2 = f(x);
	if (r2 != 2)
		return r2;

	// Rvalue struct: should prefer g(S&&)
	S s{3};
	int r3 = g((S&&)s);
	if (r3 != 10)
		return r3;

	// Lvalue struct: should prefer g(const S&)
	int r4 = g(s);
	if (r4 != 20)
		return r4;

	return 0;
}
