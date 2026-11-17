// Function declaration/definition matching must preserve an interleaved
// pointer-to-array parameter shape instead of reducing it to flat pointer levels.

int sum_row(int (*cells[2])[4]);

int sum_row(int (*cells[2])[4]) {
	return 0;
}

int main() {
	return 0;
}
