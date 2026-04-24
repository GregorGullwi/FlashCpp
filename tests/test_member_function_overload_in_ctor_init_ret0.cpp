// Regression: a class template's member constructor template uses an
// unqualified call to a *static* member function that has multiple
// overloads (e.g. const/non-const) from within its member-initializer
// list.  The parser's complete-class member-function lookup used to
// register only the first matching overload, so overload resolution
// would fail with "No matching function for call to ..." even though a
// valid overload existed.  This is the shape used by libstdc++'s
// `<tuple>` / `_Tuple_impl` allocator-extended constructors via
// `_M_tail(__in)`.
//
// This test restores the original tuple-like context:
// - class template
// - recursive inheritance
// - overloaded static helper returning a base subobject
// - member constructor template calling that helper unqualified from a
//   base-class member-initializer
//
// It must compile, link, and return 0.

template<unsigned I, typename... Ts>
struct TupleImpl;

template<unsigned I>
struct TupleImpl<I> {
	int leaf = 0;
};

template<unsigned I, typename Head, typename... Tail>
struct TupleImpl<I, Head, Tail...> : TupleImpl<I + 1, Tail...> {
	using Inherited = TupleImpl<I + 1, Tail...>;

	Head head{};

	static constexpr Inherited& tail(TupleImpl& t) noexcept { return t; }
	static constexpr const Inherited& tail(const TupleImpl& t) noexcept { return t; }

	TupleImpl() = default;

	template<typename U>
	TupleImpl(U, const TupleImpl& in)
		: Inherited(tail(in)), head(in.head) {}
};

int main() {
	TupleImpl<0u, int, int> a;
	a.head = -3;
	static_cast<TupleImpl<1u, int>&>(a).leaf = 2;
	TupleImpl<0u, int, int> b(0, a);

	return 0;
}
