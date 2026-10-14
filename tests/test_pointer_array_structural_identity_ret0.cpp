// Structural identity for projectable pointer and array types must stay
// consistent with the flat pointer/array projection when that projection is
// consumed: conditional array decay, pointer-to-array, subscript, address-of,
// dereference, and member access through a pointer.
struct Payload {
	int value;
};

static int read_at(const int* base, int index) {
	return base[index];
}

int main() {
	int values[3] = {10, 20, 30};
	int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};

	// Array-to-pointer decay in a conditional with another pointer operand.
	const int* chosen = values[0] == 10 ? values : &values[1];
	if (chosen[1] != 20) {
		return 1;
	}

	// Pointer-to-array keeps its extent; it is not the element pointer.
	int (*row)[3] = grid;
	if ((*row)[2] != 3 || row[1][2] != 6) {
		return 2;
	}

	// Address of an array object yields pointer-to-array, not pointer-to-element.
	int (*whole)[3] = &values;
	if ((*whole)[1] != 20) {
		return 3;
	}

	// Subscript and dereference through a decayed pointer.
	if (read_at(values, 2) != 30) {
		return 4;
	}
	if (*(values + 1) != 20) {
		return 5;
	}

	// Conditional across an array and a local array decays both operands.
	int local[2] = {7, 8};
	int* pick = values[0] ? values : local;
	if (pick[0] != 10) {
		return 6;
	}

	// Member access through a pointer keeps the pointee's member type.
	Payload item{42};
	Payload* item_pointer = &item;
	if (item_pointer->value != 42) {
		return 7;
	}

	static_assert(!__is_array(decltype(row)));
	static_assert(__is_same(decltype(chosen), const int*));
	static_assert(__is_same(decltype(row), int (*)[3]));

	return 0;
}
