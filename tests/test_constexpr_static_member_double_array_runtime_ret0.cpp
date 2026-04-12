struct DoubleTable {
	static constexpr double values[4] = {1.25, 2.5, 3.75, 5.0};
};

constexpr double getAt(int index) {
	return DoubleTable::values[index];
}

constexpr double sumTable() {
	double sum = 0.0;
	for (double value : DoubleTable::values) {
		sum += value;
	}
	return sum;
}

static_assert(DoubleTable::values[0] == 1.25);
static_assert(DoubleTable::values[1] == 2.5);
static_assert(DoubleTable::values[2] == 3.75);
static_assert(DoubleTable::values[3] == 5.0);
static_assert(getAt(2) == 3.75);
static_assert(sumTable() == 12.5);

int main() {
	if (DoubleTable::values[0] != 1.25)
		return 1;
	if (DoubleTable::values[1] != 2.5)
		return 2;
	if (DoubleTable::values[2] != 3.75)
		return 3;
	if (DoubleTable::values[3] != 5.0)
		return 4;
	if (getAt(1) != 2.5)
		return 5;
	return sumTable() == 12.5 ? 0 : 6;
}
