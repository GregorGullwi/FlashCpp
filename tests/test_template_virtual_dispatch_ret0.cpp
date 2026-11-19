template<class T>
struct PolyBase {
	virtual T get() = 0;
	virtual ~PolyBase() = default;
};

template<class T>
struct Poly : PolyBase<T> {
	T data;
	T get() override { return data; }
};

int main() {
	Poly<int> value;
	value.data = 42;
	PolyBase<int>* base = &value;
	return base->get() == 42 ? 0 : 1;
}
