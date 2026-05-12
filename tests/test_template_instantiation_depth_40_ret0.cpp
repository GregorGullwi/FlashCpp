// Regression: libstdc++ container headers require template-instantiation
// nesting deeper than 24 levels in non-recursive chains.

template<int N>
struct Nest : Nest<N - 1> {
};

template<>
struct Nest<0> {
};

int main() {
	using DeepType = Nest<30>;
	return sizeof(DeepType) > 0 ? 0 : 1;
}
