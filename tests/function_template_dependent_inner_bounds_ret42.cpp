template <int N>
short lastRow(short a[3][N]) {
	return a[2][N - 1];
}

template <int N, int M>
int cubeCorner(int a[2][N][M]) {
	return a[1][N - 1][M - 1];
}

struct Cell {
	long v;
};

template <int N>
long lastCell(Cell a[2][N]) {
	return a[1][N - 1].v;
}

int cubeDirect(int a[2][3][4]) {
	return a[1][2][3];
}

int main() {
	short rows[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
	int cube[2][3][4] = {};
	cube[1][2][3] = 17;
	Cell cells[2][2] = {};
	cells[1][1].v = 8;
	return lastRow<4>(rows) == 12 && cubeCorner<3, 4>(cube) == 17 &&
		cubeDirect(cube) == 17 && lastCell<2>(cells) == 8 ? 42 : 0;
}
