// Two-dimensional ordered shapes where the record is 16 bytes and a pointer is
// 8: the array of pointers must stay 16 bytes even though its rows have record
// elements. A size taken by multiplying the 16-byte record through every extent
// would report 96 for `grid` and 96 for `rows`.
struct Wide {
	long long a;
	long long b;
};

struct GridHolder {
	Wide* (*grid[2])[3];
};

int main() {
	Wide* (*grid[2])[3];
	Wide (*rows[2])[3];
	if (sizeof(Wide) != 16) {
		return 1;
	}
	if (sizeof(grid) != 16) {
		return 2;
	}
	if (sizeof(rows) != 16) {
		return 3;
	}
	if (sizeof(*grid) != 8) {
		return 4;
	}
	if (sizeof(**rows) != 48) {
		return 5;
	}
	if (sizeof(**grid) != 24) {
		return 6;
	}
	if (sizeof(Wide[2][3]) != 96) {
		return 7;
	}
	if (sizeof(GridHolder) != 16) {
		return 8;
	}
	return 47;
}
