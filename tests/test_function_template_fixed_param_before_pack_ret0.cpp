// Regression: function-template deduction must preserve and use fixed function
// parameters that precede a trailing function parameter pack.

template <class Callable>
constexpr auto invokeLike(Callable&& callable)
	-> decltype(static_cast<Callable&&>(callable)()) {
	return static_cast<Callable&&>(callable)();
}

template <class Callable, class First, class... Rest>
constexpr auto invokeLike(Callable&& callable, First&& first, Rest&&... rest)
	noexcept(noexcept(static_cast<Callable&&>(callable)(
		static_cast<First&&>(first), static_cast<Rest&&>(rest)...)))
	-> decltype(static_cast<Callable&&>(callable)(
		static_cast<First&&>(first), static_cast<Rest&&>(rest)...)) {
	return static_cast<Callable&&>(callable)(
		static_cast<First&&>(first), static_cast<Rest&&>(rest)...);
}

struct Sum {
	constexpr int operator()(int value) const {
		return value;
	}
};

int main() {
	Sum sum;
	int value = 42;
	using LvalueResult = decltype(invokeLike(sum, value));
	using RvalueResult = decltype(invokeLike(Sum{}, 42));
	if (sizeof(LvalueResult) != sizeof(int)) {
		return 1;
	}
	return sizeof(RvalueResult) == sizeof(int) ? 0 : 2;
}
