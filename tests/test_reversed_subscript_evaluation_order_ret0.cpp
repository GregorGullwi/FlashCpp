// C++20 [expr.sub] evaluates the written left operand before the right operand,
// including the reversed built-in form index[array].

int values[5] = {11, 22, 33, 44, 55};
int matrix[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
int sequence = 0;

short indexFirst() {
	sequence = sequence * 10 + 1;
	return 2;
}

int* pointerSecond() {
	sequence = sequence * 10 + 2;
	return values;
}

int* mutateIndexAndReturnPointer(int& index) {
	index = 4;
	return values;
}

struct ArrayView {
	int* data;

	operator int*() const {
		sequence = sequence * 10 + 3;
		return data;
	}
};

ArrayView& viewOperand(ArrayView& view) {
	sequence = sequence * 10 + 2;
	return view;
}

unsigned short mutatePointerAndReturnIndex(int*& pointer) {
	pointer = values + 1;
	return 0;
}

using RowPointer = int (*)[4];

int (*matrixPointer())[4] {
	sequence = sequence * 10 + 2;
	return matrix;
}

int column() {
	sequence = sequence * 10 + 4;
	return 2;
}

int row() {
	sequence = sequence * 10 + 5;
	return 1;
}

int (*loggedMatrix())[4] {
	sequence = sequence * 10 + 6;
	return matrix;
}

int main() {
	sequence = 0;
	if (indexFirst()[pointerSecond()] != 33 || sequence != 12) {
		return 1;
	}

	// Save the local index value before the pointer expression mutates it.
	int changing_index = 1;
	if (changing_index[mutateIndexAndReturnPointer(changing_index)] != 22 || changing_index != 4) {
		return 2;
	}

	changing_index = 1;
	changing_index[mutateIndexAndReturnPointer(changing_index)] += 2;
	if (values[1] != 24 || values[4] != 55 || changing_index != 4) {
		return 6;
	}

	ArrayView view;
	view.data = values;
	sequence = 0;
	if (indexFirst()[viewOperand(view)] != 33 || sequence != 123) {
		return 3;
	}
	sequence = 0;
	if (viewOperand(view)[indexFirst()] != 33 || sequence != 231) {
		return 9;
	}

	// Take the address through a reversed subscript and a nested row subscript.
	sequence = 0;
	int* element = &indexFirst()[matrixPointer()][2];
	if (element != &matrix[2][2] || sequence != 12) {
		return 4;
	}

	// The outer column index is evaluated before the nested row subscript.
	sequence = 0;
	int* nested_element = &column()[row()[loggedMatrix()]];
	if (nested_element != &matrix[1][2] || sequence != 456) {
		return 5;
	}

	sequence = 0;
	int* flattened_element = &column()[row()[matrix]];
	if (flattened_element != &matrix[1][2] || sequence != 45) {
		return 7;
	}

	int* pointer = values;
	if (pointer[mutatePointerAndReturnIndex(pointer)] != 11 || pointer != values + 1) {
		return 8;
	}

	using Cube = int[2][3][4];
	Cube cube{};
	cube[1][2][3] = 77;
	Cube* cube_pointer = &cube;
	RowPointer partial = &2[(*cube_pointer)[1]];
	if (partial != &cube[1][2] || (*partial)[3] != 77) {
		return 10;
	}
	return 0;
}
