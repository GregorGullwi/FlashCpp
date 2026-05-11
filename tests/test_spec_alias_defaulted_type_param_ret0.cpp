// Test: partial specializations correctly handle defaulted template arguments
// when the first argument is concrete (e.g. Foo<true> selects Foo<true, T> with
// T defaulting to int).  Aliases that go through a member type (typename Wrap<T>::inner)
// must resolve to the defaulted type, not to the concrete leading argument.

// A simple wrapper whose ::inner is just T.
template<typename T>
struct Wrap {
	using inner = T;
};

// Primary template: B=bool, T defaults to int.
template<bool B, typename T = int>
struct Foo;

// Partial specialization on B=true; alias uses a member of Wrap<T>.
template<typename T>
struct Foo<true, T> {
	using result = typename Wrap<T>::inner;
};

// -----------------------------------------------------------------------
// Secondary fixture: NTTP concrete first arg, same default pattern.
// -----------------------------------------------------------------------
template<int N, typename T = long>
struct Bar;

template<typename T>
struct Bar<7, T> {
	using result = typename Wrap<T>::inner;
};

int main() {
	// Explicit T — both args given; this is the non-defaulted baseline.
	Foo<true, double>::result explicit_val = 2.5;
	(void)explicit_val;

	// Default T=int — only one arg given; default fill must supply int.
	Foo<true>::result default_int = 42;

	// Explicit NTTP fixture with T=char.
	Bar<7, char>::result explicit_char = 'X';
	(void)explicit_char;

	// Default T=long.
	Bar<7>::result default_long = 99L;

	// The returns check that the correct types were deduced.
	if (default_int != 42)
		return 1;
	if (default_long != 99L)
		return 2;
	return 0;
}
