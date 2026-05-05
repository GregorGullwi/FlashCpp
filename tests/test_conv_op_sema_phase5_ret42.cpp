struct WithConvOp {
	int value;
	explicit WithConvOp(int v) : value(v) {}
	operator int() const { return value; }
	operator double() const { return static_cast<double>(value); }
};

struct InheritedConvOp : WithConvOp {
	explicit InheritedConvOp(int v) : WithConvOp(v) {}
};

struct BoxedLong {
	long long value;
	BoxedLong(long long v) : value(v) {}
	operator long long() const { return value; }
	operator int() const { return static_cast<int>(value); }
};

int take_int(int x) { return x; }
double take_double(double x) { return x > 41.5 ? 42 : 0; }
long long add_longlongs(long long a, long long b) { return a + b; }

int return_converted_int() {
	WithConvOp obj(21);
	return obj;
}

int main() {
	WithConvOp obj(42);
	int r1 = take_int(obj);
	if (r1 != 42)
		return 1;

	int r2 = obj;
	if (r2 != 42)
		return 2;

	double r3 = take_double(obj);
	if (static_cast<int>(r3) != 42)
		return 3;

	InheritedConvOp inherited(42);
	int r4 = take_int(inherited);
	if (r4 != 42)
		return 4;

	WithConvOp half(21);
	double d = half;
	if (static_cast<int>(d) != 21)
		return 5;

	int from_return = return_converted_int();
	if (from_return != 21)
		return 6;

	BoxedLong boxed(10LL);
	long long sum_ll = add_longlongs(boxed, boxed);
	if (sum_ll != 20)
		return 7;

	int from_boxed_long = take_int(boxed);
	if (from_boxed_long != 10)
		return 8;

	return 42;
}
