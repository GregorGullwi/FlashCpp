// Regression: a store through a pointer reference (or a dereferenced pointer)
// must use the full destination pointer width. A 32-bit store would leave the
// upper half of the pointer unchanged on the 64-bit target, so the pointer would
// not compare equal to nullptr.
int global_value = 5;
int* global_pointer = &global_value;

void clear_reference(int*& pointer) { pointer = nullptr; }
void clear_through(int** pointer) { *pointer = nullptr; }
void copy_reference(int*& destination, int* source) { destination = source; }

int main() {
	int first = 1;
	int second = 2;

	int* pointer = &first;
	clear_reference(pointer);
	if (pointer != nullptr) return 1;

	pointer = &first;
	clear_through(&pointer);
	if (pointer != nullptr) return 2;

	int* source = &second;
	pointer = &first;
	copy_reference(pointer, source);
	if (pointer != source) return 3;

	pointer = &first;
	copy_reference(pointer, global_pointer);
	if (pointer != global_pointer) return 4;

	return 0;
}
