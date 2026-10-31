// A namespace-scope target expands a single nontrivial pack pattern in a member field.
struct Payload { long long value; };
template<class T> struct Wrap {
	T value;
	int tag;
};
template<class... U> struct List {
	int marker;
	int count() const { return sizeof...(U); }
	int bytes() const { return (0 + ... + sizeof(U)); }
};
template<class T> struct Holder {
	template<class... Tail> struct Box {
		List<Wrap<Tail>...> values;
	};
};
int main() {
	Holder<int>::Box<> empty{};
	Holder<int>::Box<char, Payload> full{};
	if (empty.values.count() != 0 || empty.values.bytes() != 0) return 1;
	if (full.values.count() != 2) return 2;
	if (full.values.bytes() != sizeof(Wrap<char>) + sizeof(Wrap<Payload>)) return 3;
	empty.values.marker = 3;
	full.values.marker = 4;
	return empty.values.marker + full.values.marker - 7;
}
