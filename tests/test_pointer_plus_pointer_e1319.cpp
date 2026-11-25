// C++20 [expr.add]/1 defines pointer plus integer and pointer minus pointer.
// Adding two pointers has no built-in candidate, so the compiler reports the
// existing non-viable built-in operator diagnostic instead of treating the
// right pointer as an integer offset.

int main() {
	int values[2] = {1, 2};
	int* first = values;
	int* second = values + 1;
	int* invalid = first + second;
	return invalid == values ? 0 : 1;
}
