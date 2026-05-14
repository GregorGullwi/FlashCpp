template <auto V>
struct Pick {
	static constexpr int value = 0;
};

template <>
struct Pick<7> {
	static constexpr int value = 1;
};

template <>
struct Pick<7u> {
	static constexpr int value = 42;
};

template <unsigned N>
struct Source {
	static constexpr unsigned value = N;
};

template <unsigned N>
struct Derived : Pick<Source<N>::value> {};

int main() {
	return Derived<7>::value;
}
