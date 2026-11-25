// An array-reference parameter binds the array object, not the pointer object
// that stores its address. A pointer whose pointee is that array supplies the
// array address as its value, so a call argument must pass the loaded pointer
// value just like local reference initialization does.

struct Row3 {
	int (*row)[3];
};

struct WideRow2 {
	long long (*row)[2];
};

void bump(int (&values)[3]) {
	values[0] += 1;
	values[1] += 2;
	values[2] += 3;
}

int readMiddle(const int (&values)[3]) {
	return values[1];
}

int readPointer(int* value) {
	return *value;
}

void widen(long long (&values)[2]) {
	values[0] += 10;
	values[1] += 20;
}

int main() {
	int storage[3] = {10, 20, 30};
	int (*pointer)[3] = &storage;
	bump(pointer);
	if (storage[0] != 11 || storage[1] != 22 || storage[2] != 33) {
		return 1;
	}
	if (readMiddle(pointer) != 22) {
		return 2;
	}

	const int frozen[3] = {4, 5, 6};
	const int (*const_pointer)[3] = &frozen;
	if (readMiddle(const_pointer) != 5) {
		return 3;
	}

	long long wide[2] = {100, 200};
	long long (*wide_pointer)[2] = &wide;
	widen(wide_pointer);
	if (wide[0] != 110 || wide[1] != 220) {
		return 4;
	}

	Row3 object{&storage};
	bump(object.*(&Row3::row));
	if (storage[0] != 12 || storage[1] != 24 || storage[2] != 36) {
		return 5;
	}
	if (readMiddle(object.*(&Row3::row)) != 24) {
		return 6;
	}

	Row3* object_pointer = &object;
	bump(object_pointer->*(&Row3::row));
	if (storage[0] != 13 || storage[1] != 26 || storage[2] != 39) {
		return 7;
	}

	WideRow2 wide_object{&wide};
	widen(wide_object.*(&WideRow2::row));
	if (wide[0] != 120 || wide[1] != 240) {
		return 8;
	}

	// A plain pointer argument to a pointer parameter still passes its value.
	int* scalar_pointer = storage;
	if (*scalar_pointer != 13) {
		return 9;
	}
	if (readPointer(scalar_pointer) != 13) {
		return 10;
	}

	// Local reference-to-array initialization keeps binding the array object.
	int (&local_reference)[3] = storage;
	local_reference[1] = 27;
	if (storage[1] != 27) {
		return 11;
	}

	// Direct access through the pointer-to-array member keeps working.
	if ((*object.row)[1] != 27) {
		return 12;
	}
	return 0;
}
