constexpr int globalGrid[][3] = {{1, 2, 3}, {4, 5, 6}};
static_assert(sizeof(globalGrid) == sizeof(int) * 6);
static_assert(sizeof(globalGrid) / sizeof(globalGrid[0]) == 2);
static_assert(globalGrid[1][2] == 6);

constexpr int localUnsizedMultidim() {
	int localGrid[][2] = {{10, 20}, {30, 40}};
	return localGrid[0][1] + localGrid[1][0];
}

static_assert(localUnsizedMultidim() == 50);

int main() {
	return 0;
}
