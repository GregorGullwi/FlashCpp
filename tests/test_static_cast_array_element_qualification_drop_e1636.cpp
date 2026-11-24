// [conv.qual]/1 takes an array's cv-qualifiers from its element type, so the
// element const is a cv level of the qualification decomposition. Casting
// "const int (*)[4]" to "int (*)[4]" therefore removes const at the array
// element, which [conv.qual]/3 forbids and [expr.static.cast]/5 rejects.

int main() {
	const int row[4] = {1, 2, 3, 4};
	const int (*source)[4] = &row;
	int (*target)[4] = static_cast<int (*)[4]>(source);
	(void)target;
	return 0;
}
