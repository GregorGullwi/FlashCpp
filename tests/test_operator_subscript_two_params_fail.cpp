struct MatrixLike {
	int data[4];

	int& operator[](int row, int col) {
		return data[row + col];
	}
};

int main() {
	return 0;
}
