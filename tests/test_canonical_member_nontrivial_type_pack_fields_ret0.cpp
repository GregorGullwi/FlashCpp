// Nontrivial member-template pack patterns must substitute the generic expression per element.
struct Payload { long long value; };
template<class T> struct Wrap {
	T value;
	int tag;
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
	template<class... U> struct Box {};
	template<class Head, class... Tail> struct Box<Head, Tail...> {
		typename Owner<T>::template List<Head, Wrap<Tail>...> values;
		typename Owner<T>::template List<Head, Wrap<Wrap<Tail>>...> nested;
		typename Owner<T>::template List<Head, const Wrap<Tail>*...> pointers;
		int marker;
	};
};
int main() {
	Holder<short>::Box<int> empty{};
	Holder<short>::Box<int, char, Payload> full{};
	Holder<long long>::Box<int, char, Payload> same_args{};
	if (empty.values.count() != 1 || empty.nested.count() != 1 || empty.pointers.count() != 1) return 1;
	if (full.values.count() != 3 || full.nested.count() != 3 || full.pointers.count() != 3) return 2;
	if (same_args.values.count() != 3) return 3;
	if (empty.values.bytes() != sizeof(int) || empty.nested.bytes() != sizeof(int)) return 4;
	if (empty.pointers.bytes() != sizeof(int)) return 5;
	if (full.values.bytes() != sizeof(int) + sizeof(Wrap<char>) + sizeof(Wrap<Payload>)) return 6;
	if (full.nested.bytes() != sizeof(int) + sizeof(Wrap<Wrap<char>>) + sizeof(Wrap<Wrap<Payload>>)) return 7;
	if (full.pointers.bytes() != sizeof(int) + sizeof(const Wrap<char>*) + sizeof(const Wrap<Payload>*)) return 8;
	if (sizeof(full.values) == sizeof(same_args.values)) return 9;
	full.values.owner_value = 33;
	same_args.values.owner_value = 7;
	if (full.values.owner_value != 33 || same_args.values.owner_value != 7) return 10;
	empty.values.marker = 3;
	full.values.marker = 4;
	return empty.values.marker + full.values.marker - 7;
}
