// Regression: primary-template out-of-line constructor-template attachment must
// resolve source constructor declarations first and replay source-member ->
// instantiated-stub identity so same-name constructor-template overloads attach
// to the correct instantiated declaration.
template<class T>
struct PrimaryCtorTemplateOverload {
	int which;

	template<class U>
	PrimaryCtorTemplateOverload(T*, U);

	template<class U>
	PrimaryCtorTemplateOverload(long*, U);
};

template<class T>
template<class U>
PrimaryCtorTemplateOverload<T>::PrimaryCtorTemplateOverload(T*, U)
	: which(0) {
	which = 2;
}

template<class T>
template<class U>
PrimaryCtorTemplateOverload<T>::PrimaryCtorTemplateOverload(long*, U)
	: which(0) {
	which = 1;
}

int main() {
	int value = 0;
	long long_value = 0;

	PrimaryCtorTemplateOverload<int> from_t_ptr(&value, 0);
	if (from_t_ptr.which != 2) {
		return 1;
	}

	PrimaryCtorTemplateOverload<int> from_long_ptr(&long_value, 0);
	if (from_long_ptr.which != 1) {
		return 2;
	}

	return 0;
}
