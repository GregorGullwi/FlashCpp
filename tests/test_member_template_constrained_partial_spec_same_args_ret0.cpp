// Test: A constrained partial specialization of a member class template is
// valid even when its argument list matches the primary template parameter
// list, because the requires clause makes it more specialized.
// This mirrors the MSVC STL transform_view::_Category_base pattern.

template <typename T>
concept Integral = requires { typename T::value_type; };

template <typename T>
concept Forward = requires(T t) { ++t; };

struct outer {
	template <bool Const>
	struct category_base {};

	template <bool Const>
		requires Forward<int>
	struct category_base<Const> {
		using type = int;
	};
};

int main() {
	using cat = outer::category_base<true>;
	return sizeof(cat) - sizeof(outer::category_base<true>);
}