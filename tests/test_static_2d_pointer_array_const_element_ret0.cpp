// A const-qualified pointee does not change the pointer element width, so a
// two-dimensional array of const int pointers must initialize and index each
// pointer at the pointer stride.
const int c00 = 11;
const int c01 = 22;
const int c02 = 33;
const int c10 = 44;
const int c11 = 55;
const int c12 = 66;

const int* cells[2][3] = {{&c00, &c01, &c02}, {&c10, &c11, &c12}};

int main() {
	const int* (*p)[2][3] = &cells;
	if ((*p)[0][0] != &c00 || (*p)[0][1] != &c01 || (*p)[0][2] != &c02) return 1;
	if ((*p)[1][0] != &c10 || (*p)[1][1] != &c11 || (*p)[1][2] != &c12) return 2;
	if (*(*p)[0][1] != 22 || *(*p)[0][2] != 33) return 3;
	if (*(*p)[1][0] != 44 || *(*p)[1][2] != 66) return 4;

	// The pointer element is 8 bytes, so a pointee-sized stride would land on
	// the neighbouring pointer instead of the next stored address.
	if ((*p)[1][1] != &c11 || *(*p)[1][1] != 55) return 5;
	if (sizeof((*p)[0]) != 3 * sizeof(const int*)) return 6;
	if (sizeof(*p) != 6 * sizeof(const int*)) return 7;

	return 0;
}
