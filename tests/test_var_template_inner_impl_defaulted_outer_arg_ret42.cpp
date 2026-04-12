template <typename T>
struct inner_trait {
	static constexpr int value = 42;
};

template <typename T, typename U = void>
inline constexpr int outer_trait_v = inner_trait<T>::value;

int main() {
	return outer_trait_v<int>;
}
