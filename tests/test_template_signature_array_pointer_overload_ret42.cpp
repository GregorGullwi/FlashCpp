// An array object is not a pointer, so `int (*)[3]` and `int*` parameter types
// are distinct overloads. Dereferencing the row pointer returns the array row,
// which keeps the runtime answer dependent on the selected overload.
static_assert(!__is_same(int (*)[3], int*), "pointer to array is not a pointer");
static_assert(!__is_same(int[3], int*), "an array is not a pointer");

template <class T>
int choose(T tag, int (*row)[3]) {
	return 11;
}

template <class T>
int choose(T tag, int* value) {
	return 22;
}

int rows[3] = {1, 2, 3};

int main() {
	int value = 7;
	if (choose(0, &rows) != 11) {
		return 1;
	}
	if (choose(0, &value) != 22) {
		return 2;
	}
	return 42;
}
