// Regression: partial-spec OOL member-function-template replay must attach
// from base-template-name OOL registration and preserve dependent-base lookup
// via 'this->template ...'.
template<class T>
struct BaseTempl {
	template<class U>
	T convert(U u) { return static_cast<T>(u); }
};

template<class T>
struct FooTempl;

template<class T>
struct FooTempl<T*> : BaseTempl<T> {
	template<class U>
	T run(U u);
};

template<class T>
template<class U>
T FooTempl<T*>::run(U u) {
	return this->template convert<U>(u);
}

int main() {
	FooTempl<int*> f;
	return f.run(0L);
}
