template<typename Type>
concept movable = true;

struct swap_fn {
	template<typename Type>
		requires movable<Type>
	void operator()(Type& left, Type& right) const {
		Type temp = left;
		left = right;
		right = temp;
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
