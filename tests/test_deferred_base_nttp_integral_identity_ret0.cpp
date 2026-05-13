template <typename T, T V>
struct typed_value {
	static constexpr int selected = 1;
};

template <>
struct typed_value<unsigned, 7u> {
	static constexpr int selected = 11;
};

template <>
struct typed_value<long, 9L> {
	static constexpr int selected = 13;
};

template <>
struct typed_value<int, 7> {
	static constexpr int selected = 97;
};

template <>
struct typed_value<int, 9> {
	static constexpr int selected = 99;
};

template <typename T>
struct unsigned_source {
	static constexpr unsigned value = 7u;
};

template <typename T>
struct long_source {
	static constexpr long value = 9L;
};

template <typename T>
struct use_unsigned : typed_value<unsigned, unsigned_source<T>::value> {};

template <typename T>
struct use_long : typed_value<long, long_source<T>::value> {};

using size_t = unsigned long;

template <size_t N>
struct size_value {
	static constexpr int selected = 1;
};

template <>
struct size_value<4ul> {
	static constexpr int selected = 21;
};

template <typename T>
struct size_source {
	static constexpr size_t value = 4ul;
};

template <typename T>
struct use_size : size_value<size_source<T>::value> {};

int main() {
	if (use_unsigned<int>::selected != 11) return 1;
	if (use_long<int>::selected != 13) return 2;
	if (use_size<int>::selected != 21) return 3;
	return 0;
}
