struct Prefix {
	long long padding;
};

template <class T>
struct Base {
	T value;
};

struct Derived : Prefix, Base<short> {};

template <class T>
struct PointerSource {
	T* pointer;
	operator T*() const { return pointer; }
};

const Base<short>* pass(const Base<short>* pointer) {
	return pointer;
}

int main() {
	Derived derived{};
	derived.value = 42;
	const Base<short>* expected = static_cast<Base<short>*>(&derived);
	const Base<short>* actual = pass(PointerSource<Derived>{&derived});
	if (actual != expected) {
		return 1;
	}
	if (actual->value != 42) {
		return 2;
	}
	if (pass(PointerSource<Derived>{nullptr}) != nullptr) {
		return 3;
	}
	return 0;
}
