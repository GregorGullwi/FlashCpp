template <class T, int Columns>
using IncompleteRows = T(*)[][Columns];

static_assert(__is_pointer(IncompleteRows<int, 3>));

int main() {
	int values[2][3] = {{11, 17, 23}, {29, 31, 37}};
	IncompleteRows<int, 3> pointer = &values;
	if (sizeof(pointer) != sizeof(void*)) return 1;
	if (sizeof((*pointer)[0]) != sizeof(values[0])) return 2;
	if ((*pointer)[1][2] != 37) return 3;
	(*pointer)[0][1] = 42;
	return values[0][1] == 42 ? 0 : 4;
}
