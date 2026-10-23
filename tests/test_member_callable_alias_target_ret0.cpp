template<class T>
struct Holder {
	template<class R>
	using F = R(T);

	template<class R>
	using Ptr = R (*)(T);

	template<class R>
	using Ref = R (&)(T);

	template<class R>
	using Nx = R (*)(T) noexcept;
};

template<class T>
struct Shadowed {
	template<class T>
	using Same = T (T);
};

template<class R, class A>
using Callback = R (*)(A) noexcept;

template<class T>
struct Routed {
	template<class R>
	using F = Callback<R, T>;
};

static_assert(__is_same(Holder<int>::F<int>, int(int)));
static_assert(__is_same(Holder<int>::Ptr<int>, int (*)(int)));
static_assert(__is_same(Holder<int>::Ref<int>, int (&)(int)));
static_assert(__is_same(Holder<int>::Nx<int>, int (*)(int) noexcept));
static_assert(__is_same(Shadowed<int>::Same<long>, long(long)));
static_assert(__is_same(Routed<short>::F<long>, long (*)(short) noexcept));

int plus_one(int value) { return value + 1; }
long times_two(long value) { return value * 2; }
int plus_one_noexcept(int value) noexcept { return value + 1; }

int main() {
	Holder<int>::F<int>* direct = &plus_one;
	Holder<int>::Ptr<int> pointer = &plus_one;
	Holder<int>::Ref<int> reference = plus_one;
	Holder<int>::Nx<int> noexcept_pointer = &plus_one_noexcept;
	Shadowed<int>::Same<long>* shadowed = &times_two;
	if (direct(1) != 2) return 1;
	if (pointer(2) != 3) return 2;
	if (reference(3) != 4) return 3;
	if (noexcept_pointer(4) != 5) return 4;
	if (shadowed(5) != 10) return 5;
	return 0;
}
