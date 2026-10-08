// noexcept(T{}) must consult the selected constructor's exception specification.
// Before this was fixed every class constructor call was reported throwing.
struct Nothrow {
	Nothrow() noexcept(true) {}
};

struct Throwing {
	Throwing() noexcept(false) {}
};

struct NothrowInt {
	NothrowInt(int) noexcept(true) {}
};

struct ThrowingInt {
	ThrowingInt(int) noexcept(false) {}
};

static_assert(noexcept(Nothrow{}), "noexcept default constructor");
static_assert(noexcept(Nothrow()), "noexcept default constructor, parenthesized");
static_assert(!noexcept(Throwing{}), "throwing default constructor");
static_assert(noexcept(NothrowInt(1)), "noexcept argument-bearing constructor");
static_assert(!noexcept(ThrowingInt(1)), "throwing argument-bearing constructor");
static_assert(noexcept(static_cast<int>(3.5)), "built-in functional cast stays noexcept");

int main() {
	Nothrow n{};
	Throwing t{};
	(void)n;
	(void)t;
	return 0;
}
