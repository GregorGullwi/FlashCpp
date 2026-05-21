template <typename T>
struct provider {
	struct node {
		template <typename U>
		struct apply {
			static constexpr bool value = sizeof(T) == sizeof(U);
		};
	};
};

template <typename T, bool = provider<T>::node::template apply<T>::value>
struct probe {
	static constexpr int value = 7;
};

template <typename T>
struct probe<T, false> {
	static constexpr int value = 1;
};

int main() {
	return probe<int>::value == 7 ? 0 : 1;
}
