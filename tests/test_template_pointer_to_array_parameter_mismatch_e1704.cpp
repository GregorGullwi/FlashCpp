template<class T>
int row(T (*value)[2]) {
	return (*value)[0];
}

int main() {
	int rows[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
	return row(rows);
}
