template <int V>
struct int_constant {
	static constexpr int value = V;
	using type = int_constant;
};

template <typename T>
struct local_base {
	using type = int_constant<7>;
};

template <typename T>
using choose_base_t = local_base<T>;

namespace meta {
	template <typename T>
	struct namespaced_base {
		using type = int_constant<42>;
	};

	template <typename T>
	using choose_base_t = namespaced_base<T>;
}

template <typename T>
struct derived : meta::choose_base_t<T>::type::type {
};

int main() {
	return derived<int>::value;
}
