using FunctionPointer = int (*)(int);

int increment(int value) {
	return value + 1;
}

int invoke(FunctionPointer pointer, int value) {
	return pointer(value);
}

int main() {
	FunctionPointer pointer = &increment;
	FunctionPointer adopted = *pointer;
	if (adopted(41) != 42) {
		return 1;
	}
	FunctionPointer address = &*pointer;
	if (address(5) != 6) {
		return 2;
	}
	return invoke(*pointer, 9) == 10 ? 0 : 3;
}
