// A non-template class constructor template must participate in
// __is_constructible even though it is not an ordinary constructor overload.
struct EllipsisConstructor {
	template<class T>
	EllipsisConstructor(T, ...);
};

struct NothrowConstructor {
	template<class T>
	NothrowConstructor(T) noexcept;
};

struct ThrowingConstructor {
	template<class T>
	ThrowingConstructor(T) noexcept(false);
};

static_assert(__is_constructible(EllipsisConstructor, int, double));
static_assert(!__is_constructible(EllipsisConstructor));
static_assert(__is_nothrow_constructible(NothrowConstructor, int));
static_assert(!__is_nothrow_constructible(ThrowingConstructor, int));

int main() {
	if (!__is_constructible(EllipsisConstructor, int, double)) return 1;
	if (__is_constructible(EllipsisConstructor)) return 2;
	if (!__is_nothrow_constructible(NothrowConstructor, int)) return 3;
	if (__is_nothrow_constructible(ThrowingConstructor, int)) return 4;
	return 0;
}
