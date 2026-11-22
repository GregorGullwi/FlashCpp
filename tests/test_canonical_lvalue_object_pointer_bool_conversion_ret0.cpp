// An lvalue-marked object pointer converts to bool through the canonical
// structural plan, including multi-level, cv-qualified, and cv void pointees.
static int pointerBool(bool value) {
	return value ? 4 : 0;
}

int main() {
	int value = 0;
	int* pointer = &value;
	int** pointer_pointer = &pointer;
	const int* const_pointer = &value;
	void* void_pointer = &value;
	int result = 0;
	result += pointerBool(pointer) != 4;
	result += pointerBool(pointer_pointer) != 4;
	result += pointerBool(const_pointer) != 4;
	result += pointerBool(void_pointer) != 4;
	return result;
}
