template <int N>
struct IntConstant {
	static constexpr int value = N;
};

template <typename T>
struct Holder {
	static constexpr int value = T::value;

	template <typename U>
	static int add_value() {
		return value + U::value;
	}
};

template <typename A, typename B>
int check_disambiguation() {
	const bool less = Holder<A>::value < Holder<B>::value;
	const int sum = Holder<A>::template add_value<IntConstant<1>>();
	if (!less) {
		return 1;
	}
	return sum == (A::value + 1) ? 0 : 2;
}

int main() {
	return check_disambiguation<IntConstant<2>, IntConstant<5>>();
}
