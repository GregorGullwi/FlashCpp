struct FactEmpty {};

struct FactLeft : FactEmpty {};
struct FactRight : FactEmpty {};
struct FactRepeatedBase : FactLeft, FactRight {};

template <int Depth>
struct FactChain;

template <int Depth>
struct FactChain : FactChain<Depth - 1> {};

template <>
struct FactChain<0> {};

static_assert(__is_standard_layout(FactChain<64>));
static_assert(__is_empty(FactChain<64>));
static_assert(__is_trivially_constructible(FactChain<64>));
static_assert(__is_nothrow_constructible(FactChain<64>));
static_assert(!__is_standard_layout(FactRepeatedBase));

int main() {
	return 0;
}
