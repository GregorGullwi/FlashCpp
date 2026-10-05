template <int Depth>
struct DeepBaseChain : DeepBaseChain<Depth - 1> {
};

template <>
struct DeepBaseChain<0> {
	char value;
};

static_assert(sizeof(DeepBaseChain<1024>) == sizeof(char));

int main() {
	return 0;
}
