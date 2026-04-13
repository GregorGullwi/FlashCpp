namespace meta {
	template<bool B, typename T = void>
	struct enable_if { };

	template<typename T>
	struct enable_if<true, T> {
		using type = T;
	};

	template<bool B, typename T = void>
	using enable_if_t = typename enable_if<B, T>::type;
}

using qualified_int = meta::enable_if_t<true, int>;
using global_qualified_int = ::meta::enable_if_t<true, int>;

int main() {
	qualified_int first = 19;
	global_qualified_int second = 23;
	return (first + second) - 42;
}
