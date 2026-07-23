template <typename Value>
struct AliasHolder {
	using CallbackAlias = Value (*)();
	CallbackAlias function;
};

struct ConcreteHolder {
	int (*function)();
};

struct DifferentHolder {
	long long (*function)();
};

template <bool IsNoexcept>
struct NoexceptAliasHolder {
	using CallbackAlias = int (*)() noexcept(IsNoexcept);
	CallbackAlias function;
};

struct NoexceptHolder {
	int (*function)() noexcept;
};

struct ThrowingHolder {
	int (*function)();
};

static_assert(__is_same(
	decltype(AliasHolder<int>::function),
	decltype(ConcreteHolder::function)));
static_assert(!__is_same(
	decltype(AliasHolder<int>::function),
	decltype(DifferentHolder::function)));
static_assert(__is_same(
	decltype(NoexceptAliasHolder<true>::function),
	decltype(NoexceptHolder::function)));
static_assert(__is_same(
	decltype(NoexceptAliasHolder<false>::function),
	decltype(ThrowingHolder::function)));

int main() {
	return __is_same(
			   decltype(AliasHolder<int>::function),
			   decltype(ConcreteHolder::function)) &&
		   !__is_same(
			   decltype(AliasHolder<int>::function),
			   decltype(DifferentHolder::function)) &&
		   __is_same(
			   decltype(NoexceptAliasHolder<true>::function),
			   decltype(NoexceptHolder::function)) &&
		   __is_same(
			   decltype(NoexceptAliasHolder<false>::function),
			   decltype(ThrowingHolder::function))
		? 0
		: 1;
}
