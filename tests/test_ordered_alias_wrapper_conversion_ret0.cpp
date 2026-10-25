// An ordered declarator over an array, function, reference, or member-pointer
// alias converts as the spelled type.

struct Owner {
	int value;
};

using Row = int[4];
using F = int(int);
using Ref = int&;
using Field = int Owner::*;
using Inner = int(double);
using Outer = Inner*(int);

int object = 7;

int id(int value) {
	return value + 1;
}

Ref read() {
	return object;
}

int rank_row(int (*parameter)[2][4]) {
	(void)parameter;
	return 1;
}

int rank_row(long parameter) {
	(void)parameter;
	return 2;
}

int take_function(int (*parameter)(int)) {
	return parameter(41);
}

int take_reader(int& (*parameter)()) {
	return parameter();
}

int take_field(int (Owner::* (*parameter)[2])) {
	return parameter == nullptr ? 4 : 5;
}

int main() {
	Row (*grid)[2] = nullptr;
	Row (*grid_copy)[2] = grid;
	if (rank_row(grid_copy) != 1) {
		return 1;
	}
	F (*pointer) = id;
	F (*pointer_copy) = pointer;
	if (take_function(pointer_copy) != 42) {
		return 2;
	}
	Ref (*reader)() = read;
	Ref (*reader_copy)() = reader;
	if (take_reader(reader_copy) != 7) {
		return 3;
	}
	Field (*slots)[2] = nullptr;
	int (Owner::* (*spelled_slots)[2]) = slots;
	if (take_field(spelled_slots) != 4) {
		return 4;
	}
	Outer* (*page)[3] = nullptr;
	int (*(*(*spelled_page)[3])(int))(double) = page;
	if (sizeof(*spelled_page) != 3 * sizeof(void*)) {
		return 5;
	}
	return 0;
}
