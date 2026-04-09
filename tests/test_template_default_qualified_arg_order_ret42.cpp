template <int Z, int A>
struct pair_value {
	static constexpr int value = Z * 10 + A;
};

template <int Z, int A, int V = pair_value<Z, A>::value>
struct holder {
	static constexpr int value = V;
};

int main() {
	return holder<4, 2>::value;
}
