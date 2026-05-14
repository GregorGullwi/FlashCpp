template <typename T>
inline constexpr int value_v = 1;

namespace meta {
	template <typename T>
	inline constexpr int value_v = 42;
}

template <typename T>
int read_meta_value() {
	return meta::value_v<T>;
}

int main() {
	return read_meta_value<int>();
}
