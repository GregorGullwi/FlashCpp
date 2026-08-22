// Regression: local pointer-to-array initialization must keep its initializer
// and support indexing through the dereferenced pointer.
// Covers int (*p)[N] = &local; then (*p)[i] reads/writes.

int main() {
	int data[3] = {10, 20, 30};
	int(*ptr)[3] = &data;
	if ((*ptr)[0] != 10) {
		return 1;
	}
	(*ptr)[1] = 42;
	if (data[1] != 42) {
		return 2;
	}
	if ((*ptr)[2] + (*ptr)[0] != 40) {
		return 3;
	}

	long mixed[4] = {100L, 200L, 300L, 400L};
	long(*lptr)[4] = &mixed;
	if ((*lptr)[3] != 400L) {
		return 4;
	}
	(*lptr)[0] = 5L;
	if (mixed[0] != 5L) {
		return 5;
	}

	char text[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
	char(*cptr)[6] = &text;
	if ((*cptr)[4] != 'e') {
		return 6;
	}

	struct Pair {
		int first;
		int second;
	};
	Pair pairs[2] = {{1, 2}, {3, 4}};
	Pair(*pptr)[2] = &pairs;
	if ((*pptr)[1].second != 4) {
		return 7;
	}
	(*pptr)[0].first = 9;
	if (pairs[0].first != 9) {
		return 8;
	}

	return 42;
}
