// C++20 [expr.cond]/3: each conditional operand undergoes array-to-pointer
// decay before the common type is chosen, including when both branches share
// the same array type — a shape where no branch-to-common conversion is
// recorded. Local array branches used to be copied by value, so the decayed
// pointer held the first element's bytes instead of the array's address and
// neither the pointer comparison nor an element read could agree on the value.
// Compare against a scalar anchor so binary != sees two pointers and only the
// conditional's decay is under test.
struct Pair {
	int first;
	int second;
};

int main() {
	int local[3] = {7, 8, 9};
	int* anchor = &local[0];
	int* decayed = (1 == 1) ? local : local;
	if (decayed != anchor) {
		return 3;
	}
	if (decayed[2] != 9) {
		return 4;
	}
	long wide[2] = {4096L, 8192L};
	long* wide_anchor = &wide[0];
	long* wide_decay = (1 == 0) ? wide : wide;
	if (wide_decay != wide_anchor) {
		return 5;
	}
	if (wide_decay[0] != 4096L) {
		return 6;
	}
	Pair pairs[2] = {{1, 2}, {3, 4}};
	Pair* pair_anchor = &pairs[0];
	Pair* pair_decay = (1 == 1) ? pairs : pairs;
	if (pair_decay != pair_anchor) {
		return 7;
	}
	if (pair_decay[1].second != 4) {
		return 8;
	}
	return 42;
}
