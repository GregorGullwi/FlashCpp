// Partial specialization patterns may name a pack inside a nested template-id
// (`List<Head, Tail...>`). The remaining inner arguments must all bind to that
// pack, including the empty remainder after the last peel.

template <typename...>
struct List {};

template <typename Head, typename... Tail>
struct List<Head, Tail...> : List<Tail...> {};

template <typename>
struct RestCount;

template <typename Head, typename... Tail>
struct RestCount<List<Head, Tail...>> {
	static constexpr int value = sizeof...(Tail);
};

struct Probe {
	short field;
};

int main() {
	using Five = List<char, unsigned char, Probe, int, long long>;
	using One = List<unsigned int>;
	return (RestCount<Five>::value == 4 && RestCount<One>::value == 0) ? 0 : 1;
}
