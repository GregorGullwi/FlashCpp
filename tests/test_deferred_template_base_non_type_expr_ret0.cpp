template <unsigned long I, typename... Ts>
struct TupleImpl;

template <unsigned long I>
struct TupleImpl<I> {
	static constexpr unsigned long depth = I;
};

template <unsigned long I, typename T, typename... Rest>
struct TupleImpl<I, T, Rest...> : TupleImpl<I + 1, Rest...> {
	static constexpr unsigned long depth = TupleImpl<I + 1, Rest...>::depth;
};

int main() {
	TupleImpl<0, int, float, double> value;
	(void)value;
	return 0;
}
