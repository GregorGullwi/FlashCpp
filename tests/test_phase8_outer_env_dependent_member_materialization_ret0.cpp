template <typename T>
struct identity {
	using type = T;
};

template <typename T>
struct outer {
	template <typename U>
	struct inner {
		using rebound = typename identity<T>::type;
		U payload;
		rebound value;
	};
};

int main() {
	(void)sizeof(outer<long>::inner<int>);
	return 0;
}
