// The inner fold consumes Inner before the enclosing expansion selects each Outer.
template<int... V> struct Tags {
	int count() const { return sizeof...(V); }
	int sum() const { return (0 + ... + V); }
};
template<class... Outer> struct Owner {
	template<class... Inner> struct Box {
		Tags<(static_cast<int>(sizeof(Outer)) + (0 + ... + static_cast<int>(sizeof(Inner))))...> values;
	};
};
int main() {
	Owner<char, long long, double>::Box<short, int> full{};
	Owner<>::Box<short, int> empty_outer{};
	Owner<char, long long, double>::Box<> empty_inner{};
	if (full.values.count() != 3) return 1;
	if (full.values.sum() != 3 * (sizeof(short) + sizeof(int)) + sizeof(char) + sizeof(long long) + sizeof(double)) return 2;
	if (empty_outer.values.count() != 0 || empty_outer.values.sum() != 0) return 3;
	if (empty_inner.values.count() != 3 || empty_inner.values.sum() != sizeof(char) + sizeof(long long) + sizeof(double)) return 4;
	return 0;
}
