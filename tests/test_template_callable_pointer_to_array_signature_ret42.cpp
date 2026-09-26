template <typename T>
using PointerToArray = T (*)[3];

template <typename T>
struct Callable {
	using Callback = void (*)(T);
	Callback callback;
};

using ExpectedCallback = void (*)(PointerToArray<int>);
using DifferentCallback = void (*)(PointerToArray<long long>);

static_assert(__is_same(
	decltype(Callable<PointerToArray<int>>::callback), ExpectedCallback));
static_assert(!__is_same(
	decltype(Callable<PointerToArray<int>>::callback), DifferentCallback));

int main() {
	return __is_same(
			   decltype(Callable<PointerToArray<int>>::callback), ExpectedCallback) &&
			   !__is_same(
				   decltype(Callable<PointerToArray<int>>::callback), DifferentCallback)
		? 42
		: 1;
}
