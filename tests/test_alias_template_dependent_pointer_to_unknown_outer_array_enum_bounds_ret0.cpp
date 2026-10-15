enum class Level : int {
	low = 11,
	mid = 17,
	high = 23
};

template <class T, int Columns>
using IncompleteRows = T(*)[][Columns];

static_assert(__is_pointer(IncompleteRows<Level, 2>));

int main() {
	Level values[2][2] = {{Level::low, Level::mid}, {Level::high, Level::mid}};
	IncompleteRows<Level, 2> pointer = &values;
	if (sizeof(pointer) != sizeof(void*)) return 1;
	if (sizeof((*pointer)[0]) != sizeof(values[0])) return 2;
	if ((*pointer)[1][0] != Level::high) return 3;
	(*pointer)[0][1] = Level::high;
	return values[0][1] == Level::high ? 0 : 4;
}
