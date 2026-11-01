template<class A, class B> struct Pair {};
template<class... U> struct List { int marker; };
template<class... Outer> struct Holder {
	template<class... Inner> struct Box {
		List<Pair<Outer, Inner>...> values;
	};
};
int main() {
	Holder<int>::Box<char, double> value{};
	value.values.marker = 1;
	return value.values.marker - 1;
}
