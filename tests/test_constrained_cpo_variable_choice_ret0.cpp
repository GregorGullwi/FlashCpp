enum class strategy {
	none,
	exchange
};

template<typename Type>
struct choice {
	strategy selected;
};

template<typename Type>
struct iter {
	Type* value;
};

template<typename Type>
Type& operator*(iter<Type> it) {
	return *it.value;
}

namespace detail {
	template<typename Left, typename Right>
	void iter_swap(Left, Right) = delete;

	template<typename Left, typename Right>
	concept has_adl_iter_swap = requires(Left left, Right right) {
		iter_swap(left, right);
	};
}

struct iter_swap_fn {
	template<typename Left, typename Right>
	static constexpr choice<Left> choose() {
		if constexpr (detail::has_adl_iter_swap<Left, Right>) {
			return {strategy::none};
		}
		return {strategy::exchange};
	}

	template<typename Left, typename Right>
	static constexpr choice<Left> selected = choose<Left, Right>();

	template<typename Left, typename Right>
		requires (selected<Left, Right>.selected != strategy::none)
	void operator()(Left left, Right right) const {
		constexpr strategy active = selected<Left, Right>.selected;
		if constexpr (active == strategy::exchange) {
			auto temp = *left;
			*left = *right;
			*right = temp;
		}
	}
};

namespace ranges {
	inline constexpr iter_swap_fn iter_swap{};
}

int main() {
	int left = 1;
	int right = 2;
	ranges::iter_swap(iter<int>{&left}, iter<int>{&right});
	return left == 2 && right == 1 ? 0 : 1;
}
