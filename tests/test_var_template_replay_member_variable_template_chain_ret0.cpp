template <typename T>
struct direct_owner {
	template <typename U>
	static constexpr int value = sizeof(T) + sizeof(U);
};

template <typename T>
constexpr int direct_value_v = direct_owner<T>::template value<T>;

template <typename T>
struct nested_owner {
	struct inner {
		template <typename U>
		static constexpr int value = sizeof(T) + sizeof(U);
	};
};

template <typename T>
constexpr int nested_value_v = nested_owner<T>::inner::template value<T>;

template <typename T>
struct base_owner {
	template <typename U>
	static constexpr int value = sizeof(T) + sizeof(U);
};

template <typename T>
struct derived_owner : base_owner<T> {};

template <typename T>
constexpr int inherited_value_v = derived_owner<T>::template value<T>;

int main() {
	static_assert(
		direct_value_v<int> == 8,
		"direct member variable-template replay preserves explicit template arguments");
	static_assert(
		nested_value_v<int> == 8,
		"nested qualified member variable-template replay preserves explicit template arguments");
	static_assert(
		inherited_value_v<int> == 8,
		"inherited qualified member variable-template replay preserves explicit template arguments");
	return direct_value_v<int> + nested_value_v<int> + inherited_value_v<int> - 24;
}
