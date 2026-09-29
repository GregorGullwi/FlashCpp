// Each hop is a distinct alias-template specialization of a deferred decltype
// member-function-template address, so substitution must republish signature
// metadata through the whole chain.
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
using RunPointer0 = decltype(&Owner<Type>::template run<Type>);
template <class Type> using RunPointer1 = RunPointer0<Type>;
template <class Type> using RunPointer2 = RunPointer1<Type>;
template <class Type> using RunPointer3 = RunPointer2<Type>;
template <class Type> using RunPointer4 = RunPointer3<Type>;
template <class Type> using RunPointer5 = RunPointer4<Type>;
template <class Type> using RunPointer6 = RunPointer5<Type>;
template <class Type> using RunPointer7 = RunPointer6<Type>;
template <class Type> using RunPointer8 = RunPointer7<Type>;
template <class Type> using RunPointer9 = RunPointer8<Type>;
template <class Type> using RunPointer10 = RunPointer9<Type>;
template <class Type> using RunPointer11 = RunPointer10<Type>;
template <class Type> using RunPointer12 = RunPointer11<Type>;
template <class Type> using RunPointer13 = RunPointer12<Type>;
template <class Type> using RunPointer14 = RunPointer13<Type>;
template <class Type> using RunPointer15 = RunPointer14<Type>;
template <class Type> using RunPointer16 = RunPointer15<Type>;
template <class Type> using RunPointer17 = RunPointer16<Type>;
template <class Type> using RunPointer18 = RunPointer17<Type>;
template <class Type> using RunPointer19 = RunPointer18<Type>;
template <class Type> using RunPointer20 = RunPointer19<Type>;
template <class Type> using RunPointer21 = RunPointer20<Type>;
template <class Type> using RunPointer22 = RunPointer21<Type>;
template <class Type> using RunPointer23 = RunPointer22<Type>;
template <class Type> using RunPointer24 = RunPointer23<Type>;
template <class Type> using RunPointer25 = RunPointer24<Type>;
template <class Type> using RunPointer26 = RunPointer25<Type>;
template <class Type> using RunPointer27 = RunPointer26<Type>;
template <class Type> using RunPointer28 = RunPointer27<Type>;
template <class Type> using RunPointer29 = RunPointer28<Type>;
template <class Type> using RunPointer30 = RunPointer29<Type>;
template <class Type> using RunPointer31 = RunPointer30<Type>;
template <class Type> using DeepRunPointer = RunPointer31<Type>;

template <class Type>
auto selectViaDeepAliasTemplateChain() {
	return choose(static_cast<DeepRunPointer<Type>>(
		&Owner<Type>::template run<Type>));
}

static_assert(sizeof(decltype(selectViaDeepAliasTemplateChain<int>())) ==
	sizeof(NoexceptSelection));
static_assert(sizeof(decltype(selectViaDeepAliasTemplateChain<char>())) ==
	sizeof(ThrowingSelection));

int main() {
	return sizeof(decltype(selectViaDeepAliasTemplateChain<int>())) ==
			sizeof(NoexceptSelection) &&
		sizeof(decltype(selectViaDeepAliasTemplateChain<char>())) ==
			sizeof(ThrowingSelection)
		? 0
		: 1;
}
