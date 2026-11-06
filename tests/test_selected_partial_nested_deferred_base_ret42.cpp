// Nested classes declared in a selected partial specialization must replay
// dependent deferred bases before layout and member lookup.
struct DirectBase {
	long long direct;
};

template<class T>
struct Base {
	T value;
};

template<class A, class B>
struct PairBase {
	A first;
	B second;
};

template<class T>
struct Owner {
	struct Child {};
};

template<class T>
struct Owner<T*> {
	struct Child : Base<T> {
		T own;
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
	child.own = 9;
	child.value = 5000000000LL;
	return child.value + child.own;
}

int main() {
	Owner<long long*>::Child child{};
	child.value = 5000000000LL;
	child.own = 9;
	if (child.value != 5000000000LL || child.own != 9) return 1;

	DirectOwner<DirectBase*>::Direct direct{};
	direct.direct = 5000000000LL;
	if (direct.direct != 5000000000LL) return 2;

	PairOwner<long long*>::Pair pair{};
	pair.first = 5000000000LL;
	pair.second = 3;
	if (pair.first != 5000000000LL || pair.second != 3) return 3;

	if (dependent_child_value<long long>() != 5000000009LL) return 4;

	return 42;
}
