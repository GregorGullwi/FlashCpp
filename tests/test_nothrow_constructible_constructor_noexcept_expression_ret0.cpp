// A constructor's exception specification is the effective value of its
// noexcept operand, not merely the presence of the noexcept keyword. Before
// this was fixed, a constructor declared `noexcept(false)` was recorded as
// noexcept, so a class whose default member initializer calls it was reported
// nothrow-constructible. The constant-expression operand and the old-style
// throw() spelling are covered too.
struct ThrowingCtor {
	ThrowingCtor(int) noexcept(false) {}
	ThrowingCtor() noexcept(false) {}
};

struct SafeCtor {
	SafeCtor(int) noexcept(true) {}
	SafeCtor() noexcept(true) {}
};

struct NothrowKeywordCtor {
	NothrowKeywordCtor(int) noexcept {}
	NothrowKeywordCtor() noexcept {}
};

struct ThrowStyleCtor {
	ThrowStyleCtor(int) throw() {}
	ThrowStyleCtor() throw() {}
};

struct TrueConstantCtor {
	TrueConstantCtor(int) noexcept(sizeof(int) == 4) {}
	TrueConstantCtor() noexcept(sizeof(int) == 4) {}
};

struct FalseConstantCtor {
	FalseConstantCtor(int) noexcept(sizeof(int) == 0) {}
	FalseConstantCtor() noexcept(sizeof(int) == 0) {}
};

// A native member is mixed in; only the class-type member initializer can throw.
struct ThrowingInitializer {
	int tag;
	ThrowingCtor member{1};
};

struct SafeInitializer {
	int tag;
	SafeCtor member{1};
};

struct NothrowKeywordInitializer {
	NothrowKeywordCtor member{1};
};

struct ThrowStyleInitializer {
	ThrowStyleCtor member{1};
};

struct TrueConstantInitializer {
	TrueConstantCtor member{1};
};

struct FalseConstantInitializer {
	FalseConstantCtor member{1};
};

// The same constructor specification applies to array elements.
struct ThrowingArrayInitializer {
	ThrowingCtor members[2] = {ThrowingCtor(1), ThrowingCtor(2)};
};

static_assert(!__is_nothrow_constructible(ThrowingInitializer), "a noexcept(false) constructor makes default construction throwing");
static_assert(__is_nothrow_constructible(SafeInitializer), "a noexcept(true) constructor keeps default construction non-throwing");
static_assert(__is_nothrow_constructible(NothrowKeywordInitializer), "a bare noexcept constructor is non-throwing");
static_assert(__is_nothrow_constructible(ThrowStyleInitializer), "a throw() constructor is non-throwing");
static_assert(__is_nothrow_constructible(TrueConstantInitializer), "a true constant noexcept operand is non-throwing");
static_assert(!__is_nothrow_constructible(FalseConstantInitializer), "a false constant noexcept operand is throwing");
static_assert(!__is_nothrow_constructible(ThrowingArrayInitializer), "a noexcept(false) array element makes default construction throwing");

int main() {
	ThrowingInitializer value{};
	return value.tag == 0 ? 0 : 1;
}
