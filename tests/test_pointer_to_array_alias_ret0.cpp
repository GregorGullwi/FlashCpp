// [dcl.ptr], [dcl.typedef], [dcl.fct]: a ptr-operator on an array alias points
// at that array. A function may return the pointer. Postfix brackets after the
// pointer declare an array of those pointers. `int *a[N]` stays that array.

using Row = int[2];
using Grid = int[2][3];
using RowPointer = Row*;

struct Holder {
	Row* method(Row* source) {
		return source;
	}
	Row* none() {
		return nullptr;
	}
};

using Method = Row* (Holder::*)();

int main() {
	Row* pointer = nullptr;
	if (sizeof(pointer) != sizeof(void*) || sizeof(*pointer) != 2 * sizeof(int)) {
		return 1;
	}
	RowPointer alias = pointer;
	if (sizeof(alias) != sizeof(pointer) || sizeof(*alias) != sizeof(*pointer)) {
		return 2;
	}
	Row** indirect = &pointer;
	if (sizeof(indirect) != sizeof(void*) || sizeof(**indirect) != 2 * sizeof(int)) {
		return 3;
	}
	Grid* grid = nullptr;
	if (sizeof(grid) != sizeof(void*) || sizeof(*grid) != 6 * sizeof(int)) {
		return 4;
	}

	Row* row_pointers[2] = {};
	if (sizeof(row_pointers) != 2 * sizeof(void*)) {
		return 5;
	}
	if (sizeof(row_pointers[0]) != sizeof(void*) || sizeof(*row_pointers[0]) != 2 * sizeof(int)) {
		return 10;
	}
	Grid* pages[2] = {};
	if (sizeof(pages) != 2 * sizeof(void*) || sizeof(*pages[0]) != 6 * sizeof(int)) {
		return 11;
	}
	int* scalar_pointers[2] = {};
	if (sizeof(scalar_pointers) != 2 * sizeof(void*)) {
		return 6;
	}

	int local[2] = {4, 5};
	Row* (Holder::*member)(Row*) = &Holder::method;
	if (sizeof(member) != sizeof(void*)) {
		return 7;
	}
	Holder object;
	Row* result = (object.*member)(&local);
	if (result != &local || sizeof(*result) != 2 * sizeof(int)) {
		return 8;
	}
	Method none = &Holder::none;
	if (sizeof(none) != sizeof(member) || (object.*none)() != nullptr) {
		return 9;
	}
	return 0;
}
