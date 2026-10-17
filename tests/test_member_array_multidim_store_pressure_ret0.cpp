// Regression: sequential stores into multidimensional member arrays must not
// clobber the computed element address. The store's destination address and the
// stored value must live in distinct registers, even after enough stores to put
// the backend register allocator under pressure.
struct Grid {
	int cells[2][2];
	char bytes[2][2];
	double reals[2][2];
};

struct Overlay {
	int tag;
	union {
		long long wide;
		short halves[2][2];
	};
};

int main() {
	Grid grid;
	grid.cells[0][0] = 1;
	grid.cells[0][1] = 2;
	grid.cells[1][0] = 3;
	grid.cells[1][1] = 7;
	if (grid.cells[0][0] != 1 || grid.cells[0][1] != 2) return 1;
	if (grid.cells[1][0] != 3 || grid.cells[1][1] != 7) return 2;

	grid.bytes[0][0] = 'a';
	grid.bytes[0][1] = 'b';
	grid.bytes[1][0] = 'c';
	grid.bytes[1][1] = 'd';
	if (grid.bytes[0][1] != 'b' || grid.bytes[1][1] != 'd') return 3;

	grid.reals[0][0] = 1.5;
	grid.reals[0][1] = 2.5;
	grid.reals[1][0] = 3.5;
	grid.reals[1][1] = 4.5;
	if (grid.reals[1][1] != 4.5 || grid.reals[0][0] != 1.5) return 4;

	Overlay overlay;
	overlay.tag = 9;
	overlay.halves[0][0] = 11;
	overlay.halves[0][1] = 12;
	overlay.halves[1][0] = 13;
	overlay.halves[1][1] = 14;
	if (overlay.halves[1][1] != 14 || overlay.halves[0][0] != 11) return 5;

	{
		Grid grid;
		grid.cells[0][0] = 21;
		grid.cells[0][1] = 22;
		grid.cells[1][0] = 23;
		grid.cells[1][1] = 24;
		if (grid.cells[1][1] != 24) return 6;
	}
	if (grid.cells[1][1] != 7) return 7;
	return 0;
}
