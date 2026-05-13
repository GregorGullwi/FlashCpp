template <auto V>
struct Pick {
	static constexpr int value = 0;
};

template <>
struct Pick<5> {
	static constexpr int value = 1;
};

template <>
struct Pick<5LL> {
	static constexpr int value = 44;
};

template <long long N>
struct Source {
	static constexpr long long value = N;
};

template <long long N>
struct Derived : Pick<Source<N>::value> {};

int main() {
	return Derived<5>::value;
}
