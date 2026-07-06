enum class strategy {
	none,
	swap,
	exchange
};

template<typename Type>
void unavailable_swap(Type&, Type&) = delete;

struct swap_fn {
	template<typename Type>
	static constexpr strategy choose() {
		return strategy::exchange;
	}

	template<typename Type>
	static constexpr strategy choice = choose<Type>();

	template<typename Type>
	void operator()(Type& left, Type& right) const {
		constexpr strategy selected = choice<Type>;
		if constexpr (selected == strategy::swap) {
			unavailable_swap(left, right);
		} else if constexpr (selected == strategy::exchange) {
			Type temp = left;
			left = right;
			right = temp;
		}
	}
};

namespace ranges {
	inline constexpr swap_fn swap{};
}

int main() {
	int left = 1;
	int right = 2;
	ranges::swap(left, right);
	return left == 2 && right == 1 ? 0 : 1;
}
