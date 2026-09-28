struct Callable {
	template <bool IsNoexcept>
	int run() noexcept(IsNoexcept) {
		return 0;
	}
};

struct ThrowingSelection {};
struct NoexceptSelection {};

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

NoexceptSelection choose(int (Callable::*)() noexcept) {
	return {};
}

ThrowingSelection choose(int (Callable::*)()) {
	return {};
}

ThrowingSelection chooseThrowingOnly(int (Callable::*)()) {
	return {};
}

NoexceptSelection chooseNoexceptOnly(int (Callable::*)() noexcept) {
	return {};
}

template <bool IsNoexcept>
auto chooseMemberTarget() {
	return choose(&Callable::run<IsNoexcept>);
}

template <bool IsNoexcept>
auto chooseThrowingMemberTarget() {
	return chooseThrowingOnly(&Callable::run<IsNoexcept>);
}

static_assert(SameType<
	decltype(chooseMemberTarget<true>()), NoexceptSelection>::value);
static_assert(SameType<
	decltype(chooseMemberTarget<false>()), ThrowingSelection>::value);
static_assert(SameType<
	decltype(chooseThrowingMemberTarget<true>()), ThrowingSelection>::value);
static_assert(SameType<
	decltype(chooseThrowingMemberTarget<false>()), ThrowingSelection>::value);
static_assert(SameType<
	decltype(chooseNoexceptOnly(&Callable::run<true>)), NoexceptSelection>::value);
static_assert(SameType<
	decltype(choose(&Callable::run<true>)), NoexceptSelection>::value);
static_assert(SameType<
	decltype(choose(&Callable::run<false>)), ThrowingSelection>::value);

int main() {
	return 0;
}
