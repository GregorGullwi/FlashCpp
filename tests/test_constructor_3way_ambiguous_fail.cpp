// Test that 3-candidate constructor overload resolution detects ambiguity
// when a later strictly-better candidate replaces the current best but an
// earlier incomparable candidate was discarded without re-evaluation.
//
// resolve_constructor_overload has TWO bugs (same pattern as resolve_overload):
//   1. The incomparable case (this_is_better && this_is_worse) falls through
//      silently because the else-if only checks (!this_is_better && !this_is_worse).
//   2. When a strictly-better candidate replaces the current best, previously
//      accumulated tied/incomparable candidates are discarded without being
//      re-evaluated against the new best.
//
// Constructor evaluation for Foo(x, y, z) with args (short, short, short):
//   A: Foo(double, double, short) -> ranks [Conversion, Conversion, Exact]
//   B: Foo(short, double, double) -> ranks [Exact, Conversion, Conversion]
//   C: Foo(int, int, short)       -> ranks [Promotion, Promotion, Exact]
//
// B incomparable with A: arg0 better, arg2 worse -> incomparable
//   (BUG #1: silently dropped because else-if only matches exact ties)
// C strictly better than A: arg0 better, arg1 better, arg2 equal
//   (BUG #2: even if B survived bug #1, it would be discarded here)
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
