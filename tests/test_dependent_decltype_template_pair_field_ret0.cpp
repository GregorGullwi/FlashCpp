// Edge case: decltype with a parameter whose type is a template instantiation
// (e.g. Pair<X,Y>) rather than a bare template parameter name.
// This exercises the is_incomplete_instantiation_ branch in
// typeSpecDependsOnActiveTemplateParam: the scanner looks up the parameter
// 'p', finds its declared type spec is for Pair<X,Y> which carries
// is_incomplete_instantiation_=true, and correctly defers the decltype.
//
// Also exercises the parser fix for trailing return types on static member
// function declarations (no body) inside a struct template.

template <class A, class B>
struct Pair {
	A first;
	B second;
};

template <class X, class Y>
struct PairAccessor {
	static auto getFirst(Pair<X, Y> p) -> decltype(p.first);
	static auto getSecond(Pair<X, Y> p) -> decltype(p.second);
};

using FirstType = decltype(PairAccessor<int, double>::getFirst(Pair<int, double>{}));
using SecondType = decltype(PairAccessor<int, double>::getSecond(Pair<int, double>{}));

int main() {
	FirstType a = 0;
	SecondType b = 0.0;
	return static_cast<int>(a) + static_cast<int>(b);
}
