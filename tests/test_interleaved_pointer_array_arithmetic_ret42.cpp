// C++20 [expr.add]/[conv.array]: an interleaved array first decays to a pointer
// to its first pointer-to-array element, and pointer arithmetic scales the
// offset by that element's pointer size, not by the flat base type.
int storage[4] = {7, 8, 9, 10};
int other[4] = {1, 2, 3, 4};
int (*cells[2])[4] = {&storage, &other};

int main() {
	auto last = cells + 1;
	if ((*last)[0][0] != 1) return 1;
	if ((*last)[0][3] != 4) return 2;

	auto first = 1 + cells;
	if ((*first)[0][1] != 2) return 3;

	auto back = last - 1;
	if ((*back)[0][2] != 9) return 4;
	return 42;
}
