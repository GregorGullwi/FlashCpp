// Regression: out-of-line constructor definitions whose parameter types are
// swapped dependent member types must replay the body selected by source-member
// identity, not by name/arity or owner-only recovery.
template<class T>
struct DependentCtorSwapInner {
	int tag;

	DependentCtorSwapInner(typename T::first_type)
		: tag(10) {}

	DependentCtorSwapInner(typename T::second_type)
		: tag(20) {}
};

template<class T>
struct DependentCtorSwap {
	using first_type = typename T::first_type;
	using second_type = typename T::second_type;

	int tag;

	DependentCtorSwap(first_type);
	DependentCtorSwap(second_type);
};

template<class T>
DependentCtorSwap<T>::DependentCtorSwap(typename T::second_type value)
	: tag(2) {
	DependentCtorSwapInner<T> inner(value);
	tag += inner.tag;
}

template<class T>
DependentCtorSwap<T>::DependentCtorSwap(typename T::first_type value)
	: tag(1) {
	DependentCtorSwapInner<T> inner(value);
	tag += inner.tag;
}

struct CtorSwapArgs {
	using first_type = int;
	using second_type = long;
};

int main() {
	DependentCtorSwap<CtorSwapArgs> first(7);
	if (first.tag != 11) {
		return 1;
	}

	DependentCtorSwap<CtorSwapArgs> second(7L);
	if (second.tag != 22) {
		return 2;
	}

	return 0;
}
