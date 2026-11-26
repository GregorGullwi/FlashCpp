// A partial class-template pattern whose template argument is a function type
// must publish a canonical pattern environment. The pattern's deduced parameter
// is the function's return type; the parameter list is part of the pattern's
// identity, so a different signature selects the primary or another partial
// instead of binding the whole callable type to the parameter.

template <class T> struct FnBox {
	static constexpr int tag = 0;
	T value;
};

template <class R> struct FnBox<R(int)> {
	static constexpr int tag = 1;
	using value_type = R;
	R value;
	int marker = 11;
	FnBox(R seed) : value(static_cast<R>(seed * 2)) {}
};

template <class R> struct FnBox<R(long)> {
	static constexpr int tag = 2;
	using value_type = R;
	R value;
	short marker = 22;
	FnBox(short seed) : value(static_cast<R>(seed * 3)) {}
};

using FnInt = FnBox<int(int)>;
using FnIntAgain = FnBox<int(int)>;
using FnCharLong = FnBox<char(long)>;
using FnPrimary = FnBox<int>;

static_assert(FnInt::tag == 1, "int(int) selects the R(int) partial");
static_assert(FnCharLong::tag == 2, "char(long) selects the R(long) partial");
static_assert(FnPrimary::tag == 0, "a non-function argument selects the primary");
static_assert(__is_same(FnInt, FnIntAgain), "one signature has one identity");
static_assert(!__is_same(FnInt, FnCharLong), "different signatures are distinct");
static_assert(__is_same(FnInt::value_type, int), "the parameter deduces the return type");
static_assert(__is_same(FnCharLong::value_type, char), "each signature deduces its own return type");
static_assert(sizeof(FnInt::marker) == sizeof(int), "the R(int) partial owns its schema");
static_assert(sizeof(FnCharLong::marker) == sizeof(short), "the R(long) partial owns its schema");

// Empty parameter lists and parameter-shaped parameter types are distinct
// signature shapes and publish their own environments.
template <class T> struct FnEmpty {
	static constexpr int tag = 0;
};

template <class R> struct FnEmpty<R()> {
	static constexpr int tag = 1;
	using result = R;
};

template <class T> struct FnPair {
	static constexpr int tag = 0;
};

template <class R, class A> struct FnPair<R(A)> {
	static constexpr int tag = 1;
	using result = R;
	using argument = A;
};

static_assert(FnEmpty<int()>::tag == 1, "the empty parameter list selects the partial");
static_assert(FnEmpty<int(int)>::tag == 0, "a non-empty parameter list selects the primary");
static_assert(__is_same(FnEmpty<int()>::result, int), "the empty-signature return type deduces");
static_assert(FnPair<int(char)>::tag == 1, "a parameter-shaped parameter selects the partial");
static_assert(__is_same(FnPair<int(char)>::result, int), "the return type deduces");
static_assert(__is_same(FnPair<int(char)>::argument, char), "the parameter type deduces");

// Nested member partials use the same canonical function-signature identity.
template <class T> struct FnOwner {
	template <class U> struct Inner {
		static constexpr int tag = 0;
	};
	template <class R> struct Inner<R(int)> {
		static constexpr int tag = 1;
		using result = R;
	};
	template <class R> struct Inner<R(char)> {
		static constexpr int tag = 2;
		using result = R;
	};
};

static_assert(FnOwner<short>::Inner<int(int)>::tag == 1, "the member partial selects R(int)");
static_assert(FnOwner<short>::Inner<long(char)>::tag == 2, "the member partial selects R(char)");
static_assert(FnOwner<short>::Inner<int>::tag == 0, "a non-function argument selects the member primary");
static_assert(__is_same(FnOwner<short>::Inner<int(int)>::result, int), "the member return type deduces");
static_assert(__is_same(FnOwner<short>::Inner<long(char)>::result, long), "each member signature deduces");

int main() {
	int result = 0;
	FnInt integer(5);
	FnCharLong character(static_cast<short>(4));
	FnPrimary primary{};
	primary.value = 3;
	if (FnInt::tag != 1 || integer.value != 10 || integer.marker != 11) {
		result |= 1;
	}
	if (FnCharLong::tag != 2 || character.value != 12 || character.marker != 22) {
		result |= 2;
	}
	if (FnPrimary::tag != 0 || primary.value != 3) {
		result |= 4;
	}
	return result;
}
