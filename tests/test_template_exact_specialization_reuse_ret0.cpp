template <typename T>
struct traits {
	using value_type = T;
};

template <>
struct traits<char> {
	using value_type = char;
	static constexpr int marker = 7;
};

template <typename T>
struct wrapper {
	using first = typename traits<T>::value_type;
	using second = typename traits<T>::value_type;
	static constexpr int value = traits<T>::marker + traits<T>::marker;
};

int main() {
	wrapper<char>::first a = 'A';
	wrapper<char>::second b = 'B';
	return wrapper<char>::value == 14 && a == 'A' && b == 'B' ? 0 : 1;
}
