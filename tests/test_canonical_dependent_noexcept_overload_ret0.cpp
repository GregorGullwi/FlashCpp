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

ThrowingSelection choose(ThrowingFunctionPointer);
NoexceptSelection choose(NoexceptFunctionPointer);
ThrowingSelection chooseThrowingOnly(ThrowingFunctionPointer);

template <bool IsNoexcept>
int target() noexcept(IsNoexcept) {
	return 0;
}

static_assert(SameType<decltype(choose(&target<true>)), NoexceptSelection>::value);
static_assert(SameType<decltype(choose(&target<false>)), ThrowingSelection>::value);
static_assert(SameType<decltype(chooseThrowingOnly(&target<true>)), ThrowingSelection>::value);

int main() {
	return 0;
}
