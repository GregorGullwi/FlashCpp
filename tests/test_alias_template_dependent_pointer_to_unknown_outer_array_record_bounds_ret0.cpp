struct Cell {
	int value;
};

template <class T, int Columns>
using IncompleteRows = T(*)[][Columns];

static_assert(__is_pointer(IncompleteRows<Cell, 3>));

int main() {
	Cell values[2][3] = {
		{{11}, {17}, {23}},
		{{29}, {31}, {37}}};
	IncompleteRows<Cell, 3> pointer = &values;
	if (sizeof(pointer) != sizeof(void*)) return 1;
	if (sizeof((*pointer)[0]) != sizeof(values[0])) return 2;
	if ((*pointer)[1][2].value != 37) return 3;
	(*pointer)[0][1].value = 42;
	return values[0][1].value == 42 ? 0 : 4;
}
