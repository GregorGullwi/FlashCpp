// Regression test: pointer-depth pre-filter must not reject a candidate when
// the declared parameter type IS a template type parameter (e.g. U, not U*).
//
// Before the fix, `pick<int*>(&value)` was incorrectly rejected by the
// pre-filter because the declared parameter `U` has pointer_depth()==0, even
// though U is being *explicitly* instantiated as `int*` — making the effective
// parameter type `int*` (depth 1).
//
// The pointer-depth check should only fire when the parameter type is a fully
// concrete, non-dependent type whose pointer depth is structurally known before
// substitution.
//
// Overload-selection logic:
//   pick<int*>(&x):  explicit U=int*
//     pick(U  value) → pick(int*)  — param=int*, arg=int*  → VIABLE  (returns 0)
//     pick(U* value) → pick(int**) — param=int**, arg=int* → NOT VIABLE
//   So pick(U value) with U=int* must win → return 0.

template <typename T>
struct Holder {
	// Overload 1: parameter type IS the member-template param U (depth 0 declared)
	template <typename U>
	int pick(U value) {
		(void)value;
		return 0;  // correct winner when called as pick<int*>(&x)
	}

	// Overload 2: parameter is U* (depth 1 declared)
	template <typename U>
	int pick(U* value) {
		(void)value;
		return 1;  // not viable when called as pick<int*>(&x) — would need int**
	}
};

int main() {
	Holder<int> h;
	int x = 42;
	// Explicit template arg int* → U=int*.
	// pick(U value) = pick(int*):  arg int* → viable → should return 0.
	// pick(U* value) = pick(int**): arg int* → not viable.
	// Before the fix the pre-filter wrongly dropped pick(U value) because the
	// *declared* U had pointer_depth()==0 while the call arg was a pointer.
	return h.pick<int*>(&x);
}
