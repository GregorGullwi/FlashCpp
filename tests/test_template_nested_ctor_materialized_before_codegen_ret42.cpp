// Regression test: nested template constructors must be fully materialized in
// parser/sema before variable-declaration codegen consumes them. Exercises both
// paren-init and brace-init variable declarations without relying on a codegen
// fallback to instantiate the constructor body.

template<typename T>
struct Outer {
	struct Inner {
		T value;

		template<typename U>
		Inner(U v);

		T get() const { return value; }
	};
};

template<typename T>
template<typename U>
Outer<T>::Inner::Inner(U v) {
	value = static_cast<T>(v);
}

int main() {
	Outer<int>::Inner paren(40);
	Outer<int>::Inner brace{2};
	return paren.get() + brace.get();
}
