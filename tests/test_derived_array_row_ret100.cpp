// Range-for over int[2][2] walks a row. The return value changes when that
// element loses the bound that remains after the outer array is peeled.
int main() {
	int grid[2][2] = {{10, 20}, {30, 40}};
	int sum = 0;
	for (int (&row)[2] : grid) {
		sum = sum + row[0];
	}
	return sum;
}
