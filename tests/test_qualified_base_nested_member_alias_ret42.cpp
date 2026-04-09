template <int V>
struct int_constant {
	static constexpr int value = V;
	using type = int_constant;
};

template <typename T>
struct choose_base {
	using selected = int_constant<42>;
	using type = selected;
};

template <typename T>
struct derived : choose_base<T>::type::type {
};

int main() {
	return derived<int>::value;
}
