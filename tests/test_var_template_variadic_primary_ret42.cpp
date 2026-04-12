template <bool Condition, typename Type = void>
struct enable_if { };

template <typename Type>
struct enable_if<true, Type> {
	using type = Type;
};

template <typename T, typename... Types>
inline constexpr bool matches_pack_count_v = sizeof...(Types) == 3;

template <typename T, typename = typename enable_if<matches_pack_count_v<T, int, float, char>, int>::type>
int select_overload(int) {
	return 42;
}

template <typename T>
int select_overload(...) {
	return 0;
}

int main() {
	return select_overload<int>(0);
}
