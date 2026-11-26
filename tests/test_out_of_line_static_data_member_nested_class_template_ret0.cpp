// An out-of-line definition of a static data member of a nested class template
// completes each nested instantiation's declaration. The initializers depend on
// sizeof so a definition that only parses, without attaching to the
// instantiations, leaves zero-initialized storage and fails the reads.

template <class T>
struct Outer {
	template <class U>
	struct Inner {
		static int data;
	};
};

template <class T>
template <class U>
int Outer<T>::Inner<U>::data = static_cast<int>(sizeof(T) * 10 + sizeof(U));

int main() {
	if (Outer<char>::Inner<int>::data != 14) {
		return 1;
	}
	if (Outer<long long>::Inner<short>::data != 82) {
		return 2;
	}
	if (Outer<double>::Inner<char>::data != 81) {
		return 3;
	}

	// Parenthesized assignment keeps the qualified chain an expression; the
	// unparenthesized form is a separate statement-vs-declaration ambiguity.
	(Outer<char>::Inner<int>::data) = 91;
	if (Outer<char>::Inner<int>::data != 91) {
		return 4;
	}
	if (Outer<long long>::Inner<short>::data != 82) {
		return 5;
	}
	return 0;
}
