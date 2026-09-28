#if defined(_MSC_VER)
template <class Type>
struct ValueBox {
	using type = typename Type::type;
};

template <class Type>
struct LeafBox {
	using type = Type;
};

// Sixteen nested owners stress iterative canonical import without making
// native call depth depend on template argument depth.

struct NoexceptSelection {
	char marker[1];
};

struct ThrowingSelection {
	char marker[2];
};

template <class Type>
struct Callable {
	int run() noexcept(sizeof(typename Type::type) == sizeof(int)) {
		return 0;
	}
};

using IntCallable = Callable<
	ValueBox<ValueBox<ValueBox<ValueBox<
	ValueBox<ValueBox<ValueBox<ValueBox<
	ValueBox<ValueBox<ValueBox<ValueBox<
	ValueBox<ValueBox<ValueBox<ValueBox<LeafBox<int>>>>>>>>>>>>>>>>>>;
using CharCallable = Callable<
	ValueBox<ValueBox<ValueBox<ValueBox<
	ValueBox<ValueBox<ValueBox<ValueBox<
	ValueBox<ValueBox<ValueBox<ValueBox<
	ValueBox<ValueBox<ValueBox<ValueBox<LeafBox<char>>>>>>>>>>>>>>>>>>;

NoexceptSelection choose(int (IntCallable::*)() noexcept) {
	return {};
}

ThrowingSelection choose(int (IntCallable::*)()) {
	return {};
}

NoexceptSelection choose(int (CharCallable::*)() noexcept) {
	return {};
}

ThrowingSelection choose(int (CharCallable::*)()) {
	return {};
}

template <class Owner>
using RunPointer0 = decltype(&Owner::run);

template <class Owner>
using RunPointer1 = RunPointer0<Owner>;

template <class Owner>
using RunPointer2 = RunPointer1<Owner>;

template <class Owner>
using RunPointer3 = RunPointer2<Owner>;

template <class Owner>
auto selectViaAliasChain() {
	return choose(static_cast<RunPointer3<Owner>>(&Owner::run));
}

constexpr bool selects_noexcept_for_int =
	sizeof(decltype(selectViaAliasChain<IntCallable>())) ==
	sizeof(NoexceptSelection);
constexpr bool selects_throwing_for_char =
	sizeof(decltype(selectViaAliasChain<CharCallable>())) ==
	sizeof(ThrowingSelection);

static_assert(selects_noexcept_for_int);
static_assert(selects_throwing_for_char);

int main() {
	return selects_noexcept_for_int && selects_throwing_for_char ? 0 : 1;
}
#else
// Itanium member-function-pointer parameter mangling is not implemented yet.
int main() {
	return 0;
}
#endif
