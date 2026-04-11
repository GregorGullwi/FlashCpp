constexpr int ints[] = {10, 20, 30};
constexpr double doubles[] = {1.5, 2.5, 3.5, 4.5};

static_assert(sizeof(ints) == 12);
static_assert(sizeof(ints) / sizeof(ints[0]) == 3);
static_assert(sizeof(doubles) == 32);
static_assert(sizeof(doubles) / sizeof(doubles[0]) == 4);

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
