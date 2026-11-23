// The same pointee-qualification rule applies to a record pointee: a
// pointer-to-array of const objects cannot drop that const for a by-value
// pointer-to-array parameter.
struct Cell {
	int value;
};

int read_cells(Cell (*rows)[2]) {
	return (*rows)[0].value;
}

int main() {
	const Cell cells[2] = {{4}, {5}};
	const Cell (*rows)[2] = &cells;
	return read_cells(rows);
}
