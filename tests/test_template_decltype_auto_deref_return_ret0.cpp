// Regression: selecting a function-template specialization with a deduced
// return type must materialize its body before semantic analysis queries the
// call's type and value category. For decltype(auto), dereferencing a pointer
// preserves the lvalue category and therefore returns T&.
struct Record {
	int small;
	long long wide;
};

template <class T>
decltype(auto) dereference(T* pointer) {
	return *pointer;
}

int main() {
	int small = 3;
	long long wide = 41;
	Record record{5, 19};

	dereference(&small) = 7;
	dereference(&wide) = 42;
	dereference(&record).small = 11;
	dereference(&record).wide = 23;

	return small == 7 && wide == 42 &&
		record.small == 11 && record.wide == 23 ? 0 : 1;
}
