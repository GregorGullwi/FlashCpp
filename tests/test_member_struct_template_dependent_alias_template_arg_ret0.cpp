template <bool Const, class Type>
struct maybe_const {
	using type = Type;
};

template <class Type>
struct iterator_of {
	using type = Type;
};

template <class View>
struct outer {
	template <bool Const>
	struct category_base {};

	template <bool Const>
	struct category_base<Const> {};

	template <bool Const>
	struct iterator : category_base<Const> {
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
	return it.check();
}
