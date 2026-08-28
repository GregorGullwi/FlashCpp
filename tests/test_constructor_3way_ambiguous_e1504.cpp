// Test that 3-candidate constructor overload resolution detects ambiguity
// when a later strictly-better candidate replaces the current best but an
// earlier incomparable candidate must be re-evaluated against the new best.
//
// Constructor evaluation for Foo(x, y, z) with args (short, short, short):
//   A: Foo(double, double, short) -> ranks [Conversion, Conversion, Exact]
//   B: Foo(short, double, double) -> ranks [Exact, Conversion, Conversion]
//   C: Foo(int, int, short)       -> ranks [Promotion, Promotion, Exact]
//
// B incomparable with A: arg0 better, arg2 worse -> incomparable
// C strictly better than A: arg0 better, arg1 better, arg2 equal
//   -> C replaces A; B is re-evaluated against C
// C incomparable with B: arg0 worse (Promotion > Exact), arg1 better, arg2 better
//
// Correct result: AMBIGUOUS (no single constructor dominates all others)

struct Foo {
	int value;
	Foo(double a, double b, short c) : value(1) {}
	Foo(short a, double b, double c) : value(2) {}
	Foo(int a, int b, short c) : value(3) {}
};

int main() {
	short x = 1;
	short y = 2;
	short z = 3;
	Foo f(x, y, z); // ambiguous: should fail to compile
	return f.value;
}
