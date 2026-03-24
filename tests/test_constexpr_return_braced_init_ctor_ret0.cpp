// Test: constexpr braced return for a non-aggregate type must use constructor
// list-initialization, not direct aggregate member binding.

struct Pair {
	int first;
	int second;

	constexpr Pair(int x, int y)
		: first(y), second(x + 1) {}
};

constexpr Pair makePair(int x, int y) {
	return {x, y};
}

constexpr Pair p = makePair(2, 5);

static_assert(p.first == 5);
static_assert(p.second == 3);

int main() {
	return 0;
}
