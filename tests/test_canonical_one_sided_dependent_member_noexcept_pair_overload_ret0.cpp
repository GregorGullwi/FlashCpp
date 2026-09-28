template <class Type>
struct Callable {
	int source() noexcept(sizeof(Type) > 1) {
		return 0;
	}

	int noexceptTarget() noexcept {
		return 0;
	}

	int throwingTarget() {
		return 0;
	}
};

template <class Type>
using NoexceptTarget = decltype(&Callable<Type>::noexceptTarget);

template <class Type>
using ThrowingTarget = decltype(&Callable<Type>::throwingTarget);

template <class Type>
struct TypeTag {};

template <class Type>
struct NoexceptSelection {};

template <class Type>
struct ThrowingSelection {};

template <class Type>
NoexceptSelection<Type> choose(NoexceptTarget<Type>, TypeTag<Type>) {
	return {};
}

template <class Type>
ThrowingSelection<Type> choose(ThrowingTarget<Type>, TypeTag<Type>) {
	return {};
}

template <class Type>
auto selected() {
	return choose(&Callable<Type>::source, TypeTag<Type>{});
}

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

static_assert(SameType<decltype(selected<int>()), NoexceptSelection<int>>::value);
static_assert(SameType<decltype(selected<char>()), ThrowingSelection<char>>::value);

int main() {
	return 0;
}
