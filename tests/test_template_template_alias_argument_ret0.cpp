template <bool>
struct Select;

template <>
struct Select<true> {
	template <template <class> class Function, class Type>
	using Apply = typename Function<Type>::type;
};

template <class Type>
struct Wrap {
	using type = Type;
};

using Result = typename Select<true>::template Apply<Wrap, int>;

int main() {
	return 0;
}
