// Regression test for reference member access in Load context.
// Bug: after MemberAccess loads the stored pointer for a reference member,
// the IR was missing a Dereference instruction in Load context, causing the
// raw pointer bits to be used as the value instead of the dereferenced int/double.
//
// Also covers: applyConstructorArgConversion now applies pre-bind type conversions
// for reference parameters (e.g. int → const double&).

// --- Part 1: const int& member ---
struct BoxInt {
	const int& val;
	BoxInt(const int& v) : val(v) {}
	int get() const { return val; }
};

// --- Part 2: non-const int& member (mutation through reference) ---
struct Wrapper {
	int& r;
	Wrapper(int& x) : r(x) {}
	void inc() { r = r + 1; }
	void add(int n) { r = r + n; }
	int get() const { return r; }
};

// --- Part 3: double& member ---
struct BoxDouble {
	const double& d;
	BoxDouble(const double& v) : d(v) {}
	double get() const { return d; }
};

// --- Part 4: constructor taking const double& from int literal (pre-bind conversion) ---
struct Sink {
	double stored;
	Sink(const double& x) : stored(x) {}
};

int main() {
	// Part 1: reading through const int& member
	int a = 42;
	BoxInt b(a);
	if (b.get() != 42) return 1;

	// Part 2: mutation must propagate back through non-const int& member
	int c = 10;
	Wrapper w(c);
	w.inc();
	if (c != 11) return 2;		// caller sees the mutation
	if (w.get() != 11) return 3;
	w.add(5);
	if (c != 16) return 4;
	if (w.get() != 16) return 5;

	// Part 3: reading through const double& member
	double dd = 3.14;
	BoxDouble bd(dd);
	if ((int)(bd.get() * 100) != 314) return 6;

	// Part 4: constructor pre-bind conversion int → const double&
	Sink s(7);
	if ((int)s.stored != 7) return 7;

	// Part 5: const int& member with int literal arg (temporary materialization)
	BoxInt lit(99);
	if (lit.get() != 99) return 8;

	return 0;
}
