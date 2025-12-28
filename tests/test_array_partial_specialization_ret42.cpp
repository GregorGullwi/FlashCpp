// Validate template partial specializations with array template arguments
template<typename T>
struct is_array {
	static constexpr int value = 0;
};

template<typename T>
struct is_array<T[]> {
	static constexpr int value = 1;
};

template<typename T, int N>
struct is_array<T[N]> {
	static constexpr int value = 2;
};

int main() {
	return (is_array<int[]>::value == 1 && is_array<int[3]>::value == 2) ? 42 : 0;
}
