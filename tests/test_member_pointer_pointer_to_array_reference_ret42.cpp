// A data-member pointer's pointee keeps the member's complete type. A
// pointer-to-array member therefore publishes a pointer value, so binding an
// array reference to the access must read the pointer member's value instead of
// the address where that pointer is stored.

struct Row3 {
	int (*row)[3];
};

struct LongRow2 {
	long long (*row)[2];
};

struct PlainPointer {
	int* value;
};

struct PlainInt {
	int value;
};

int main() {
	int storage[3] = {10, 20, 30};
	Row3 object{&storage};
	int (&row)[3] = object.*(&Row3::row);
	row[0] = 11;
	row[2] = 33;
	if (storage[0] != 11 || storage[2] != 33) {
		return 1;
	}
	if (row[1] != 20) {
		return 2;
	}

	const Row3 const_object{&storage};
	const int (&const_row)[3] = const_object.*(&Row3::row);
	if (const_row[1] != 20 || const_row[2] != 33) {
		return 3;
	}

	long long wide[2] = {100, 200};
	LongRow2 wide_object{&wide};
	long long (&wide_row)[2] = wide_object.*(&LongRow2::row);
	wide_row[1] = 250;
	if (wide[1] != 250) {
		return 4;
	}
	if (wide_row[0] != 100) {
		return 5;
	}

	int scalar = 9;
	PlainPointer pointer_object{&scalar};
	int*& pointer_reference = pointer_object.*(&PlainPointer::value);
	if (*pointer_reference != 9) {
		return 6;
	}
	*pointer_reference = 10;
	if (scalar != 10) {
		return 7;
	}

	PlainInt value_object{5};
	int PlainInt::*member = &PlainInt::value;
	value_object.*member = 12;
	if (value_object.value != 12) {
		return 8;
	}

	// The pointer-to-array member also stays usable for direct array access.
	if ((*object.row)[1] != 20) {
		return 9;
	}
	return 42;
}
