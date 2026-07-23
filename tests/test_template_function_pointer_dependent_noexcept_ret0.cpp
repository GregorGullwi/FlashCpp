template <bool IsNoexcept>
struct Callback {
	int (*function)() noexcept(IsNoexcept);
};

struct NoexceptFunctionHolder {
	int (*function)() noexcept;
};

struct ThrowingFunctionHolder {
	int (*function)();
};

static_assert(__is_same(
	decltype(Callback<true>::function),
	decltype(NoexceptFunctionHolder::function)));
static_assert(__is_same(
	decltype(Callback<false>::function),
	decltype(ThrowingFunctionHolder::function)));
static_assert(!__is_same(
	decltype(Callback<true>::function),
	decltype(Callback<false>::function)));

int main() {
	return __is_same(
			   decltype(Callback<true>::function),
			   decltype(NoexceptFunctionHolder::function)) &&
			   __is_same(
				   decltype(Callback<false>::function),
				   decltype(ThrowingFunctionHolder::function)) &&
			   !__is_same(
				   decltype(Callback<true>::function),
				   decltype(Callback<false>::function))
		? 0
		: 1;
}
