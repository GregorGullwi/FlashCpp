// Nested classes declared in a selected partial specialization must replay
// dependent deferred bases and their default member initializers before layout.
struct DirectBase {
	long long direct = 5;
};

template<class T>
struct Base {
	T value = 7;
};

template<class A, class B>
struct PairBase {
	A first = 11;
	B second = 12;
};

template<class T>
struct Owner {
	struct Child {};
};

template<class T>
struct Owner<T*> {
	struct Child : Base<T> {
		T own = 9;
	};
};

template<class T>
struct DirectOwner {
	struct Direct {};
};

template<class T>
struct DirectOwner<T*> {
	struct Direct : T {};
};

template<class T>
struct PairOwner {
	struct Pair {};
};

template<class T>
struct PairOwner<T*> {
	struct Pair : PairBase<T, int> {};
};

template<class T>
long long dependent_child_value() {
	Owner<T*>::Child child{};
	return child.value + child.own;
}

int main() {
	Owner<long long*>::Child child{};
	if (child.value != 7 || child.own != 9) return 1;
	child.value = 5000000000LL;
	if (child.value != 5000000000LL) return 2;

	DirectOwner<DirectBase*>::Direct direct{};
	if (direct.direct != 5) return 3;

	PairOwner<long long*>::Pair pair{};
	if (pair.first != 11 || pair.second != 12) return 4;
	pair.first = 5000000000LL;
	if (pair.first != 5000000000LL) return 5;

	if (dependent_child_value<long long>() != 16) return 6;

	return 42;
}
