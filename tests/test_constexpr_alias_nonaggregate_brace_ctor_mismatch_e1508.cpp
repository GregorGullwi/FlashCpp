// Per C++20, a type with a user-declared constructor is not an aggregate.
// Brace initialization must therefore resolve a constructor rather than
// falling back to direct member initialization, even through an alias type.

struct PairImpl {
	int first;
	int second;
	constexpr PairImpl(int value) : first(value), second(value + 1) {}
};

using Pair = PairImpl;

// Expected diagnostic: no matching 2-arg constructor for PairImpl/Pair.
constexpr Pair p{1, 2};	// ERROR: no matching 2-arg constructor

int main() {
	return p.first;
}
