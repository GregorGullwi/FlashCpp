// Regression test: ordinary type parsing through a chained alias template that resolves to a class template.

template <typename T, T V>
struct integral_constant {
	static constexpr T value = V;
};

template <bool B>
using bool_constant = integral_constant<bool, B>;

template <bool B>
using chained_bool_constant = bool_constant<B>;

int main() {
	chained_bool_constant<true> truth;
	chained_bool_constant<false> falsehood;
	return (truth.value ? 40 : 0) + (!falsehood.value ? 2 : 0);
}
