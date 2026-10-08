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

// A converting argument does not match the schema exactly and still uses the
// compatibility path, which reports a user-provided constructor as throwing.
struct ConvertingLong {
	ConvertingLong(long) noexcept(true) {}
};

static_assert(__is_constructible(NothrowInt, int), "exact argument is constructible");
static_assert(__is_constructible(ThrowingInt, int), "exact throwing argument is constructible");
static_assert(__is_nothrow_constructible(NothrowInt, int), "exact noexcept constructor is nothrow");
static_assert(!__is_nothrow_constructible(ThrowingInt, int), "exact throwing constructor is not nothrow");
static_assert(__is_constructible(ConvertingLong, int), "converting argument stays constructible");

int main() {
	NothrowInt nothrow{1};
	ThrowingInt throwing{2};
	return nothrow.value == 1 && throwing.value == 2 ? 0 : 1;
}
