// Regression test: template constructor with member initializer list.
// Before the fix, the member-init expressions were skipped during parsing and
// the member was zero-initialised instead of being set from the argument.
// The test verifies both paren-init and brace-init call syntax.
struct ValueWrapper {
	int value;

	template<typename T>
	ValueWrapper(T v) : value(v) {}
};

struct Multi {
	int a;
	int b;

	template<typename A, typename B>
	Multi(A x, B y) : a(x), b(y) {}
};

int main() {
	ValueWrapper w(42);
	if (w.value != 42) return 1;

	ValueWrapper w2{100};
	if (w2.value != 100) return 2;

	Multi m(3, 7);
	if (m.a != 3) return 3;
	if (m.b != 7) return 4;

	return 0;
}
