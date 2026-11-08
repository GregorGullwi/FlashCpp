struct EllipsisOnly {
	EllipsisOnly(...) noexcept {}
};

struct NamedEllipsis {
	NamedEllipsis(int, ...) noexcept {}
};

struct FixedAndEllipsis {
	FixedAndEllipsis(long) noexcept(false) {}
	FixedAndEllipsis(...) noexcept(true) {}
};

struct UndeducedVariadicTemplate {
	template<typename T>
	UndeducedVariadicTemplate(...) noexcept {}
};

template<typename T>
struct ClassTemplatedEllipsis {
	ClassTemplatedEllipsis(T, ...) noexcept {}
};

static_assert(__is_constructible(EllipsisOnly));
static_assert(__is_constructible(EllipsisOnly, int, double));
static_assert(__is_nothrow_constructible(EllipsisOnly, int, double));
static_assert(!__is_constructible(NamedEllipsis));
static_assert(__is_constructible(NamedEllipsis, int));
static_assert(__is_constructible(NamedEllipsis, int, double, char));
static_assert(__is_nothrow_constructible(NamedEllipsis, int, double));
static_assert(!__is_nothrow_constructible(FixedAndEllipsis, int));
static_assert(!__is_constructible(UndeducedVariadicTemplate, int));
static_assert(__is_constructible(ClassTemplatedEllipsis<int>, int, double));
static_assert(__is_nothrow_constructible(ClassTemplatedEllipsis<int>, int, double));
static_assert(!__is_constructible(EllipsisOnly, void));

int main() {
	return __is_constructible(EllipsisOnly, int, double) && __is_constructible(NamedEllipsis, int, double) &&
		__is_constructible(ClassTemplatedEllipsis<int>, int, double) ? 0 : 1;
}
