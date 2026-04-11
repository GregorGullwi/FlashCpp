constexpr int ints[] = {10, 20, 30};
constexpr double doubles[] = {1.5, 2.5, 3.5, 4.5};
struct Pair {
	int first;
	short second;
};
constexpr Pair pairs[] = {{1, 2}, {3, 4}};
constexpr int ints_bytes = sizeof(ints);
constexpr int ints_count = sizeof(ints) / sizeof(ints[0]);
constexpr int doubles_bytes = sizeof(doubles);
constexpr int doubles_count = sizeof(doubles) / sizeof(doubles[0]);
constexpr int pairs_bytes = sizeof(pairs);
constexpr int pairs_count = sizeof(pairs) / sizeof(pairs[0]);

static_assert(ints_bytes == 12);
static_assert(ints_count == 3);
static_assert(doubles_bytes == 32);
static_assert(doubles_count == 4);
static_assert(pairs_count == 2);
static_assert(pairs_bytes == static_cast<int>(sizeof(Pair) * 2));

int main() {
	if (sizeof(ints) != 12) {
		return 1;
	}
	if (sizeof(ints) / sizeof(ints[0]) != 3) {
		return 2;
	}
	if (sizeof(doubles) != 32) {
		return 3;
	}
	if (sizeof(doubles) / sizeof(doubles[0]) != 4) {
		return 4;
	}
	if (sizeof(pairs) / sizeof(pairs[0]) != 2) {
		return 5;
	}
	if (sizeof(pairs) != sizeof(Pair) * 2) {
		return 6;
	}
	return 0;
}
