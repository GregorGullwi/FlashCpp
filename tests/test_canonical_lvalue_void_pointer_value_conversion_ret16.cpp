// Reduced regression for canonical planning of lvalue-marked void pointer
// value conversions: a top-level const pointer object and a const void pointee
// must still match exactly, adding const to the pointee must rank as a
// qualification adjustment, and removing const from the pointee must fail and
// fall back to the bool overload.

static int acceptVoidPointer(void* pointer) {
	return pointer != nullptr ? 1 : 0;
}

static int acceptConstVoidPointer(const void* pointer) {
	return pointer != nullptr ? 2 : 0;
}

static int rejectConstVoidRemoval(void*) {
	return 4;
}

static int rejectConstVoidRemoval(bool) {
	return 8;
}

int main() {
	int value = 0;
	int* int_pointer = &value;
	void* void_pointer = int_pointer;
	void* const const_void_pointer = int_pointer;
	const void* const_void_pointer_value = int_pointer;
	const void* const const_const_void_pointer = int_pointer;
	return acceptVoidPointer(void_pointer) + acceptVoidPointer(const_void_pointer) +
		acceptConstVoidPointer(void_pointer) + acceptConstVoidPointer(const_void_pointer_value) +
		acceptConstVoidPointer(const_const_void_pointer) + rejectConstVoidRemoval(const_void_pointer_value);
}
