// Regression: structured binding tuple-like lookup must prefer member get<I>()
// over non-member get<I>(e) when both are available.
namespace std {
	using size_t = decltype(sizeof(0));

	template <typename T>
	struct tuple_size;

	template <size_t I, typename T>
	struct tuple_element;
}

struct Pair {
	int first;
	int second;

	template <std::size_t I>
	int get() const;
};

template <>
int Pair::get<0>() const { return first; }

template <>
int Pair::get<1>() const { return second; }

namespace std {
	template <>
	struct tuple_size<Pair> {
		static constexpr std::size_t value = 2;
	};

	template <>
	struct tuple_element<0, Pair> {
		using type = int;
	};

	template <>
	struct tuple_element<1, Pair> {
		using type = int;
	};
} // namespace std

// Deliberately conflicting free get specializations. If chosen, the result is wrong.
template <std::size_t I>
typename std::tuple_element<I, Pair>::type get(const Pair& p);

template <>
int get<0>(const Pair&) { return -100; }

template <>
int get<1>(const Pair&) { return -200; }

int main() {
	Pair p{10, 32};
	auto [a, b] = p;
	return a + b;
}
