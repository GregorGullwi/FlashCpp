template <auto V>
struct Kind {
	static constexpr int value = 0;
};

template <unsigned V>
struct Kind<V> {
	static constexpr int value = 42;
};

template <unsigned V>
struct Source {
	static constexpr unsigned value = V;
};

template <unsigned N>
struct Derived : Kind<Source<N>::value> {};

int main() {
	return Derived<7>::value;
}
