constexpr int globalGrid[][3] = {{1, 2, 3}, {4, 5, 6}};
static_assert(sizeof(globalGrid) == sizeof(int) * 6);
static_assert(sizeof(globalGrid[0]) == sizeof(int) * 3);
static_assert(sizeof(globalGrid) / sizeof(globalGrid[0]) == 2);
static_assert(globalGrid[1][2] == 6);

constexpr int localUnsizedMultidim() {
	int localGrid[][2] = {{10, 20}, {30, 40}};
	return localGrid[0][1] + localGrid[1][0];
}

static_assert(localUnsizedMultidim() == 50);

// Regression: explicitly-sized multidimensional arrays must still work for sizeof(arr[0])
constexpr int explicitGrid[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
static_assert(sizeof(explicitGrid) == sizeof(int) * 8);
static_assert(sizeof(explicitGrid[0]) == sizeof(int) * 4);
static_assert(sizeof(explicitGrid) / sizeof(explicitGrid[0]) == 2);

// 3D unsized-first: exercises trailing-dims product logic with >1 trailing dimension
constexpr int cube[][2][3] = {{{1,2,3},{4,5,6}}, {{7,8,9},{10,11,12}}};
static_assert(sizeof(cube) == sizeof(int) * 12);
static_assert(sizeof(cube[0]) == sizeof(int) * 6);
static_assert(sizeof(cube) / sizeof(cube[0]) == 2);
static_assert(cube[1][1][2] == 12);

// Partial inner init / zero-fill: inner braces shorter than inner dimension
constexpr int sparse[][3] = {{1}, {2}};
static_assert(sizeof(sparse) == sizeof(int) * 6);
static_assert(sizeof(sparse) / sizeof(sparse[0]) == 2);
static_assert(sparse[0][0] == 1);
static_assert(sparse[1][0] == 2);

int main() {
	if (globalGrid[0][0] != 1 || globalGrid[0][1] != 2 || globalGrid[0][2] != 3)
		return 1;
	if (globalGrid[1][0] != 4 || globalGrid[1][1] != 5 || globalGrid[1][2] != 6)
		return 2;
	if (explicitGrid[0][0] != 1 || explicitGrid[0][3] != 4 || explicitGrid[1][0] != 5 || explicitGrid[1][3] != 8)
		return 3;
	if (cube[0][0][0] != 1 || cube[0][1][2] != 6 || cube[1][0][0] != 7 || cube[1][1][2] != 12)
		return 4;
	if (sparse[0][0] != 1 || sparse[0][1] != 0 || sparse[0][2] != 0 ||
		sparse[1][0] != 2 || sparse[1][1] != 0 || sparse[1][2] != 0)
		return 5;
	return 0;
}
