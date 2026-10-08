// __is_nothrow_constructible(T, Args) must read the canonical constructor schema
// for an exact argument match. Before this was fixed the argument-bearing query
// used only the compatibility fallback, which assumes every user-provided
// constructor is potentially throwing, so an exact noexcept constructor was
// reported throwing.
struct NothrowInt {
	NothrowInt(int v) noexcept(true) : value(v) {}
	int value;
};

struct ThrowingInt {
	ThrowingInt(int v) noexcept(false) : value(v) {}
	int value;
};

// A converting argument does not match the schema exactly; the compatibility
// resolver selects the constructor and its exception specification is read.
struct ConvertingNothrow {
	ConvertingNothrow(long) noexcept(true) {}
};

struct ConvertingThrowing {
	ConvertingThrowing(long) noexcept(false) {}
};

// An argument-bearing trivial construction is a copy or move, so it is trivial
// exactly when the class is trivially copyable.
struct NonTrivialMember {
	NonTrivialMember(const NonTrivialMember&) {}
	int value = 0;
};

struct NonTrivialCopy {
	NonTrivialMember member;
};

struct TrivialCopy {
	int value;
};

static_assert(!__is_trivially_constructible(NonTrivialCopy, const NonTrivialCopy&), "non-trivial copy constructor");
static_assert(__is_trivially_constructible(TrivialCopy, const TrivialCopy&), "trivial copy constructor");

static_assert(__is_constructible(NothrowInt, int), "exact argument is constructible");
static_assert(__is_constructible(ThrowingInt, int), "exact throwing argument is constructible");
static_assert(__is_nothrow_constructible(NothrowInt, int), "exact noexcept constructor is nothrow");
static_assert(!__is_nothrow_constructible(ThrowingInt, int), "exact throwing constructor is not nothrow");
static_assert(__is_constructible(ConvertingNothrow, int), "converting argument stays constructible");
static_assert(__is_nothrow_constructible(ConvertingNothrow, int), "converting noexcept constructor is nothrow");
static_assert(!__is_nothrow_constructible(ConvertingThrowing, int), "converting throwing constructor is not nothrow");

int main() {
	NothrowInt nothrow{1};
	ThrowingInt throwing{2};
	return nothrow.value == 1 && throwing.value == 2 ? 0 : 1;
}
