// Regression test: nested class-template out-of-line operator() definitions must
// register under the full owner name rather than only the innermost class.

template<typename T>
struct Outer {
	struct Inner {
		template<typename U>
		T operator()(U value) const;
	};
};

template<typename T>
template<typename U>
T Outer<T>::Inner::operator()(U value) const {
	return static_cast<T>(value) + 40;
}

int main() {
	Outer<int>::Inner add;
	int result = add(2);
	return result - 42;
}
