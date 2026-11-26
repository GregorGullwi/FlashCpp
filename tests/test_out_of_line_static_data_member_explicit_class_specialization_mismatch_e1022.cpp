// An out-of-line array definition of a static member of an explicitly
// specialized class template must still reject a different array shape. It
// reports OutOfLineDefinitionMismatch (1022) instead of binding the definition
// to the specialization or silently truncating the bounds.

template <class T>
struct S {
	static int values[2];
};

template <>
struct S<int> {
	static int values[2];
};

int S<int>::values[3] = {1, 2, 3};

int main() {
	return S<int>::values[0];
}
