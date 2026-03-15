// Test: Phase 4 – inferExpressionType handles explicit cast sub-expressions.
// (int)x and static_cast<int>(x) must return the cast target type so that
// surrounding conversion annotations work correctly.

int add_int_and_long(long a, long b) { return (int)(a + b); }

// Explicit C++-style cast in a binary expression that then feeds into another conversion.
long cast_then_widen(float f) {
	int i = (int)f;          // float → int via C-style cast
	return i;                 // int → long (return conversion annotated by sema pass)
}

// static_cast result feeding into a declaration initializer
long static_cast_to_long(int x) {
	long y = static_cast<long>(x);
	return y;
}

// Cast expression as a call argument: (int)f should infer as int
int cast_arg(float f) {
	return add_int_and_long((int)f, 0L);
}

int main() {
	int r1 = (int)cast_then_widen(3.7f);   // 3.7 → 3 → (long)3 → (int)3
	int r2 = (int)static_cast_to_long(42); // 42 → (long)42 → (int)42

	// cast_arg: (int)2.9f == 2, add_int_and_long(2L, 0L) == 2
	int r3 = cast_arg(2.9f);

	return (r1 - 3) + (r2 - 42) + (r3 - 2);
}
