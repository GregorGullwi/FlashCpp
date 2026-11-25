// A class-template out-of-line definition that gives a scalar in-class member
// an array declarator is a conflicting definition. It must report
// OutOfLineDefinitionMismatch (1022) rather than silently keeping the scalar
// shape or crashing while the array bound is parsed.

template <typename T>
struct S {
	static int data;
};

template <typename T>
int S<T>::data[2];

int main() {
	return S<int>::data[0];
}
