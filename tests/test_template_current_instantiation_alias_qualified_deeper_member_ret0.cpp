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
	using alias_inner = typename alias_mid::inner;

	static int run() {
		return alias_mid::inner::value - alias_inner::value;
	}
};

int main() {
	return Box<int>::run();
}
