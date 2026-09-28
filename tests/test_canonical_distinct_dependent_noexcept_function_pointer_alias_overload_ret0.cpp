template <class Type>
int source() noexcept(sizeof(Type) > 1) {
	return 0;
}

template <class Type>
int distinctNoexceptTarget() noexcept(sizeof(Type) >= 1) {
	return 0;
}

template <class Type>
int throwingTarget() {
	return 0;
}

template <class Type>
int depthSource() noexcept(sizeof(Type) > 1) {
	return 0;
}

template <class Type>
using FunctionAddress = decltype(&depthSource<Type>);

template <class Type, int Depth>
struct FunctionAddressChain {
	using type = FunctionAddress<
		typename FunctionAddressChain<Type, Depth - 1>::type>;
};

template <class Type>
struct FunctionAddressChain<Type, 0> {
	using type = Type;
};

using DeepFunctionAddress =
	typename FunctionAddressChain<int, 32>::type;

template <class Type>
using DistinctNoexceptTarget = decltype(&distinctNoexceptTarget<Type>);

template <class Type>
using ThrowingTarget = decltype(&throwingTarget<Type>);

template <class Type>
struct TypeTag {};

template <class Type>
struct NoexceptSelection {};

template <class Type>
struct ThrowingSelection {};

template <class Type>
NoexceptSelection<Type> choose(
	DistinctNoexceptTarget<Type>, TypeTag<Type>) {
	return {};
}

template <class Type>
ThrowingSelection<Type> choose(ThrowingTarget<Type>, TypeTag<Type>) {
	return {};
}

template <class Type>
auto selected() {
	return choose(&source<Type>, TypeTag<Type>{});
}

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

static_assert(sizeof(DeepFunctionAddress) == sizeof(void*));

static_assert(SameType<decltype(selected<int>()), NoexceptSelection<int>>::value);
static_assert(SameType<decltype(selected<char>()), ThrowingSelection<char>>::value);

int main() {
	return 0;
}
