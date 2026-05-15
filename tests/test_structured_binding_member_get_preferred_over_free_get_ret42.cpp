// Regression: structured binding tuple-like lookup must prefer member get<I>()
// over non-member get<I>(e) when both are available.

namespace std {
	template <typename T>
	struct tuple_size;

	template <unsigned long I, typename T>
	struct tuple_element;
} // namespace std

struct Pair {
	int first;
	int second;

	template <unsigned long I>
	int get() const;
};

template <>
int Pair::get<0>() const { return first; }

template <>
int Pair::get<1>() const { return second; }

// Deliberately conflicting free get specializations. If chosen, the result is wrong.
template <unsigned long I>
typename std::tuple_element<I, Pair>::type get(const Pair& p);

template <>
int get<0>(const Pair&) { return -100; }

template <>
int get<1>(const Pair&) { return -200; }

namespace std {
	template <>
	struct tuple_size<Pair> {
		static constexpr unsigned long value = 2;
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

int main() {
	Pair p{10, 32};
	auto [a, b] = p;
	return a + b;
}
