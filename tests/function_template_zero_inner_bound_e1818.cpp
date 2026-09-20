template <int N>
int last(int a[2][N]) {
	return a[0][0];
}

int main() {
	return last<0>(nullptr);
}
