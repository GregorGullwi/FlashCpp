template <typename T>
struct Box {
	struct Nested {
		static constexpr int value = 42;
	};

	using self_type = Box<T>;
	using alias_inner = typename self_type::Nested;

	static int run() {
		return alias_inner::value - 42;
	}
};

int main() {
	return Box<int>::run();
}
