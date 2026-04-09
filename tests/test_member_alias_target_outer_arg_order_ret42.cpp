template <int V>
struct int_constant {
	static constexpr int value = V;
	using type = int_constant;
};

template <typename T>
struct pick_value;

template <>
struct pick_value<int> {
	using type = int_constant<1>;
};

template <>
struct pick_value<long> {
	using type = int_constant<42>;
};

template <typename T, typename U>
struct holder {
	using selected = typename pick_value<U>::type;
	static constexpr int value = selected::value;
};

int main() {
	return holder<char, long>::value;
}
