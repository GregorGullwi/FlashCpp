template <bool Condition, typename Type = void>
struct enable_if { };

template <typename Type>
struct enable_if<true, Type> {
	using type = Type;
};

template <typename A, typename B>
inline constexpr bool is_same_v = false;

template <typename A>
inline constexpr bool is_same_v<A, A> = true;

template <typename T, typename U = T, bool Matches = is_same_v<T, U>>
inline constexpr bool defaulted_matches_v = Matches;

template <typename T, typename = typename enable_if<defaulted_matches_v<T>, int>::type>
int select_match(int) {
	return 42;
}

template <typename T>
int select_match(...) {
	return 0;
}

int main() {
	return select_match<int>(0);
}
