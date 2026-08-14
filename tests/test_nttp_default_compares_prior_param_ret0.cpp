// A later NTTP default may name an earlier NTTP and a type parameter member,
// as in libstdc++ __select: `bool = (_Sz <= _Tp::__size)`.

template <unsigned long long Sz, typename Tp, bool = (Sz <= Tp::size)>
struct Fits {
	static constexpr int kind = 0;
};

template <unsigned long long Sz, typename Tp>
struct Fits<Sz, Tp, true> {
	static constexpr int kind = 1;
};

struct Small {
	char value;
	static constexpr unsigned long long size = sizeof(char);
};

struct Large {
	long long values[2];
	static constexpr unsigned long long size = sizeof(long long) * 2;
};

int main() {
	const int small_fits = Fits<sizeof(char), Small>::kind;
	const int small_miss = Fits<sizeof(Large), Small>::kind;
	const int large_fits = Fits<sizeof(Large), Large>::kind;
	return (small_fits == 1 && small_miss == 0 && large_fits == 1) ? 0 : 1;
}
