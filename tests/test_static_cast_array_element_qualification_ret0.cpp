// [conv.qual]/1 gives an array the cv-qualification of its element type, and
// [conv.qual]/3 allows adding cv at that component. Adding const to the element
// of a known-bound or unknown-bound array therefore stays a valid static_cast.
// A const pointer to the array is a prvalue top-level cv on the source, which
// the value conversion drops, so the requested "int(*)[4] from int(*)[4] const"
// form is valid as well. The constexpr case pins that the cast keeps its value.

constexpr int constexprQualifiedPointee() {
	int value = 7;
	int* pointer = &value;
	const int* qualified = static_cast<const int*>(pointer);
	return *qualified;
}

static_assert(constexprQualifiedPointee() == 7);

int main() {
	int row[4] = {1, 2, 3, 4};
	int (*plain)[4] = &row;
	int (*const const_pointer)[4] = plain;

	const int (*qualified)[4] = static_cast<const int (*)[4]>(plain);
	int (*const_pointer_dropped)[4] = static_cast<int (*)[4]>(const_pointer);
	int (*unknown)[] = plain;
	const int (*unknown_qualified)[] = static_cast<const int (*)[]>(plain);
	int* pointer_row[2] = {&row[0], &row[1]};
	int* const (*const_element_row)[2] = static_cast<int* const (*)[2]>(&pointer_row);

	// [expr.cast]/4: a C-style cast may perform a const_cast-like conversion, so
	// removing the element const stays accepted for that spelling.
	int (*c_style_stripped)[4] = (int (*)[4])(qualified);

	if ((*qualified)[2] != 3) {
		return 1;
	}
	if ((*const_pointer_dropped)[2] != 3) {
		return 2;
	}
	if ((*const_element_row)[1] != &row[1]) {
		return 3;
	}
	if ((*c_style_stripped)[2] != 3) {
		return 4;
	}
	(void)unknown;
	(void)unknown_qualified;
	return 0;
}
