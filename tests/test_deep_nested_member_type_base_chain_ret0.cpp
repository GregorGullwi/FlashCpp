template <int Depth>
struct DeepNestedMemberTypeBaseChain;

template <int Depth>
struct NestedMemberTypeBaseSelector {
	struct Nested {
		using type = DeepNestedMemberTypeBaseChain<Depth - 1>;
	};
};

template <int Depth>
struct DeepNestedMemberTypeBaseChain : NestedMemberTypeBaseSelector<Depth>::Nested::type {};

template <>
struct DeepNestedMemberTypeBaseChain<0> {
	long long value;
};

static_assert(sizeof(DeepNestedMemberTypeBaseChain<1024>) == sizeof(long long));

int main() {
	return 0;
}
