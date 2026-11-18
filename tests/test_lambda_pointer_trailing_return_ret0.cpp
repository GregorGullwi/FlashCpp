// A lambda or function trailing return type must consume pointer, reference,
// and grouped pointer-to-array abstract declarators before looking for the body.

static int value = 7;
static int rows[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};

auto get_rows() -> int (*)[4] {
	return rows;
}

int main() {
	auto get_pointer = []() -> int* { return &value; };
	if (*get_pointer() != 7) {
		return 1;
	}
	auto get_reference = []() -> int& { return value; };
	int& reference = get_reference();
	if (reference != 7) {
		return 2;
	}
	auto get_array_pointer = []() -> int (*)[4] { return rows; };
	if (get_array_pointer()[1][0] != 5) {
		return 3;
	}
	if (get_rows()[1][3] != 8) {
		return 4;
	}
	return 0;
}
