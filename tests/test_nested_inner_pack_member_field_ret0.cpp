// The inner pack expansion belongs to List; the enclosing expansion only indexes Outer.
struct Payload { long long first; long long second; };
template<class A, class B> struct Pair {
	A first;
	B second;
};
template<class... T> struct List {
	long long storage[sizeof...(T) + 1];
	int marker;
	int count() const { return sizeof...(T); }
	int bytes() const { return (0 + ... + sizeof(T)); }
};
template<class... Outer> struct Owner {
	template<class... Inner> struct Box {
		List<Pair<Outer, List<Inner...>>...> values;
	};
};
int main() {
	Owner<short, Payload>::Box<char, long long, double> nested{};
	Owner<>::Box<char, long long, double> no_outer{};
	Owner<short, Payload>::Box<> no_inner{};
	if (nested.values.count() != 2) return 1;
	const int nested_bytes = sizeof(Pair<short, List<char, long long, double>>) + sizeof(Pair<Payload, List<char, long long, double>>);
	if (nested.values.bytes() != nested_bytes) return 2;
	if (no_outer.values.count() != 0 || no_outer.values.bytes() != 0) return 3;
	if (no_inner.values.count() != 2) return 4;
	if (no_inner.values.bytes() != sizeof(Pair<short, List<>>) + sizeof(Pair<Payload, List<>>)) return 5;
	nested.values.marker = 7;
	no_outer.values.marker = 11;
	no_inner.values.marker = 13;
	return nested.values.marker + no_outer.values.marker + no_inner.values.marker - 31;
}
