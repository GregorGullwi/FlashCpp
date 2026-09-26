template <class T>
using PointerToArray = T(*)[3];

struct Cell {
	int tag;
	long long value;
};

static_assert(__is_pointer(PointerToArray<Cell>));
static_assert(!__is_array(PointerToArray<Cell>));

int main() {
	Cell cells[3] = {{1, 101LL}, {2, 202LL}, {3, 303LL}};
	PointerToArray<Cell> pointer = &cells;
	if (sizeof(pointer) != sizeof(void*)) return 1;
	if ((*pointer)[1].tag != 2 || (*pointer)[1].value != 202LL) return 2;
	(*pointer)[2].value = 404LL;
	return cells[2].value == 404LL ? 0 : 3;
}
