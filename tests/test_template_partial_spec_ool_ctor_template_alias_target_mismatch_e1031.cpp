struct PartialCtorTemplateAliasMismatchTag {
	template<class U>
	struct AddPtr {
		using type = U*;
	};
};

template<class T>
struct PartialCtorTemplateAliasMismatch;

template<class T>
struct PartialCtorTemplateAliasMismatch<T*> {
	template<class U>
	PartialCtorTemplateAliasMismatch(typename T::template AddPtr<int>::type value, U seed);
};

template<class T>
template<class U>
PartialCtorTemplateAliasMismatch<T*>::PartialCtorTemplateAliasMismatch(
	typename T::template AddPtr<long>::type value,
	U seed) {
	(void)value;
	(void)seed;
}

int main() {
	int value = 0;
	PartialCtorTemplateAliasMismatch<PartialCtorTemplateAliasMismatchTag*> instance(&value, 0);
	(void)instance;
	return 0;
}
