// Regression: partial-spec OOL plain member replay must still attach when
// OOL definitions are registered under the base template name (Foo) while
// instantiation proceeds through a partial-spec instantiated name.
template<class T>
struct BasePlain {
	T id(T x) { return x; }
};

template<class T>
struct FooPlain;

template<class T>
struct FooPlain<T*> : BasePlain<T> {
	T run(T x);
};

template<class T>
T FooPlain<T*>::run(T x) {
	return this->id(x);
}

int main() {
	FooPlain<int*> f;
	return f.run(0);
}
