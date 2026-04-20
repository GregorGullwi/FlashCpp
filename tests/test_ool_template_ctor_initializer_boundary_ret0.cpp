// Regression test: instantiated out-of-line template constructors should have
// their concrete initializer surfaces checked by the pre-sema boundary pass
// even while the deferred body still belongs to parser-owned lazy
// materialization.

template <typename T>
struct BoundaryHolder {
	T data;
	int size;

	BoundaryHolder(T val, int sz);
};

template <typename T>
BoundaryHolder<T>::BoundaryHolder(T val, int sz) : data{val}, size{sz} {
	data = val;
	size = sz;
}

int main() {
	BoundaryHolder<int> value(40, 2);
	return value.data + value.size - 42;
}
