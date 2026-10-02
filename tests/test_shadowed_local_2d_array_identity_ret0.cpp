int main() {
	int grid[2][2] = {{1, 2}, {3, 4}};
	{
		int grid[2][2] = {{5, 6}, {7, 8}};
		if (grid[1][1] != 8 || grid[0][0] != 5) return 1;
	}
	if (grid[0][0] != 1 || grid[1][1] != 4) return 2;
	return 0;
}