template <class T>
using Matrix = T[2][3];
long long readAlias(Matrix<long long> values, int row, int column) {
	return values[row][column];
}
int main() {
	Matrix<long long> values = {{1, 2, 3}, {4, 5, 6}};
	return readAlias(values, 1, 2) == 6 && readAlias(values, 0, 1) == 2 &&
		sizeof(values) == 6 * sizeof(long long) ? 42 : 0;
}
