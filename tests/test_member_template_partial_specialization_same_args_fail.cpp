template <class View>
struct outer {
	template <bool Const>
	struct category_base {};

	template <bool Const>
	struct category_base<Const> {};
};

int main() {
	return 0;
}
