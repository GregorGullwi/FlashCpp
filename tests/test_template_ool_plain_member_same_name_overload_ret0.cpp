// Regression: primary-template plain out-of-line member attachment must resolve
// the source declaration first, then attach through source-member -> stub
// identity so same-name/same-arity overloads do not swap or drop definitions.
template<class T>
struct OverloadBox {
	int pick(T*);
	int pick(long*);
};

template<class T>
int OverloadBox<T>::pick(T*) {
	return 2;
}

int main() {
	OverloadBox<int> box;
	int value = 0;
	if (box.pick(&value) != 2) {
		return 1;
	}
	return 0;
}

template<class T>
int OverloadBox<T>::pick(long*) {
	return 1;
}
