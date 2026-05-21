struct true_type {
	static constexpr int value = 42;
};

struct false_type {
	static constexpr int value = 7;
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
struct Base {
	template <typename U>
	struct Inner {
		using type = typename chooser<(sizeof(T) == sizeof(U))>::type;
	};
};

template <typename T>
struct Derived : Base<T> {
	static constexpr int value = Derived<T>::template Inner<int>::type::value;
};

int main() {
	return Derived<int>::value == 42 ? 0 : 1;
}
