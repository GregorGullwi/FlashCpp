// C++20 [conv.array]/[expr.add]: `values - other` decays the array operand and
// yields ptrdiff_t before the enclosing comparison runs. A difference used
// directly as a comparison operand must reach the same operand type as a named
// intermediate; the array-shaped left operand used to leave the difference
// without a sema-owned type and IR generation rejected the comparison.

int main() {
	int values[4] = {1, 2, 3, 4};
	int* other = values + 1;

	if (values - other != -1) {
		return 1;
	}
	if (!(values - other < 0)) {
		return 2;
	}
	if (values - other == 0) {
		return 3;
	}
	if (values - other > 0) {
		return 4;
	}
	if (other - values != 1) {
		return 5;
	}
	if (!(other - values > 0)) {
		return 6;
	}
	if (!(values - values == 0)) {
		return 7;
	}

	// The comparison operand can be a named integer; only the difference is
	// direct.
	long limit = 0;
	if (!(values - other < limit)) {
		return 8;
	}
	if (values - other >= limit) {
		return 9;
	}
	if (!(values - other <= limit)) {
		return 10;
	}

	// Named intermediates stay the control for the same expressions.
	long named = values - other;
	if (named != -1) {
		return 11;
	}
	auto deduced = other - values;
	if (!(deduced > limit)) {
		return 12;
	}

	// Pointer-pointer operands and a bool assignment of the direct comparison.
	if (!((other - values) > 0)) {
		return 13;
	}
	bool direct = values - other < 0;
	if (!direct) {
		return 14;
	}

	// Rows of a two-dimensional array decay to their first element, so the
	// difference counts ints while the address difference counts rows.
	int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};
	if (grid[1] - grid[0] != 3) {
		return 15;
	}
	if (&grid[1] - &grid[0] != 1) {
		return 16;
	}
	if (!(grid[1] - grid[0] > limit)) {
		return 17;
	}
	if (&grid[1] - &grid[0] == 0) {
		return 18;
	}

	// A record row pointer difference keeps the row stride.
	struct Cell {
		long long value;
	};
	Cell cells[2][2];
	if (cells[1] - cells[0] != 2) {
		return 19;
	}
	if (&cells[1] - &cells[0] != 1) {
		return 20;
	}

	return 0;
}
