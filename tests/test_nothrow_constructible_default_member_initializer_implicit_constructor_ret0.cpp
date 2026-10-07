// A class-type member initialized from a value selects the member type's
// implicit or defaulted copy or move constructor. Its exception specification
// derives from the member type's subobjects, so a class whose member type has a
// throwing subobject constructor must not be reported nothrow-constructible.
// Before this was fixed, the parser skipped implicit copy/move constructors and
// the walk deferred, so the class was reported non-throwing.
struct ThrowingBase {
	ThrowingBase() {}
	ThrowingBase(const ThrowingBase&) {}
	ThrowingBase(ThrowingBase&&) {}
};

struct NothrowBase {
	NothrowBase() noexcept {}
	NothrowBase(const NothrowBase&) noexcept {}
	NothrowBase(NothrowBase&&) noexcept {}
};

struct ThrowingMember {
	ThrowingBase base;
};

struct NothrowMember {
	NothrowBase base;
};

// The initializer selects the member type's implicit move constructor.
struct ThrowingHolder {
	int tag;
	ThrowingMember member{ThrowingMember{}};
};

struct NothrowHolder {
	int tag;
	NothrowMember member{NothrowMember{}};
};

// The same applies to an array of such members constructed by an explicit move.
struct ThrowingArrayHolder {
	ThrowingMember members[2] = {ThrowingMember(ThrowingMember{}), ThrowingMember(ThrowingMember{})};
};

// An explicitly defaulted copy constructor derives its specification the same
// way.
struct DefaultedThrowingMember {
	ThrowingBase base;
	DefaultedThrowingMember() = default;
	DefaultedThrowingMember(const DefaultedThrowingMember&) = default;
};

struct DefaultedHolder {
	DefaultedThrowingMember member{DefaultedThrowingMember{}};
};

static_assert(__is_constructible(ThrowingHolder), "a throwing member type is still constructible");
static_assert(!__is_nothrow_constructible(ThrowingHolder), "an implicit move constructor of a throwing member type is throwing");
static_assert(__is_nothrow_constructible(NothrowHolder), "an implicit move constructor of a noexcept member type is non-throwing");
static_assert(!__is_nothrow_constructible(ThrowingArrayHolder), "an array of implicit-move throwing members makes default construction throwing");
static_assert(!__is_nothrow_constructible(DefaultedHolder), "a defaulted constructor of a throwing member type makes default construction throwing");

int main() {
	ThrowingHolder value{};
	return value.tag == 0 ? 0 : 1;
}
