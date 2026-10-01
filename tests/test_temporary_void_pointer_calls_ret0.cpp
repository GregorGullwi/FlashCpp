// A void base category still carries a pointer value and needs a numeric slot.
#define REPEAT_2(X) X X
#define REPEAT_4(X) REPEAT_2(REPEAT_2(X))
#define REPEAT_16(X) REPEAT_4(REPEAT_4(X))
#define REPEAT_256(X) REPEAT_16(REPEAT_16(X))

void* keepPointer(int* values, int index) {
	return values + index;
}

int main() {
	int values[2] = {41, 43};
	void* pointer = keepPointer(values, 0);
	REPEAT_256(pointer = keepPointer(values, 1);)
	return pointer == &values[1] && values[0] == 41 && values[1] == 43 ? 0 : 1;
}
