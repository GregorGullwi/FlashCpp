struct Cell {
	int value;
	short tag;
	int read() { return value + tag; }
};

int add(int& target, int value) {
	target += value;
	return target;
}

int main() {
	int total = 0;
	char tiny = 2;
	short small = 3;
	double wide = 4.5;
	int matrix[2][2] = {{1, 2}, {3, 4}};
	Cell cell = {5, 6};
	int& alias = total;
	alias += tiny + small + static_cast<int>(wide) + matrix[1][0] + cell.read();
	if (total != 23) return 1;

	{
		int total = 10;
		Cell cell = {20, 2};
		int matrix[2][2] = {{7, 8}, {9, 10}};
		int& alias = total;
		add(alias, cell.read() + matrix[1][1]);
		if (total != 42) return 2;
		{
			int total = 100;
			total += matrix[0][1];
			if (total != 108) return 3;
		}
		if (total != 42) return 4;
	}
	if (total != 23 || cell.read() != 11 || matrix[1][1] != 4) return 5;

	int caught = 0;
	try {
		throw 12;
	} catch (int error) {
		caught += error;
	}
	try {
		throw 13;
	} catch (int error) {
		caught += error;
	}
	if (caught != 25) return 6;
	return 0;
}