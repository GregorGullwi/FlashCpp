#if defined(_MSC_VER)
struct NoexceptSelection {
	char marker[1];
};

struct ThrowingSelection {
	char marker[2];
};

template <class Owner>
struct Callable {
	template <class Parameter>
	int run(Parameter) noexcept(
		sizeof(Owner) == sizeof(int) && sizeof(Parameter) == sizeof(int)) {
		return 0;
	}
};

using IntCallable = Callable<int>;
using CharCallable = Callable<char>;

NoexceptSelection choose(int (IntCallable::*)(int) noexcept) {
	return {};
}

ThrowingSelection choose(int (IntCallable::*)(int)) {
	return {};
}

NoexceptSelection choose(int (CharCallable::*)(int) noexcept) {
	return {};
}

ThrowingSelection choose(int (CharCallable::*)(int)) {
	return {};
}

template <class Owner>
using ExplicitRunPointer = decltype(&Callable<Owner>::template run<int>);

template <class Owner>
auto selectExplicitMemberTemplateAddress() {
	return choose(static_cast<ExplicitRunPointer<Owner>>(
		&Callable<Owner>::template run<int>));
}

static_assert(sizeof(decltype(selectExplicitMemberTemplateAddress<int>())) ==
	sizeof(NoexceptSelection));
static_assert(sizeof(decltype(selectExplicitMemberTemplateAddress<char>())) ==
	sizeof(ThrowingSelection));

int main() {
	return static_cast<int>(
		sizeof(decltype(selectExplicitMemberTemplateAddress<int>())) !=
			sizeof(NoexceptSelection) ||
		sizeof(decltype(selectExplicitMemberTemplateAddress<char>())) !=
			sizeof(ThrowingSelection));
}
#else
// The Itanium mangler does not yet support member-function-pointer parameter
// types. Keep this source-level overload regression on the MSVC ABI.
int main() {
	return 0;
}
#endif
