template <auto V>
struct Pick {
	static constexpr int value = 0;
};

template <>
struct Pick<7> {
	static constexpr int value = 1;
};

template <>
struct Pick<7L> {
	static constexpr int value = 43;
};

template <long N>
struct Source {
	static constexpr long value = N;
};

template <long N>
struct Derived : Pick<Source<N>::value> {};

int main() {
	return Derived<7>::value;
}
