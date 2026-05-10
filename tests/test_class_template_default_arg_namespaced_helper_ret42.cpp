namespace defaults_ns {
	template <typename T>
	constexpr int helper() {
		return static_cast<int>(sizeof(T)) + 38;
	}

	template <typename T, int V = helper<T>()>
	struct Box {
		static constexpr int value = V;
	};
}

int main() {
	return defaults_ns::Box<char>::value == 39 ? 42 : 0;
}
