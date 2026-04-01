// Regression test: a template-instantiated nested struct's default member
// initializer references a static member from an unrelated non-template struct.
// The store-name rewrite in resolveGlobalOrStaticBinding must NOT rewrite
// "B::value" to "Outer$hash::Inner::value" just because the owner lacks '$'
// and the current struct has '$'.
struct B {
	static constexpr int value = 10;
};

template<int N>
struct Outer {
	struct Inner {
		int x = B::value + N;
	};
};

int main() {
	Outer<5>::Inner i;
	if (i.x != 15) return 1;  // B::value(10) + N(5) = 15

	Outer<32>::Inner j;
	if (j.x != 42) return 2;  // B::value(10) + N(32) = 42

	return 0;
}
