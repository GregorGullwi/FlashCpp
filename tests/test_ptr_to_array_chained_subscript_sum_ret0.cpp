// Protects register ownership when scalar assignment materializes a subscript base
// between chained pointer-to-array element loads.
struct Item {
	int value;
	short delta;
};

int ints[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
long longs[1][2][4] = {{{10, 20, 30, 40}, {50, 60, 70, 80}}};
Item item = {7, 2};

int checkIntSums() {
	int (*view)[4] = &ints[0];
	int three_term = (*view)[0] + (*view)[1] + (*view)[2];
	int four_term = (*view)[0] + (*view)[1] + (*view)[2] + (*view)[3];
	return (three_term - 6) + (four_term - 10);
}

long checkLongMultiBound() {
	long (*view)[2][4] = &longs[0];
	return (*view)[0][0] + (*view)[0][1] + (*view)[1][3] - 110;
}

int checkStructMixedSum() {
	int (*int_view)[4] = &ints[0];
	int mixed = item.value + (*int_view)[1] + item.delta + (*int_view)[2];
	return mixed - 14;
}

int main() {
	int failures = 0;
	if (checkIntSums() != 0) {
		failures += 1;
	}
	if (checkLongMultiBound() != 0) {
		failures += 2;
	}
	if (checkStructMixedSum() != 0) {
		failures += 4;
	}
	return failures;
}
