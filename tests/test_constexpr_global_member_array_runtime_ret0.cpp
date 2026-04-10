struct IntArrayHolder {
	int values[3];
	constexpr IntArrayHolder() : values{10, 20, 12} {}
};

struct DoubleArrayHolder {
	double values[3];
	constexpr DoubleArrayHolder() : values{1.5, 2.5, 0.25} {}
};

constexpr IntArrayHolder ints{};
constexpr DoubleArrayHolder doubles{};

static_assert(ints.values[0] == 10);
static_assert(ints.values[1] == 20);
static_assert(ints.values[2] == 12);
static_assert(doubles.values[0] == 1.5);
static_assert(doubles.values[1] == 2.5);
static_assert(doubles.values[2] == 0.25);

int main() {
	if (ints.values[0] != 10)
		return 1;
	if (ints.values[1] != 20)
		return 2;
	if (ints.values[2] != 12)
		return 3;
	if (doubles.values[0] != 1.5)
		return 4;
	if (doubles.values[1] != 2.5)
		return 5;
	if (doubles.values[2] != 0.25)
		return 6;
	return 0;
}
