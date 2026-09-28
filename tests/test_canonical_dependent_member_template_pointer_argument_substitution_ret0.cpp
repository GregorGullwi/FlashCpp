#if defined(_MSC_VER)
template <class Owner>
struct Callable {
	template <class Parameter>
	int run(Parameter) noexcept(
		sizeof(Owner) == sizeof(int) && sizeof(Parameter) == sizeof(int)) {
		return 0;
	}
};

template <class Type>
struct OwnerBox {
	using type = Callable<Type>;
};

template <class Type>
using Owner = typename OwnerBox<Type>::type;

template <class Type> using OwnerLayer0 = Owner<Type>;
template <class Type> using OwnerLayer1 = OwnerLayer0<Type>;
template <class Type> using OwnerLayer2 = OwnerLayer1<Type>;
template <class Type> using OwnerLayer3 = OwnerLayer2<Type>;
template <class Type> using OwnerLayer4 = OwnerLayer3<Type>;
template <class Type> using OwnerLayer5 = OwnerLayer4<Type>;
template <class Type> using OwnerLayer6 = OwnerLayer5<Type>;
template <class Type> using OwnerLayer7 = OwnerLayer6<Type>;
template <class Type> using OwnerLayer8 = OwnerLayer7<Type>;
template <class Type> using OwnerLayer9 = OwnerLayer8<Type>;
template <class Type> using OwnerLayer10 = OwnerLayer9<Type>;
template <class Type> using OwnerLayer11 = OwnerLayer10<Type>;
template <class Type> using OwnerLayer12 = OwnerLayer11<Type>;
template <class Type> using OwnerLayer13 = OwnerLayer12<Type>;
template <class Type> using OwnerLayer14 = OwnerLayer13<Type>;
template <class Type> using DeepOwner = OwnerLayer14<Type>;

template <class Type>
using RunPointer = decltype(&DeepOwner<Type>::template run<Type>);

using IntCallable = Callable<int>;
using CharCallable = Callable<char>;

struct NoexceptSelection {
	char marker[1];
};

struct ThrowingSelection {
	char marker[2];
};

NoexceptSelection choose(int (IntCallable::*)(int) noexcept) { return {}; }
ThrowingSelection choose(int (IntCallable::*)(int)) { return {}; }
NoexceptSelection choose(int (CharCallable::*)(char) noexcept) { return {}; }
ThrowingSelection choose(int (CharCallable::*)(char)) { return {}; }

template <class Type>
	auto selectDependentMemberTemplateAddress() {
	return choose(static_cast<RunPointer<Type>>(
		&DeepOwner<Type>::template run<Type>));
}

template <class Type>
auto selectDirectDependentMemberTemplateAddress() {
	return choose(static_cast<decltype(&DeepOwner<Type>::template run<Type>)>(
		&DeepOwner<Type>::template run<Type>));
}

static_assert(sizeof(decltype(selectDependentMemberTemplateAddress<int>())) ==
	sizeof(NoexceptSelection));
static_assert(sizeof(decltype(selectDependentMemberTemplateAddress<char>())) ==
	sizeof(ThrowingSelection));
static_assert(sizeof(decltype(selectDirectDependentMemberTemplateAddress<int>())) ==
	sizeof(NoexceptSelection));
static_assert(sizeof(decltype(selectDirectDependentMemberTemplateAddress<char>())) ==
	sizeof(ThrowingSelection));
int main() {
	return sizeof(decltype(selectDependentMemberTemplateAddress<int>())) ==
			sizeof(NoexceptSelection) &&
		sizeof(decltype(selectDependentMemberTemplateAddress<char>())) ==
			sizeof(ThrowingSelection) &&
		sizeof(decltype(selectDirectDependentMemberTemplateAddress<int>())) ==
			sizeof(NoexceptSelection) &&
		sizeof(decltype(selectDirectDependentMemberTemplateAddress<char>())) ==
			sizeof(ThrowingSelection)
		? 0
		: 1;
}
#else
// The Itanium mangler does not yet support member-function-pointer parameter types.
int main() {
	return 0;
}
#endif
