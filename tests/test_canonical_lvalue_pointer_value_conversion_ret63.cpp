// Reduced regression for canonical planning of an lvalue-marked builtin pointer
// source: the value-category marker on these pointer variables must not send the
// by-value pointer conversions to the syntax compatibility tail. Exact matches,
// qualification adjustment, pointer depth rejection, and const-removal rejection
// must all keep their existing results.

static int acceptCharPointer(char*) {
	return 1;
}

static int acceptCharPointer(const char*) {
	return 2;
}

static int rankPointer(int*) {
	return 4;
}

static int rankPointer(const int*) {
	return 16;
}

static int rejectConstRemoval(int*) {
	return 32;
}

static int rejectConstRemoval(bool) {
	return 8;
}

static int acceptPointerDepth(int**) {
	return 16;
}

static int rejectPointerDepth(int*) {
	return 64;
}

static int rejectPointerDepth(bool) {
	return 32;
}

int main() {
	char text[4] = {0, 0, 0, 0};
	char* text_pointer = text;
	const char* const const_text_pointer = text_pointer;
	int value = 0;
	int* int_pointer = &value;
	int** int_pointer_pointer = &int_pointer;
	const int* const_int_pointer = int_pointer;
	return acceptCharPointer(text_pointer) + acceptCharPointer(const_text_pointer) + rankPointer(int_pointer) +
		rejectConstRemoval(const_int_pointer) + acceptPointerDepth(int_pointer_pointer) +
		rejectPointerDepth(int_pointer_pointer);
}
