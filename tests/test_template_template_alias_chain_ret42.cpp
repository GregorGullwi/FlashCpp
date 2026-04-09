template <int V>
struct int_constant {
	static constexpr int value = V;
	using type = int_constant;
};

template <typename T>
struct Choose {
	using type = int_constant<42>;
};

template <template <typename> class TT, typename T>
struct Wrap {
	using chosen = typename TT<T>::type::type;
	static constexpr int value = chosen::value;
};

int main() {
	return Wrap<Choose, int>::value;
}
