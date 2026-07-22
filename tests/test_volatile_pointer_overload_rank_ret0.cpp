// Qualification conversion ranking must consider volatile as well as const.
// The identity conversion to int* is better than adding volatile to the pointee.

int classify(volatile int*) {
	return 1;
}

int classify(int*) {
	return 0;
}

int main() {
	int value = 42;
	return classify(&value);
}
