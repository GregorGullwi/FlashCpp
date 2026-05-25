// Regression: nested out-of-line constructor-template attachment must resolve
// source constructor declarations first and replay through source-member identity
// so same-name constructor-template overloads bind to the correct stub.
template<class T>
struct Outer {
	struct Inner {
		int which;

		template<class U>
		Inner(T*, U);

		template<class U>
		Inner(long*, U);
	};
};

template<class T>
template<class U>
Outer<T>::Inner::Inner(T*, U)
	: which(0) {
	which = 2;
}

template<class T>
template<class U>
Outer<T>::Inner::Inner(long*, U)
	: which(0) {
	which = 1;
}

int main() {
	int value = 0;
	long long_value = 0;

	Outer<int>::Inner from_t_ptr(&value, 0);
	if (from_t_ptr.which != 2) {
		return 1;
	}

	Outer<int>::Inner from_long_ptr(&long_value, 0);
	if (from_long_ptr.which != 1) {
		return 2;
	}

	return 0;
}
