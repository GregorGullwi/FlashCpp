using Grid = int[2][2];
using Rows = long long[2][3];
using Flat = int[2][2];
using Cube = int[2][2][2];

int main() {
	if (Grid{{1, 2}, {3, 4}}[1][0] != 3 || Grid{{1, 2}, {3, 4}}[0][1] != 2) {
		return 1;
	}
	if (Grid{{9, 8}}[1][0] != 0 || Grid{{9, 8}}[0][1] != 8) {
		return 2;
	}
	if (Rows{{-5, 6, 7}, {8, 9, 10}}[1][2] != 10 || Rows{{-5, 6, 7}, {8, 9, 10}}[0][0] != -5) {
		return 3;
	}
	if (Flat{1, 2, 3, 4}[1][0] != 3) {
		return 4;
	}
	if (Cube{{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}}[1][0][1] != 6) {
		return 5;
	}
	int row = 1;
	int column = 0;
	if (Grid{{1, 2}, {3, 4}}[row][column] != 3) {
		return 6;
	}
	return 0;
}
