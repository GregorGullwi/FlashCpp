struct true_type {
	static constexpr bool value = true;
};

struct false_type {
	static constexpr bool value = false;
};

template <bool Select>
struct chooser;

template <>
struct chooser<true> {
	using type = true_type;
};

template <>
struct chooser<false> {
	using type = false_type;
};

template <typename T>
struct provider_with_alias {
	template <typename U>
	struct node {
		using type = typename chooser<(sizeof(T) == sizeof(U))>::type;
	};
};

template <typename T,
		  bool = provider_with_alias<T>::template node<T>::type::value>
struct alias_probe {
	static constexpr int value = 7;
};

template <typename T>
struct alias_probe<T, false> {
	static constexpr int value = 1;
};

int main() {
	return alias_probe<int>::value == 7 ? 0 : 1;
}
