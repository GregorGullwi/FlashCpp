// Regression: the Modular build must preserve initialized constructor-template
// metadata while partial-specialization out-of-line definitions are copied and
// materialized. Source declarations must map to the correct instantiated stub
// when same-name constructor-template overloads coexist.
struct Marker {
	int value;
};

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

	short short_value = 3;
	PartialCtorTemplateOverload<int*> from_t_ptr_short(&value, short_value);
	if (from_t_ptr_short.which != 2) {
		return 3;
	}

	Marker marker{4};
	PartialCtorTemplateOverload<int*> from_long_ptr_struct(&long_value, marker);
	if (from_long_ptr_struct.which != 1) {
		return 4;
	}

	return 0;
}
