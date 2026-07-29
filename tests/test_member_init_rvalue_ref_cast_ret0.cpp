// Regression: a member initializer whose expression yields a reference
// (static_cast<T&&> / std::move / std::forward, i.e. an xvalue) arrives in
// codegen holding the *address* of the source.  Storing that address into a
// non-reference scalar member would copy the pointer instead of the pointee
// value.  The member-init path must load through the reference first.
// Covers class templates, member-function templates, and mixed native sizes.

namespace std {
	template <typename T> struct remove_reference { using type = T; };
	template <typename T> struct remove_reference<T&> { using type = T; };
	template <typename T> struct remove_reference<T&&> { using type = T; };
	template <typename T> using remove_reference_t = typename remove_reference<T>::type;
	template <typename T>
	constexpr T&& forward(remove_reference_t<T>& t) noexcept { return static_cast<T&&>(t); }
	template <typename T>
	constexpr T&& forward(remove_reference_t<T>&& t) noexcept { return static_cast<T&&>(t); }
}

// Class template whose forwarding member-template ctor inits via static_cast<U&&>.
template <typename T1, typename T2>
struct FwdPair {
	T1 first;
	T2 second;
	template <typename U1, typename U2>
	FwdPair(U1&& a, U2&& b) : first(static_cast<U1&&>(a)), second(static_cast<U2&&>(b)) {}
};

// Same but through std::forward (a call returning U&&).
template <typename T1, typename T2>
struct MovePair {
	T1 first;
	T2 second;
	template <typename U1, typename U2>
	MovePair(U1&& a, U2&& b) : first(std::forward<U1>(a)), second(std::forward<U2>(b)) {}
};

// Non-template struct with a member-template ctor (int member via static_cast<U&&>).
struct IntBox {
	int v;
	template <typename U>
	IntBox(U&& x) : v(static_cast<U&&>(x)) {}
};

// Mixed sizes: char, long long, double through the same mechanism.
template <typename T>
struct One {
	T m;
	template <typename U>
	One(U&& x) : m(static_cast<U&&>(x)) {}
};

int main() {
	FwdPair<int, float> a(42, 2.5f);
	if (a.first != 42) return 1;
	if (a.second < 2.0f || a.second > 3.0f) return 2;

	MovePair<int, double> b(7, 9.5);
	if (b.first != 7) return 3;
	if (b.second < 9.0 || b.second > 10.0) return 4;

	IntBox c(100);
	if (c.v != 100) return 5;

	One<char> d('A');
	if (d.m != 'A') return 6;

	One<long long> e(1234567890123LL);
	if (e.m != 1234567890123LL) return 7;

	One<double> f(3.75);
	if (f.m < 3.0 || f.m > 4.0) return 8;

	return 0;
}
