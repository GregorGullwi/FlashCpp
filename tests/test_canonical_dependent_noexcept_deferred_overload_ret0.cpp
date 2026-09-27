using ThrowingFunctionPointer = int (*)();
using NoexceptFunctionPointer = int (*)() noexcept;

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

ThrowingSelection choose(ThrowingFunctionPointer) {
	return {};
}

NoexceptSelection choose(NoexceptFunctionPointer) {
	return {};
}

ThrowingSelection chooseThrowingOnly(ThrowingFunctionPointer) {
	return {};
}

template <bool IsNoexcept>
int target() noexcept(IsNoexcept) {
	return 0;
}

template <bool IsNoexcept>
auto chooseTarget() {
	return choose(&target<IsNoexcept>);
}

template <bool IsNoexcept>
auto chooseThrowingTarget() {
	return chooseThrowingOnly(&target<IsNoexcept>);
}

static_assert(SameType<decltype(chooseTarget<true>()), NoexceptSelection>::value);
static_assert(SameType<decltype(chooseTarget<false>()), ThrowingSelection>::value);
static_assert(SameType<decltype(chooseThrowingTarget<true>()), ThrowingSelection>::value);
static_assert(SameType<decltype(chooseThrowingTarget<false>()), ThrowingSelection>::value);

int main() {
	return 0;
}
