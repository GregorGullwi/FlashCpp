// Phase 22 regression: user-defined conversion operator (operator int / operator double)
// through the function-argument path, using the sema-owned UserDefined annotation.

struct BoxedInt {
	int value;
	BoxedInt(int v) : value(v) {}
	operator int() const { return value; }
	operator double() const { return static_cast<double>(value); }
};

struct BoxedLong {
	long long value;
	BoxedLong(long long v) : value(v) {}
	operator long long() const { return value; }
	operator int() const { return static_cast<int>(value); }
};

static int add_ints(int a, int b) { return a + b; }
static double add_doubles(double a, double b) { return a + b; }
static long long add_longlongs(long long a, long long b) { return a + b; }

int main() {
	BoxedInt bi(21);
	BoxedLong bl(10LL);

 // function-arg path: sema annotates UserDefined, codegen calls operator int()
	int sum_int = add_ints(bi, bi);			// 21 + 21 = 42
	if (sum_int != 42)
		return 1;

 // function-arg path: operator double()
	double sum_d = add_doubles(bi, bi);		// 21.0 + 21.0 = 42.0
	if (static_cast<int>(sum_d) != 42)
		return 2;

 // function-arg path: operator long long()
	long long sum_ll = add_longlongs(bl, bl); // 10 + 10 = 20
	if (sum_ll != 20)
		return 3;

 // function-arg path: operator int() from BoxedLong
	int from_bl = add_ints(bl, bl);			// 10 + 10 = 20
	if (from_bl != 20)
		return 4;

	return sum_int;	// 42
}
