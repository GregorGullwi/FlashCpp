// A scalar argument must select a direct scalar constructor even when the class
// also has a copy/move constructor that could form a temporary through one
// user-defined conversion. The direct candidate wins by conversion rank.
struct ExplicitScalar {
	explicit ExplicitScalar(int) noexcept {}
	ExplicitScalar(const ExplicitScalar&) = default;
};

struct UserCopy {
	UserCopy(int) noexcept {}
	UserCopy(const UserCopy&) noexcept(false) {}
};

struct OnlyCopy {
	OnlyCopy() noexcept {}
	OnlyCopy(const OnlyCopy&) noexcept {}
};

struct ScalarViaLong {
	ScalarViaLong(long) noexcept {}
	ScalarViaLong(const ScalarViaLong&) noexcept(false) {}
};

enum ScalarEnum {
	ScalarValue
};

struct FromEnum {
	FromEnum(ScalarEnum) noexcept {}
	FromEnum(const FromEnum&) noexcept(false) {}
};

static_assert(__is_constructible(ExplicitScalar, int), "explicit scalar constructor wins over copy");
static_assert(__is_nothrow_constructible(ExplicitScalar, int), "explicit scalar constructor is noexcept");
static_assert(__is_constructible(UserCopy, int), "user-declared copy constructor does not poison selection");
static_assert(__is_nothrow_constructible(UserCopy, int), "direct scalar constructor outranks throwing copy");
static_assert(!__is_constructible(OnlyCopy, int), "copy-only class has no scalar conversion");
static_assert(__is_constructible(ScalarViaLong, int), "standard scalar conversion selects the direct constructor");
static_assert(__is_nothrow_constructible(ScalarViaLong, int), "direct long constructor outranks throwing copy");
static_assert(__is_constructible(FromEnum, ScalarEnum), "enum argument converts to the direct class constructor");
static_assert(__is_nothrow_constructible(FromEnum, ScalarEnum), "direct enum constructor outranks throwing copy");

int main() {
	return 0;
}
