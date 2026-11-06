// A nested class materialized from a selected partial specialization must
// preserve its default member initializers.
template<class T>
struct Owner {
	struct Child {};
};

template<class T>
struct Owner<T*> {
	struct Child {
		T own = 9;
	};
};

int main() {
	Owner<long long*>::Child child{};
	if (child.own != 9) return 1;
	return 42;
}
