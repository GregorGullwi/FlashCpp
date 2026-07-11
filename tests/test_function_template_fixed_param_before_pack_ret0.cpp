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

template <class T>
struct ReferenceKind {
	static constexpr int value = 0;
};

template <class T>
struct ReferenceKind<T&> {
	static constexpr int value = 1;
};

template <class T>
struct ReferenceKind<const T&> {
	static constexpr int value = 2;
};

template <class First, class... Rest>
constexpr int deductionCode(First&&, Rest&&...) {
	return ReferenceKind<First>::value * 10 + sizeof...(Rest);
}

int main() {
	Sum sum;
	int value = 42;
	const int const_value = 42;
	using LvalueResult = decltype(invokeLike(sum, value));
	using RvalueResult = decltype(invokeLike(Sum{}, 42));
	if (sizeof(LvalueResult) != sizeof(int)) {
		return 1;
	}
	if (sizeof(RvalueResult) != sizeof(int)) {
		return 2;
	}
	if (deductionCode(value, static_cast<short>(1), Sum{}) != 12) {
		return 3;
	}
	if (deductionCode(const_value, 1) != 21) {
		return 4;
	}
	if (deductionCode(42, 1) != 1) {
		return 5;
	}
	return deductionCode<int&>(value) == 10 ? 0 : 6;
}
