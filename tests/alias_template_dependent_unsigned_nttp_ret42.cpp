template <auto V>
struct Kind {
	static constexpr int value = 0;
};

template <unsigned V>
struct Kind<V> {
	static constexpr int value = 42;
};

template <unsigned N>
using AliasKind = Kind<N>;

template <unsigned N>
int read() {
	return AliasKind<N>::value;
}

int main() {
	return read<7>();
}
