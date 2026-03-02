template<typename A, typename B>
struct bool_wrap {
	static constexpr bool value = false;
};

template<>
struct bool_wrap<char, int> {
	static constexpr bool value = true;
};

template<typename X, typename Y>
struct chooser : bool_wrap<Y, X> {};

int main() {
	return chooser<int, char>::value ? 42 : 0;
}
