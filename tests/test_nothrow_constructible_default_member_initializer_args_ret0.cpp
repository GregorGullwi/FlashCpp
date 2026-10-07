// A class with an implicit default constructor and a class-type member whose
// default member initializer calls a non-default constructor must account for
// that constructor's exception specification. Before this was fixed, only a
// default-construction initializer (`Member member{}`) contributed to the
// nothrow answer; a `Member member{arg}` initializer was ignored, so a class
// whose member construction can throw was reported nothrow-constructible.
struct ThrowingCtor {
	ThrowingCtor(int value) : value(value) {}
	int value;
};

struct NothrowCtor {
	NothrowCtor(int value) noexcept : value(value) {}
	int value;
};

struct ThrowingTwoArg {
	ThrowingTwoArg(int, long) {}
};

struct NothrowTwoArg {
	NothrowTwoArg(int, long) noexcept {}
};

// Native members are mixed in; only the class-type member initializer can
// throw.
struct ThrowingInitializer {
	int tag;
	ThrowingCtor member{7};
};

struct NothrowInitializer {
	int tag;
	NothrowCtor member{7};
};

struct ThrowingTwoArgInitializer {
	ThrowingTwoArg member{1, 2L};
};

struct NothrowTwoArgInitializer {
	NothrowTwoArg member{1, 2L};
};

// The parenthesized copy-initialization spelling selects the same constructor.
struct ThrowingCopyInitializer {
	ThrowingCtor member = ThrowingCtor(7);
};

static_assert(!__is_nothrow_constructible(ThrowingCopyInitializer), "a throwing copy member initializer makes default construction throwing");

static_assert(__is_constructible(ThrowingInitializer), "a throwing member initializer is still constructible");
static_assert(!__is_nothrow_constructible(ThrowingInitializer), "a throwing member initializer makes default construction throwing");
static_assert(__is_nothrow_constructible(NothrowInitializer), "a noexcept member initializer keeps default construction non-throwing");
static_assert(!__is_nothrow_constructible(ThrowingTwoArgInitializer), "a throwing two-arg member initializer makes default construction throw");
static_assert(__is_nothrow_constructible(NothrowTwoArgInitializer), "a noexcept two-arg member initializer keeps default construction non-throwing");

// Any default member initializer makes the default constructor non-trivial.
static_assert(!__is_trivially_constructible(ThrowingInitializer), "a member initializer makes default construction non-trivial");
static_assert(!__is_trivially_constructible(NothrowInitializer), "a member initializer makes default construction non-trivial");

int main() {
	ThrowingInitializer value{};
	return value.tag == 0 && value.member.value == 7 ? 0 : 1;
}
