// A namespace-scope two-dimensional array of pointers has one scalar pointer
// element per brace leaf, not one pointer object per top-level row clause.
// Reading through a pointer to the whole array must see each stored pointer and
// its pointee, and a store through one element must stay on the element stride
// instead of overwriting a neighbouring row.
int p00 = 1;
int p01 = 2;
int p02 = 3;
int p10 = 4;
int p11 = 5;
int p12 = 6;

int* cells[2][3] = {{&p00, &p01, &p02}, {&p10, &p11, &p12}};

int main() {
	int* (*p)[2][3] = &cells;
	if (p != &cells) return 1;
	if ((*p)[0][0] != &p00 || (*p)[0][1] != &p01 || (*p)[0][2] != &p02) return 2;
	if ((*p)[1][0] != &p10 || (*p)[1][1] != &p11 || (*p)[1][2] != &p12) return 3;
	if (*(*p)[0][0] != 1 || *(*p)[0][2] != 3) return 4;
	if (*(*p)[1][0] != 4 || *(*p)[1][2] != 6) return 5;
	if (sizeof(*p) != 6 * sizeof(int*)) return 6;

	*(*p)[1][0] = 60;
	if (p10 != 60) return 7;
	if (*(*p)[1][0] != 60) return 8;
	// The neighbouring pointer element keeps its own pointee and value.
	if ((*p)[1][1] != &p11 || *(*p)[1][1] != 5) return 9;
	if (p11 != 5) return 10;

	return 0;
}
