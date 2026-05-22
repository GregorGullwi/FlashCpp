// Regression: out-of-line constructor replay must preserve complete-class
// overload visibility for helper calls inside member-initializer expressions.

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

	TupleImpl(const Inherited& tail_value, Head value)
		: Inherited(tail_value), head(value) {}

	template<typename U>
	TupleImpl(U, const TupleImpl& in);
};

template<unsigned I, typename Head, typename... Tail>
template<typename U>
TupleImpl<I, Head, Tail...>::TupleImpl(U, const TupleImpl& in)
	: Inherited(tail(in)), head(in.head) {}

int main() {
	TupleImpl<1u, int> tail_value;
	tail_value.head = 7;
	TupleImpl<0u, int, int> a(tail_value, 3);
	TupleImpl<0u, int, int> b(0, a);
	return 0;
}
