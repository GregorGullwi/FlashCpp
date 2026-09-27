using FunctionPointer = int (*)(int);

int target(int value) {
	return value + 1;
}

int choose(FunctionPointer&) {
	return 1;
}

int choose(FunctionPointer const&) {
	return 2;
}

int acceptRvalue(FunctionPointer&&) {
	return 3;
}

int main() {
	FunctionPointer pointer = &target;
	if (choose(*pointer) != 2)
		return 1;
	if (acceptRvalue(*pointer) != 3)
		return 2;
	return 0;
}
