// Regression: OOL member-function-template body on a partial class template
// specialization must be replayed (not fall through to AST-only) so that
// dependent-base lookup via 'this->template ...' resolves correctly.
template<class T>
struct Base {
	template<class U>
	T transform(U u) { return static_cast<T>(u); }
};

template<class T>
struct Foo;

template<class T>
struct Foo<T*> : Base<T> {
	template<class U>
	T convert(U u);
};

template<class T>
template<class U>
T Foo<T*>::convert(U u) {
	return this->template transform<U>(u);
}

int main() {
	Foo<int*> f;
	return f.convert(42L) == 42 ? 0 : 1;
}
