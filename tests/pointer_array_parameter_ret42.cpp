int readLast(int (*values)[3]) { return values[1][2]; }
int main() {
	int values[2][3] = {{1, 2, 3}, {4, 5, 6}};
	return readLast(values) == 6 ? 42 : 0;
}
