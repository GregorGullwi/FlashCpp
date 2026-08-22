// Regression: pointer-to-array function parameters must keep their type
// (C++20 [dcl.fct]/5 - no extra array-to-pointer adjustment) and calls must
// accept both a pointer-to-array lvalue and &array arguments.
void setFirst(int (*arr)[3], int val) {
	(*arr)[0] = val;
	(*arr)[2] = val * 3;
}

long sumAll(long (*vals)[4]) {
	long total = 0L;
	for (int i = 0; i < 4; ++i) {
		total += (*vals)[i];
	}
	return total;
}

struct Pair {
	int first;
	int second;
};

void bumpSecond(Pair (*items)[2], int by) {
	(*items)[0].second += by;
	(*items)[1].second += by;
}

int main() {
	int data[3] = {1, 2, 3};
	setFirst(&data, 7);
	if (data[0] != 7 || data[2] != 21 || data[1] != 2) {
		return 1;
	}

	int(*alias)[3] = &data;
	setFirst(alias, 5);
	if (data[0] != 5) {
		return 2;
	}

	long vals[4] = {10L, 20L, 30L, 40L};
	if (sumAll(&vals) != 100L) {
		return 3;
	}

	Pair pairs[2] = {{1, 2}, {3, 4}};
	bumpSecond(&pairs, 6);
	if (pairs[0].second != 8 || pairs[1].second != 10) {
		return 4;
	}
	if ((*&pairs)[0].first != 1) {
		return 5;
	}

	return 42;
}
