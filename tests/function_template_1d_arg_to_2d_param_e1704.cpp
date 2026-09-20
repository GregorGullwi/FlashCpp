template <int N>
int last(int a[2][N]) {
	return a[1][N - 1];
}

int main() {
	int values[3] = {1, 2, 3};
	return last<3>(values);
}
