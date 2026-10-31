// An enclosing owner pack and a nested member-template pack must expand in lockstep in a field type.
struct PackPayload { long long first; long long second; long long third; };
template<class A, class B> struct PackPair {
	A first;
	B second;
};
template<class T> struct PackTarget {
	template<class... U> struct List {
		T owner_value;
		int marker;
		int count() const { return sizeof...(U); }
		int bytes() const { return (0 + ... + sizeof(U)); }
	};
};
template<class T, class... Outer> struct PackOwner {
	template<class... Inner> struct Box {
		T owner_value;
		typename PackTarget<T>::template List<PackPair<Outer, Inner>...> pairs;
	};
};
int main() {
	PackOwner<short>::Box<> empty{};
	PackOwner<short, PackPayload>::Box<double> singleton{};
	PackOwner<long long, short, char, PackPayload>::Box<int, long long, double> mixed{};
	PackOwner<short, long long, double, unsigned char>::Box<int, long long, double> same_inner{};
	empty.owner_value = 5;
	singleton.owner_value = 7;
	mixed.owner_value = 11;
	same_inner.owner_value = 13;
	empty.pairs.owner_value = 17;
	singleton.pairs.owner_value = 19;
	mixed.pairs.owner_value = 23;
	same_inner.pairs.owner_value = 29;
	if (empty.pairs.count() != 0 || empty.pairs.bytes() != 0) return 1;
	if (sizeof(empty.pairs) != sizeof(PackTarget<short>::List<>)) return 2;
	if (singleton.pairs.count() != 1 || singleton.pairs.bytes() != sizeof(PackPair<PackPayload, double>)) return 2;
	if (mixed.pairs.count() != 3 || same_inner.pairs.count() != 3) return 1;
	const int mixed_bytes = sizeof(PackPair<short, int>) + sizeof(PackPair<char, long long>) + sizeof(PackPair<PackPayload, double>);
	const int same_inner_bytes = sizeof(PackPair<long long, int>) + sizeof(PackPair<double, long long>) + sizeof(PackPair<unsigned char, double>);
	if (mixed.pairs.bytes() != mixed_bytes || same_inner.pairs.bytes() != same_inner_bytes) return 3;
	if (sizeof(mixed.pairs) == sizeof(same_inner.pairs)) return 4;
	if (empty.owner_value != 5 || singleton.owner_value != 7) return 5;
	if (mixed.owner_value != 11 || same_inner.owner_value != 13) return 5;
	if (empty.pairs.owner_value != 17 || singleton.pairs.owner_value != 19) return 6;
	if (mixed.pairs.owner_value != 23 || same_inner.pairs.owner_value != 29) return 6;
	return 0;
}
