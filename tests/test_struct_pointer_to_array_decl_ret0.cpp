struct Cell {
	int tag;
	long long value;
};

int main() {
	Cell cells[3] = {{1, 101LL}, {2, 202LL}, {3, 303LL}};
	Cell (*pointer)[3] = &cells;
	if (sizeof(pointer) != sizeof(void*)) return 1;
	if ((*pointer)[1].tag != 2 || (*pointer)[1].value != 202LL) return 2;
	(*pointer)[2].value = 404LL;
	return cells[2].value == 404LL ? 0 : 3;
}
