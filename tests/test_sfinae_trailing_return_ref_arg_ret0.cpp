template <typename T>
inline constexpr bool is_reference_v = false;

template <typename T>
inline constexpr bool is_reference_v<T&> = true;

template <typename T>
inline constexpr bool is_reference_v<T&&> = true;

template <bool B, typename T = void>
struct enable_if {};

template <typename T>
struct enable_if<true, T> {
	using type = T;
};

template <typename T>
auto select(T&&) -> typename enable_if<is_reference_v<T>, int>::type {
	return 42;
}

int select(...) {
	return 7;
}

int main() {
	int value = 0;
	return select(value) == 42 ? 0 : 1;
}
