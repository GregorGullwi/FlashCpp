// [expr.ref]/2: a member whose own type is const below its top level stays
// assignable at the top. The const belongs to the pointed-to int, not to the
// non-projectable pointer-to-array wrapper, so neither member access nor auto&
// deduction may hoist it onto the wrapper. Every write is read back through a
// different path.
struct Store {
	const int* (*rows)[3];
};

const int first = 101;
const int second = 202;
const int third = 303;
const int other_first = 404;
const int other_second = 505;
const int other_third = 606;

const int* first_row[3] = {&first, &second, &third};
const int* second_row[3] = {&other_first, &other_second, &other_third};

int main() {
	Store store{&first_row};

	auto& rows = store.rows;
	if (rows != &first_row) return 1;
	if (*(*rows)[0] != 101) return 2;
	if (*(*rows)[2] != 303) return 3;

	rows = &second_row;
	if (*(*store.rows)[1] != 505) return 4;
	if (*(*store.rows)[2] != 606) return 5;

	store.rows = &first_row;
	if (*(*store.rows)[2] != 303) return 6;
	if (store.rows != &first_row) return 7;

	return 0;
}
