constexpr int globalGrid[][3] = {{1, 2, 3}, {4, 5, 6}};
static_assert(sizeof(globalGrid) == sizeof(int) * 6);
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

int main() {
	return 0;
}
