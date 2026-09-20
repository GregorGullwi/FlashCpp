template <int N>
int last(int a[2][N]) {
	return a[1][N - 1];
}

int lastDirect(int a[2][3]) {
	return a[1][2];
}

template <int N>
int successorLast(int a[2][N + 1]) {
	return a[1][N];
}

int main() {
	int values[2][3] = {{1, 2, 3}, {4, 5, 6}};
	int successor[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
	return last<3>(values) == 6 && lastDirect(values) == 6 &&
		successorLast<3>(successor) == 8 ? 42 : 0;
}
