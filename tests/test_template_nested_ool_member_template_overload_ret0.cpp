// Regression: nested struct template member function overloads with the same
// name and same inner template parameter count but different non-template
// parameter types must each bind to the correct OOL body.
// Previously, the name+arity-only scan caused both overloads to receive the
// first OOL body and the second OOL body was silently dropped.
template<class T>
struct Outer {
	struct Inner {
		template<class U>
		int process(U, int);

		template<class U>
		int process(U, double);
	};
};

template<class T>
template<class U>
int Outer<T>::Inner::process(U, int) {
	return 10;
}

template<class T>
template<class U>
int Outer<T>::Inner::process(U, double) {
	return 20;
}

int main() {
	Outer<int>::Inner obj;

	if (obj.process(0, 1) != 10) {
		return 1;
	}

	if (obj.process(0, 1.0) != 20) {
		return 2;
	}

	return 0;
}
