// A selected scalar-to-class parameter conversion must propagate the nested
// converting constructor's exception specification into the outer nothrow
// query, while leaving plain constructibility unchanged.

struct NoexceptTarget {
	NoexceptTarget(int) noexcept {}
};

struct ThrowingTarget {
	ThrowingTarget(int) noexcept(false) {}
};

struct NoexceptOuter {
	NoexceptOuter(NoexceptTarget) noexcept {}
};

struct ThrowingOuter {
	ThrowingOuter(ThrowingTarget) noexcept {}
};

struct RemovingOuter {
	RemovingOuter(NoexceptTarget) noexcept(false) {}
};

static_assert(__is_nothrow_constructible(NoexceptTarget, int));
static_assert(!__is_nothrow_constructible(ThrowingTarget, int));
static_assert(__is_constructible(ThrowingOuter, int));
static_assert(__is_nothrow_constructible(NoexceptOuter, int));
static_assert(!__is_nothrow_constructible(ThrowingOuter, int));
static_assert(!__is_nothrow_constructible(RemovingOuter, int));

int main() {
	return 0;
}
