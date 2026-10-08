// noexcept(expr) must consult the selected overloaded operator's exception
// specification; before this was fixed an overloaded operator call was treated
// like a built-in one and always reported noexcept.
struct ThrowingAssign {
	ThrowingAssign& operator=(const ThrowingAssign&) noexcept(false) {}
};

struct NothrowAssign {
	NothrowAssign& operator=(const NothrowAssign&) noexcept(true) {}
};

struct ThrowingAdd {};

ThrowingAdd operator+(const ThrowingAdd&, const ThrowingAdd&) noexcept(false);

static_assert(!noexcept(*(ThrowingAssign*)nullptr = *(ThrowingAssign*)nullptr), "member operator= noexcept(false)");
static_assert(noexcept(*(NothrowAssign*)nullptr = *(NothrowAssign*)nullptr), "member operator= noexcept(true)");
static_assert(!noexcept(*(ThrowingAdd*)nullptr + *(ThrowingAdd*)nullptr), "free operator+ noexcept(false)");
static_assert(noexcept(1 + 2), "built-in arithmetic stays noexcept");

int main() {
	ThrowingAssign a{};
	NothrowAssign n{};
	(void)a;
	(void)n;
	return 0;
}
