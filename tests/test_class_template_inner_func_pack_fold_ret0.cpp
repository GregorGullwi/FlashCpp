// Regression test for Phase 6 known-issue:
// Inner member function pack inside a class template context
// previously deduced only 1 element for the inner pack, causing
// a unary fold like `(0 + ... + args)` to return only the first
// argument instead of the full sum.
//
// The fix lives in src/Parser_Templates_Inst_Deduction.cpp
// (buildDeductionMapFromCallArgs fallback detection) and
// src/Parser_Templates_Inst_MemberFunc.cpp
// (variadic type-pack handling in
// try_instantiate_member_function_template).

template<typename... Ts>
struct Wrapper {
	int value;

	template<typename... Us>
	int call(Us... args) {
		return (0 + ... + args);
	}
};

template<typename T>
struct BoxWrapper {
	T value;

	template<typename... Us>
	int call_boxed(Us... args) {
		return (0 + ... + args);
	}
};

int main() {
	Wrapper<int, double> w;
	w.value = 0;
	int a = w.call(10, 15, 17);              // expect 42

	Wrapper<char> w2;
	w2.value = 0;
	int b = w2.call(1, 2, 3, 4, 5, 6, 7, 8); // expect 36

	BoxWrapper<long> bw;
	bw.value = 0;
	int c = bw.call_boxed(100, 200, 300);    // expect 600

	// All three must match the expected sums for this test to
	// return 0; any regression in inner-pack fold expansion will
	// cause a non-zero exit.
	return (a - 42) + (b - 36) + (c - 600);
}
