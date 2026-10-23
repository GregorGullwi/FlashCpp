struct Cell {
	long long value;
};

template<class T>
int template_row(T (*value)[2]) {
	static_assert(__is_same(T, int), "T is the element type");
	return (*value)[0] + (*value)[1];
}

template<class T>
int template_cube(T (*value)[2][3]) {
	static_assert(__is_same(T, int), "T is the element type");
	return (*value)[0][0] + (*value)[1][2];
}

template<class T>
long long template_cell_row(T (*value)[2]) {
	static_assert(__is_same(T, Cell), "T is the record element type");
	return value[0][0].value + value[0][1].value;
}

int plain_row(int (*value)[2]) {
	return (*value)[0] + (*value)[1];
}

int main() {
	int rows[2][2] = {{1, 2}, {3, 4}};
	int cubes[4][2][3] = {{{1, 2, 3}, {4, 5, 6}}, {{7, 8, 9}, {10, 11, 12}},
		{{13, 14, 15}, {16, 17, 18}}, {{19, 20, 21}, {22, 23, 24}}};
	Cell cells[2][2] = {{10, 20}, {30, 40}};
	if (plain_row(rows) != 3) return 1;
	if (template_row(rows) != 3) return 2;
	if (template_cube(cubes) != 7) return 3;
	if (template_cell_row(cells) != 30) return 4;
	return 0;
}
