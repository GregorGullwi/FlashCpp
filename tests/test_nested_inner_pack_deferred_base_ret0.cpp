// An inner pack expansion supplies each base pattern; the outer expansion indexes Outer.
struct Payload { long long first; long long second; };
template<class A, class B> struct Pair {
	A first;
	B second;
};
template<class... T> struct List {
	long long storage[sizeof...(T) + 1];
};
template<class... Outer> struct Owner {
	template<class... Inner> struct Box : Pair<Outer, List<Inner...>>... {};
};
int main() {
	using Full = Owner<short, Payload>::Box<char, long long, double>;
	using EmptyOuter = Owner<>::Box<char, long long, double>;
	using EmptyInner = Owner<short, Payload>::Box<>;
	if (sizeof(Full) != sizeof(Pair<short, List<char, long long, double>>) + sizeof(Pair<Payload, List<char, long long, double>>)) return 1;
	if (sizeof(EmptyOuter) != 1) return 2;
	if (sizeof(EmptyInner) != sizeof(Pair<short, List<>>) + sizeof(Pair<Payload, List<>>)) return 3;
	return 0;
}
