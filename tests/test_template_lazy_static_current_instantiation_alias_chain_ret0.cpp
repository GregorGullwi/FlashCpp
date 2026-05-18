template <typename T>
struct Box {
	struct Deep {
		static constexpr int value = 42;
	};

	struct Mid {
		using inner = Deep;
	};

	using self_type = Box<T>;
	using alias_mid = typename self_type::Mid;

	inline static int value = alias_mid::inner::value;
};

int main() {
	return Box<int>::value == 42 ? 0 : 1;
}
