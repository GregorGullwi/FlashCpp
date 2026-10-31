// A member-template field must combine its instantiated owner with an expanded pack.
struct Payload { long long value; };
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
		typename Owner<T>::template List<Head, Tail...> values;
		typename Owner<T>::template List<Head, const Tail*...> pointers;
	};
};
int main() {
	Holder<short>::Box<int> empty{};
	Holder<long long>::Box<int, char, Payload> full{};
	Holder<short>::Box<int, char, Payload> same_args{};
	empty.values.marker = 3;
	full.values.marker = 4;
	full.values.owner_value = 33;
	same_args.values.owner_value = 7;
	if (empty.values.count() != 1) return 1;
	if (full.values.count() != 3 || same_args.values.count() != 3) return 2;
	if (sizeof(full.values) == sizeof(same_args.values)) return 3;
	if (full.values.owner_value != 33 || same_args.values.owner_value != 7) return 4;
	if (empty.pointers.bytes() != sizeof(int)) return 5;
	if (full.pointers.bytes() != sizeof(int) + sizeof(const char*) + sizeof(const Payload*)) return 6;
	return empty.values.marker + full.values.marker - 7;
}
