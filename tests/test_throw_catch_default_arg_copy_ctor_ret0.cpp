// Test: throwing a struct with a copy constructor that has trailing default args.
// Exercises the emitSameTypeCopyOrMoveConstructorCall early-return path for
// multi-param ctors (params.size() > 1), which falls through to the raw-copy
// path. Verifies compilation succeeds and the caught value is correct.
struct ThrownEx {
	int x;
	ThrownEx(int v) : x(v) {}
	ThrownEx(const ThrownEx& o, int n = 0) : x(o.x + n) {}
};

int main() {
	ThrownEx a(42);
	try {
		throw a;
	} catch (ThrownEx e) {
		if (e.x != 42)
			return 1;
		return 0;
	}
	return 2;
}
