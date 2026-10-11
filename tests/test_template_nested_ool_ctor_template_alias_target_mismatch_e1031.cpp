struct NestedCtorTemplateAliasMismatchTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct NestedCtorTemplateAliasMismatchOuter {
	struct Inner {
		template<class U>
		Inner(typename T::template AddPtr<int>::type value, U seed);
	};
};

template<class T>
template<class U>
NestedCtorTemplateAliasMismatchOuter<T>::Inner::Inner(
	typename T::template AddPtr<long>::type value,
	U seed) {
	(void)value;
	(void)seed;
}

int main() {
	int value = 0;
	NestedCtorTemplateAliasMismatchOuter<NestedCtorTemplateAliasMismatchTag>::Inner instance(
		&value,
		0);
	(void)instance;
	return 0;
}
