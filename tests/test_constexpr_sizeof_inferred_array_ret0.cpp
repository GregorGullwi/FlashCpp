constexpr int ints[] = {10, 20, 30};
constexpr double doubles[] = {1.5, 2.5, 3.5, 4.5};
constexpr int ints_bytes = sizeof(ints);
constexpr int ints_count = sizeof(ints) / sizeof(ints[0]);
constexpr int doubles_bytes = sizeof(doubles);
constexpr int doubles_count = sizeof(doubles) / sizeof(doubles[0]);

static_assert(ints_bytes == 12);
static_assert(ints_count == 3);
static_assert(doubles_bytes == 32);
static_assert(doubles_count == 4);

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
	return 0;
}
