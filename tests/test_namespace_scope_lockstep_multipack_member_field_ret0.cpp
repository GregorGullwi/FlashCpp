// A namespace-scope target expands enclosing and member packs in lockstep.
struct PackPayload { long long value; };
template<class A, class B> struct Pair {
	A first;
	B second;
};
template<class... U> struct List {
	int marker;
	int count() const { return sizeof...(U); }
	int bytes() const { return (0 + ... + sizeof(U)); }
};
template<class... Outer> struct Holder {
	template<class... Inner> struct Box {
		List<Pair<Outer, Inner>...> values;
	};
};
int main() {
	Holder<>::Box<> empty{};
	Holder<PackPayload>::Box<double> single{};
	Holder<short, char, PackPayload>::Box<int, long long, double> full{};
	if (empty.values.count() != 0 || empty.values.bytes() != 0) return 1;
	if (single.values.count() != 1 || single.values.bytes() != sizeof(Pair<PackPayload, double>)) return 2;
	if (full.values.count() != 3) return 3;
	const int full_bytes = sizeof(Pair<short, int>) + sizeof(Pair<char, long long>) + sizeof(Pair<PackPayload, double>);
	if (full.values.bytes() != full_bytes) return 4;
	empty.values.marker = 3;
	single.values.marker = 4;
	full.values.marker = 5;
	return empty.values.marker + single.values.marker + full.values.marker - 12;
}
