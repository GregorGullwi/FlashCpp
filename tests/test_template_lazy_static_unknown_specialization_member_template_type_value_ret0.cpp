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
struct Traits {
	template <typename U>
	struct Box {
		using type = typename chooser<(sizeof(U) == sizeof(T))>::type;
	};
};

template <typename T>
struct Holder {
	inline static int value = Traits<T>::template Box<T>::type::value;
};

int main() {
	return Holder<int>::value == 42 ? 0 : 1;
}
