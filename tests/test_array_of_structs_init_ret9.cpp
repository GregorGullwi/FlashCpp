struct Pair {
	int x;
	int y;
};

int main() {
	Pair arr[3] = {{1, 2}, {3, 4}, {5, 6}};
	return arr[0].x + arr[1].x + arr[2].x; // 1+3+5 = 9
}
