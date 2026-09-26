template <class T, int Rows, int Columns>
using MatrixPointer = T(*)[Rows][Columns];

static_assert(__is_pointer(MatrixPointer<int, 2, 3>));
static_assert(!__is_array(MatrixPointer<int, 2, 3>));

int main() {
	int values[2][3] = {{11, 17, 23}, {29, 31, 37}};
	MatrixPointer<int, 2, 3> pointer = &values;
	if (sizeof(pointer) != sizeof(void*)) return 1;
	if (sizeof(*pointer) != sizeof(values)) return 2;
	if (sizeof((*pointer)[0]) != sizeof(values[0])) return 3;
	if ((*pointer)[1][2] != 37) return 4;
	(*pointer)[0][1] = 42;
	if (values[0][1] != 42) return 5;

	long long other_values[3][2] = {{101LL, 202LL}, {303LL, 404LL}, {505LL, 606LL}};
	MatrixPointer<long long, 3, 2> other_pointer = &other_values;
	if (sizeof(*other_pointer) != sizeof(other_values)) return 6;
	if (sizeof((*other_pointer)[0]) != sizeof(other_values[0])) return 7;
	if ((*other_pointer)[2][1] != 606LL) return 8;
	return 0;
}
