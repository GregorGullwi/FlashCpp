// A dependent NTTP default must preserve its canonical identity when a class
// specialization is imported through the instantiated declaration.
template <typename Left, typename Right>
struct IsSame {
	static constexpr bool value = false;
};

template <typename Type>
struct IsSame<Type, Type> {
	static constexpr bool value = true;
};

template <int N, int M = N + 1>
struct Pair {
	static constexpr int value = 0;
};

template <>
struct Pair<3, 4> {
	static constexpr int value = 17;
};

template <int N>
struct Holder {
	using type = Pair<N>;
};

template <int N>
struct ComputedHolder {
	using type = Pair<N, N + 1>;
};

int main() {
	using Defaulted = typename Holder<3>::type;
	using Explicit = Pair<3, 4>;
	if (!IsSame<Pair<3>, Explicit>::value) {
		return 2;
	}
	if (!IsSame<typename ComputedHolder<3>::type, Explicit>::value) {
		return 3;
	}
	if (Defaulted::value != 17) {
		return 4;
	}
	return IsSame<Defaulted, Explicit>::value ? 0 : 1;
}
