template <int Depth>
struct DeepMemberTypeBaseChain;

template <int Depth>
struct SelectDeepMemberTypeBase {
	using type = DeepMemberTypeBaseChain<Depth - 1>;
};

template <int Depth>
struct DeepMemberTypeBaseChain : SelectDeepMemberTypeBase<Depth>::type {};

template <>
struct DeepMemberTypeBaseChain<0> {
	char value;
};

static_assert(sizeof(DeepMemberTypeBaseChain<1024>) == sizeof(char));

int main() {
	return 0;
}
