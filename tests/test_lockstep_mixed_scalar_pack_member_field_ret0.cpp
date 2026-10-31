// A scalar template parameter and a pack expand together in a member field pattern.
struct Payload { long long value; };
template<class A, class B> struct Pair {
	A first;
	B second;
};
template<class T> struct Owner {
	template<class... U> struct List {
		T owner_value;
		int marker;
		int count() const { return sizeof...(U); }
		int bytes() const { return (0 + ... + sizeof(U)); }
	};
};
template<class T> struct Holder {
	template<class Head, class... Tail> struct Box {
		typename Owner<T>::template List<Pair<Head, Tail>...> values;
	};
};
int main() {
	Holder<short>::Box<char> empty{};
	Holder<short>::Box<char, int, Payload> full{};
	Holder<long long>::Box<char, int, Payload> same_args{};
	if (empty.values.count() != 0 || empty.values.bytes() != 0) return 1;
	if (full.values.count() != 2 || same_args.values.count() != 2) return 2;
	if (full.values.bytes() != sizeof(Pair<char, int>) + sizeof(Pair<char, Payload>)) return 3;
	if (sizeof(full.values) == sizeof(same_args.values)) return 4;
	full.values.owner_value = 33;
	same_args.values.owner_value = 7;
	if (full.values.owner_value != 33 || same_args.values.owner_value != 7) return 5;
	empty.values.marker = 3;
	full.values.marker = 4;
	return empty.values.marker + full.values.marker - 7;
}
