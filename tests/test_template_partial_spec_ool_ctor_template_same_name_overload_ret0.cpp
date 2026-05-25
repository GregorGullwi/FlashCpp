// Regression: partial-specialization out-of-line constructor-template attachment
// must resolve source constructor declarations first and map source-member ->
// instantiated-stub identity so same-name constructor-template overloads attach
// to the correct instantiated declaration.
template<class T>
struct PartialCtorTemplateOverload;

template<class T>
struct PartialCtorTemplateOverload<T*> {
	int which;

	template<class U>
	PartialCtorTemplateOverload(T*, U);

	template<class U>
	PartialCtorTemplateOverload(long*, U);
};

template<class T>
template<class U>
PartialCtorTemplateOverload<T*>::PartialCtorTemplateOverload(T*, U)
	: which(0) {
	which = 2;
}

template<class T>
template<class U>
PartialCtorTemplateOverload<T*>::PartialCtorTemplateOverload(long*, U)
	: which(0) {
	which = 1;
}

int main() {
	int value = 0;
	long long_value = 0;

	PartialCtorTemplateOverload<int*> from_t_ptr(&value, 0);
	if (from_t_ptr.which != 2) {
		return 1;
	}

	PartialCtorTemplateOverload<int*> from_long_ptr(&long_value, 0);
	if (from_long_ptr.which != 1) {
		return 2;
	}

	return 0;
}
