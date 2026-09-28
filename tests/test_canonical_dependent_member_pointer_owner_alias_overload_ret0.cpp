struct NoexceptSelection {
	char marker[1];
};

struct ThrowingSelection {
	char marker[2];
};

template <class Type>
struct Callable {
	int run() noexcept(sizeof(Type) == sizeof(int)) {
		return 0;
	}
};

using IntCallable = Callable<int>;
using CharCallable = Callable<char>;

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

template <class Type>
using RunPointer = decltype(&Callable<Type>::run);

template <class Type>
auto chooseViaDependentOwnerAlias() {
	return choose(
		static_cast<RunPointer<Type>>(&Callable<Type>::run));
}

static_assert(sizeof(decltype(chooseViaDependentOwnerAlias<int>())) ==
	sizeof(NoexceptSelection));
static_assert(sizeof(decltype(chooseViaDependentOwnerAlias<char>())) ==
	sizeof(ThrowingSelection));

int main() {
	return static_cast<int>(
		sizeof(decltype(chooseViaDependentOwnerAlias<int>())) !=
			sizeof(NoexceptSelection) ||
		sizeof(decltype(chooseViaDependentOwnerAlias<char>())) !=
			sizeof(ThrowingSelection));
}
