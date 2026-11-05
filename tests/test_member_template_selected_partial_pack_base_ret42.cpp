// A selected partial owner resolves its own nested member class templates,
// including pack-expanded bases and the owner's dependent parameters.
struct A {
	int a = 1;
};

struct B {
	int b = 2;
};

template<class T>
struct Base {
	T base = 3;
};

template<class T>
struct Owner {
	template<class... U>
	struct List {
		int primary = 99;
	};

	template<class U>
	struct Single {
		int primary = 98;
	};
};

template<class T>
struct Owner<T*> {
	template<class... U>
	struct List : U... {
		T tag = 7;
	};

	template<class U>
	struct Single : Base<U> {
		T tag = 9;
	};
};

template<class T>
int dependent_list_value() {
	Owner<T*>::template List<A, B> value{};
	return value.a + value.b + static_cast<int>(value.tag);
}

int main() {
	Owner<int>::List<A, B> primary{};
	if (primary.primary != 99) return 1;

	Owner<long long*>::List<> empty{};
	if (static_cast<int>(empty.tag) != 7) return 2;

	Owner<long long*>::List<A> one{};
	if (one.a != 1 || static_cast<int>(one.tag) != 7) return 3;

	Owner<long long*>::List<A, B> two{};
	if (two.a + two.b != 3 || static_cast<int>(two.tag) != 7) return 4;

	Owner<long long*>::Single<int> single{};
	if (single.tag != 9 || single.base != 3) return 5;

	if (dependent_list_value<long long>() != 10) return 6;

	return 42;
}
