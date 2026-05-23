#include <cstddef>

template<typename...>
struct Tup {};

template<std::size_t...>
struct Idx {};

template<typename T1, typename T2>
struct Pair {
	template<typename... A1, std::size_t... I1, typename... A2, std::size_t... I2>
	Pair(Tup<A1...>&, Tup<A2...>&, Idx<I1...>, Idx<I2...>);
};

template<typename T1, typename T2>
template<typename... A1, std::size_t... I1, typename... A2, std::size_t... I2>
Pair<T1, T2>::Pair(Tup<A1...>&, Tup<A2...>&, Idx<I1...>, Idx<I2...>) {}

int main() {
	Tup<int> left;
	Tup<double> right;
	Pair<int, double> value(left, right, Idx<0>{}, Idx<0>{});
	(void)value;
	return 0;
}
