template <int Depth>
struct DeepPackExpandedBaseChain;

template <int... Depths>
struct ExpandDeepBaseChain : DeepPackExpandedBaseChain<Depths>... {};

template <int Depth>
struct DeepPackExpandedBaseChain : ExpandDeepBaseChain<Depth - 1> {};

template <>
struct DeepPackExpandedBaseChain<0> {
	char value;
};

static_assert(sizeof(DeepPackExpandedBaseChain<1024>) == sizeof(char));

int main() {
	return 0;
}
