// Regression: primary-template out-of-line constructor attachment must resolve
// replay-first through source-member identity so same-name ctor overloads attach
// to the right instantiated declaration/body.
template<class T>
struct CtorOverloadBox {
	int which;
	CtorOverloadBox(T*);
	CtorOverloadBox(T**);
	CtorOverloadBox(long*);
};

template<class T>
CtorOverloadBox<T>::CtorOverloadBox(T*)
	: which(0) {
	which = 1;
}

template<class T>
CtorOverloadBox<T>::CtorOverloadBox(T**)
	: which(0) {
	which = 2;
}

template<class T>
CtorOverloadBox<T>::CtorOverloadBox(long*)
	: which(0) {
	which = 3;
}

int main() {
	int value = 7;
	int* ptr = &value;
	int** ptr_ptr = &ptr;
	long lvalue = 9;
	long* long_ptr = &lvalue;

	CtorOverloadBox<int> from_ptr(&value);
	if (from_ptr.which != 1) {
		return 1;
	}

	CtorOverloadBox<int> from_double_ptr(ptr_ptr);
	if (from_double_ptr.which != 2) {
		return 2;
	}

	CtorOverloadBox<int> from_long(long_ptr);
	if (from_long.which != 3) {
		return 3;
	}

	return 0;
}
