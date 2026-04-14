constexpr int globalFlat[][3] = {1, 2, 3, 4, 5, 6};
static_assert(sizeof(globalFlat) == sizeof(int) * 6);
static_assert(sizeof(globalFlat[0]) == sizeof(int) * 3);
static_assert(globalFlat[1][2] == 6);

constexpr int localFlat() {
	int local[][3] = {7, 8, 9, 10, 11, 12};
	return local[0][1] + local[1][2];
}

static_assert(localFlat() == 20);

int main() {
	if (globalFlat[0][0] != 1 || globalFlat[0][2] != 3 || globalFlat[1][0] != 4 || globalFlat[1][2] != 6)
		return 1;
	return localFlat() == 20 ? 0 : 1;
}
