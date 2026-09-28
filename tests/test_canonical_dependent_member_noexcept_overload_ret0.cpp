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

NoexceptSelection choose(int (Callable::*)());
NoexceptSelection choose(int (Callable::*)() noexcept);
ThrowingSelection chooseThrowingOnly(int (Callable::*)());

static_assert(SameType<
	decltype(choose(&Callable::run<true>)), NoexceptSelection>::value);
static_assert(SameType<
	decltype(chooseThrowingOnly(&Callable::run<true>)), ThrowingSelection>::value);

int main() {
	return 0;
}
