template <bool IsNoexcept>
struct NoexceptAliasHolder {
	using CallbackAlias = int (*)() noexcept(IsNoexcept);
	CallbackAlias function;
};

struct ThrowingHolder {
	int (*function)();
};

static_assert(__is_same(
	decltype(NoexceptAliasHolder<false>::function),
	decltype(ThrowingHolder::function)));

int main() {
	return __is_same(
			   decltype(NoexceptAliasHolder<false>::function),
			   decltype(ThrowingHolder::function))
		? 42
		: 1;
}
