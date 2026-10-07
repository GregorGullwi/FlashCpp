struct Cell {
	short value;
	char tag;
};

struct Container {
	char padding[3];
	Cell cells[2][3];
	int values[2][3];
};

int main() {
	Container container;
	container.cells[1][2].value = 31;
	container.cells[0][1].tag = 'x';
	container.values[1][1] = 11;
	if (container.cells[1][2].value != 31) return 1;
	if (container.cells[0][1].tag != 'x') return 2;
	if (container.values[1][1] != 11) return 3;
	return 0;
}
