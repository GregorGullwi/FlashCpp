// C++20 [conv.array]/[expr.unary.op]/1: an interleaved array operand decays to
// a pointer to its first element and unary * selects that element. For
// int (*[N])[M] the element is a pointer-to-array, not the flat base int.
int storage[4] = {7, 8, 9, 10};
int other[4] = {1, 2, 3, 4};
int (*cells[2])[4] = {&storage, &other};

int main() {
	auto first = *cells;
	if ((*first)[2] != 9) return 1;
	if ((*first)[3] != 10) return 2;

	int (*second)[4] = *cells;
	if ((*second)[0] != 7) return 3;
	return 42;
}
