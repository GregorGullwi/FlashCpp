template <typename T>
struct owner {
	template <typename U>
	static constexpr int value = 42;
};

template <typename T>
constexpr int replay_value_v = owner<T>::template value<T>;

int main() {
	static_assert(
		replay_value_v<int> == 42,
		"variable-template initializer replay preserves dependent member variable-template ids");
	return replay_value_v<int> - 42;
}
