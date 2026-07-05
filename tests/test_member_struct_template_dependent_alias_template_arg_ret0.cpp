template <bool Const, class Type>
struct maybe_const {
	using type = Type;
};

template <class Type>
struct maybe_const<true, Type> {
	using type = const Type;
};

template <class Type>
struct iterator_of {
	using type = Type;
};

template <class View>
struct outer {
	template <class Base, bool Const>
	struct category_base {};

	template <class Base>
	struct category_base<Base*, true> {};

	template <bool Const>
	struct iterator : category_base<View*, Const> {
		using Base = typename maybe_const<Const, View>::type;
		using Current = typename iterator_of<Base>::type;

		Current value;

		int check() {
			Current local = 42;
			return local - 42;
		}
	};
};

int main() {
	outer<int>::iterator<false> it{};
	outer<int>::iterator<true> const_it{};
	return it.check() + const_it.check();
}
