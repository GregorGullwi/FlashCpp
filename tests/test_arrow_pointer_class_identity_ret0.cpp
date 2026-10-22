// C++20 [expr.ref]: E1->E2 is (*(E1)).E2. Builtin arrow peels one pointer.
// An array object decays and one indirection removes one bound, so Cell[2]
// selects the first Cell. Cv on the pointer object does not qualify the
// result; cv on an array stays on the element. A pointer to a row still
// addresses that row, so the second row is not the second Cell. An
// overloaded operator-> still wins over the builtin peel.
struct Cell {
	int value;
	char tag;

	int get() { return value + 2; }
	int get() const { return value + 1; }
};

struct Data {
	int value;
};

struct Box {
	int value;
	Data* data;
	Data* operator->() { return data; }
};

struct Wide {
	long value;
};

struct Narrow {
	short value;
};

template <class T>
struct Holder {
	T value;
	char tag;
};

template <class T>
struct Pair;

template <class T>
struct Pair<T> {
	T value{};
};

template <class T, class U>
struct Pair<T, U> {
	T head{};

	int get() { return 2; }
	int get() const { return 1; }
	int via_arrow() const { return this->get(); }
	int via_arrow_mut() { return this->get(); }
};

int main() {
	Cell cell{21, 'q'};
	const Cell* const_pointee = &cell;
	if (const_pointee->value != 21 || const_pointee->get() != 22) {
		return 1;
	}
	Cell* const const_pointer = &cell;
	if (const_pointer->value != 21 || const_pointer->get() != 23) {
		return 2;
	}

	Cell cells[2] = {{11, 'a'}, {22, 'b'}};
	if (cells->value != 11 || cells->tag != 'a') {
		return 3;
	}
	Cell* cell_pointer = &cells[1];
	if (cell_pointer->value != 22 || cell_pointer->tag != 'b') {
		return 4;
	}
	const Cell const_cells[2] = {{5, 'q'}, {6, 'r'}};
	if (const_cells->value != 5 || const_cells->get() != 6 || const_cells->tag != 'q') {
		return 5;
	}

	Cell grid[2][3] = {{{1, 'a'}, {2, 'b'}, {3, 'c'}}, {{4, 'd'}, {5, 'e'}, {6, 'f'}}};
	Cell* first = &grid[0][0];
	Cell* second_row = &grid[1][0];
	if (first->value != 1 || first->tag != 'a' || second_row->value != 4 || second_row->tag != 'd') {
		return 6;
	}
	Cell (*row_pointer)[3] = &grid[0];
	if ((*row_pointer)[2].value != 3 || row_pointer[1][0].value != 4) {
		return 7;
	}

	Wide wides[2] = {{100L}, {200L}};
	if (wides->value != 100L) {
		return 8;
	}
	Wide* wide_pointer = &wides[1];
	if (wide_pointer->value != 200L) {
		return 9;
	}
	Narrow narrows[2] = {{7}, {8}};
	if (narrows->value != 7) {
		return 10;
	}
	Narrow* narrow_pointer = &narrows[1];
	if (narrow_pointer->value != 8) {
		return 11;
	}

	Holder<long> held[2] = {{9L, 'x'}, {8L, 'y'}};
	if (held->value != 9L || held->tag != 'x') {
		return 12;
	}
	Holder<long>* held_pointer = &held[1];
	if (held_pointer->value != 8L || held_pointer->tag != 'y') {
		return 13;
	}
	Holder<short> narrow_held{4, 'n'};
	const Holder<short>* const_held = &narrow_held;
	if (const_held->value != 4 || const_held->tag != 'n') {
		return 14;
	}

	Pair<int, char> pair;
	const Pair<int, char> const_pair;
	if (pair.via_arrow_mut() != 2 || const_pair.via_arrow() != 1) {
		return 15;
	}

	Data data{7};
	Box box{1, &data};
	if (box->value != 7) {
		return 16;
	}
	return 0;
}
