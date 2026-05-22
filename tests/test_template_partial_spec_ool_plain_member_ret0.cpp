// Regression: OOL plain (non-template) member function body on a partial class
// template specialization must be attached for replay so that dependent-base
// lookup via 'this->...' resolves correctly with T in scope.
template<class T>
struct Base {
	T value(T x) { return x; }
};

template<class T>
struct Foo;

template<class T>
struct Foo<T*> : Base<T> {
	T get(T x);
};

template<class T>
T Foo<T*>::get(T x) {
	return this->value(x);
}

int main() {
	Foo<int*> f;
	return f.get(0);
}
