// Regression: a dependent if constexpr condition must defer decltype(auto)
// return deduction until the selected specialization is known.

template <typename...>
using void_t = void;

template <typename Left, typename Right>
constexpr bool same_type = false;

template <typename Type>
constexpr bool same_type<Type, Type> = true;

template <typename Type, typename = void>
constexpr bool has_matching_marker = false;

template <typename Type>
constexpr bool has_matching_marker<Type, void_t<typename Type::marker>> =
	same_type<Type, typename Type::marker>;

template <typename Type>
decltype(auto) choose_value(Type&& value) {
	if constexpr (has_matching_marker<Type>) {
		return value + 0;
	} else {
		return static_cast<Type&&>(value);
	}
}

int main() {
	int value = 0;
	return choose_value<int&>(value) == 0 ? 0 : 1;
}
