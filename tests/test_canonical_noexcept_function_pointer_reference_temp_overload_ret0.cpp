using ThrowingFunctionPointer = int (*)(int);
using NoexceptFunctionPointer = int (*)(int) noexcept;

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

ThrowingSelection choose(ThrowingFunctionPointer const&);
NoexceptSelection choose(NoexceptFunctionPointer const&);
ThrowingSelection chooseThrowingOnly(ThrowingFunctionPointer const&);

NoexceptFunctionPointer noexceptPointer;

static_assert(SameType<decltype(choose(noexceptPointer)), NoexceptSelection>::value);
static_assert(SameType<decltype(chooseThrowingOnly(noexceptPointer)), ThrowingSelection>::value);

int main() {
	return 0;
}
