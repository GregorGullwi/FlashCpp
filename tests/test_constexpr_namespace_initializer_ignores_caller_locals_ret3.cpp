// Evaluating a namespace-scope initializer must not see local variables of the
// function call that triggered it and happened to shadow a namespace name.

namespace N {
constexpr int base = 3;
constexpr int stored = base;
constexpr int read_stored() {
	int base = 10;
	return stored;
}
}

static_assert(N::read_stored() == 3, "initializer must not see caller locals");

int main() {
	return N::read_stored();
}
