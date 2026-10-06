// A directly spelled reference to an array of unknown bound is a valid
// function parameter type and binds an array argument without array decay.
int sum(int (&values)[]) {
	return values[0] + values[1] + values[2];
}

int diagonal_sum(int (&values)[][2]) {
	return values[0][0] + values[1][1];
}

int main() {
	int values[3] = {4, 5, 6};
	if (sum(values) != 15) return 1;
	int matrix[2][2] = {{7, 8}, {9, 10}};
	if (diagonal_sum(matrix) != 17) return 2;
	return 0;
}
