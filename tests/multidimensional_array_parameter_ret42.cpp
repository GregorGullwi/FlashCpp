int readAt(int values[2][3], int row, int column) {
	return values[row][column];
}
int readCube(int values[2][3][4], int plane, int row, int column) {
	return values[plane][row][column];
}
int main() {
	int values[2][3] = {{1, 2, 3}, {4, 5, 6}};
	int cube[2][3][4] = {};
	cube[1][2][3] = 17;
	return readAt(values, 1, 2) == 6 && readAt(values, 0, 1) == 2 &&
		readCube(cube, 1, 2, 3) == 17 ? 42 : 0;
}
