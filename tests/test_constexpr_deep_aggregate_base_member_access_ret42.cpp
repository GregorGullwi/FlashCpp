// Member lookup must not follow the source-controlled inheritance depth with
// native recursion. This chain instantiates 1,024 base subobjects and reads the
// root member from a constant object evaluated inside a constexpr function.

template <int Depth>
struct DeepChain : DeepChain<Depth - 1> {};

template <>
struct DeepChain<0> {
	int value;
};

constexpr int read_deep_member() {
	constexpr DeepChain<1024> chain{42};
	return chain.value;
}

static_assert(read_deep_member() == 42);

int main() {
	DeepChain<1024> runtime_chain{42};
	if (runtime_chain.value != 42) {
		return 1;
	}
	return 42;
}
