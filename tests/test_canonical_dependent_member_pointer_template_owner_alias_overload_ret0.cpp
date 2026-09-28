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

template <class Owner>
using RunPointer = decltype(&Owner::run);

template <class Owner>
auto chooseViaDependentOwnerAlias() {
	return choose(static_cast<RunPointer<Owner>>(&Owner::run));
}

static_assert(sizeof(decltype(chooseViaDependentOwnerAlias<IntCallable>())) ==
	sizeof(NoexceptSelection));
static_assert(sizeof(decltype(chooseViaDependentOwnerAlias<CharCallable>())) ==
	sizeof(ThrowingSelection));

int main() {
	return static_cast<int>(
		sizeof(decltype(chooseViaDependentOwnerAlias<IntCallable>())) !=
			sizeof(NoexceptSelection) ||
		sizeof(decltype(chooseViaDependentOwnerAlias<CharCallable>())) !=
			sizeof(ThrowingSelection));
}
