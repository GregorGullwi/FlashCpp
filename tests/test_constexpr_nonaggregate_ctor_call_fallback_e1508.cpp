// Per C++20, a type with a user-declared constructor is not an aggregate.
// A constructor-style brace expression must not fall back to aggregate member
// initialization when no matching constructor exists.

struct Pair {
	int first;
	int second;
	constexpr Pair(int value) : first(value), second(value + 1) {}
};

constexpr Pair p = Pair{1, 2};	// ERROR: no matching 2-arg constructor

int main() {
	return p.first;
}
