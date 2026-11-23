// Pointee cv distinguishes two function-template overloads: `const int*` and
// `int*` are different parameter types, so both templates must stay viable and
// each call must select the exact match.
static_assert(!__is_same(const int*, int*), "pointee cv is part of the type");

template <class T>
struct Box {
	T value;
};

template <class T>
int choose(T tag, const int* value) {
	return 11;
}

template <class T>
int choose(T tag, int* value) {
	return 22;
}

// Specializations are distinct types with the same template name, so the shape
// compare must use their identity rather than the shared spelling.
template <class T>
int tag_of(T tag, Box<int> box) {
	return 33;
}

template <class T>
int tag_of(T tag, Box<double> box) {
	return 44;
}

int main() {
	int value = 7;
	const int* read_only = &value;
	int* writable = &value;
	if (choose(0, read_only) != 11) {
		return 1;
	}
	if (choose(0, writable) != 22) {
		return 2;
	}
	Box<int> ints{1};
	Box<double> doubles{2.0};
	if (tag_of(0, ints) != 33) {
		return 3;
	}
	if (tag_of(0, doubles) != 44) {
		return 4;
	}
	return 42;
}
