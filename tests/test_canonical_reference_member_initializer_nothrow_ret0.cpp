// A reference member's initializer can construct a temporary even though
// binding the reference itself does not throw. The temporary's constructor must
// decide the enclosing class's nothrow default construction.

struct ThrowingTemporary {
	ThrowingTemporary() noexcept(false) {}
};

struct NothrowTemporary {
	NothrowTemporary() noexcept {}
};

struct ThrowingReference {
	const ThrowingTemporary& reference = ThrowingTemporary{};
};

struct NothrowReference {
	const NothrowTemporary& reference = NothrowTemporary{};
};

static_assert(__is_constructible(ThrowingReference));
static_assert(!__is_trivially_constructible(ThrowingReference));
static_assert(!__is_nothrow_constructible(ThrowingReference));

static_assert(__is_constructible(NothrowReference));
static_assert(!__is_trivially_constructible(NothrowReference));
static_assert(__is_nothrow_constructible(NothrowReference));

int main() {
	return 0;
}
