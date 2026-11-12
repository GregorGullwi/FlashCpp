int guarded(int* pointer) {
	return !pointer || *pointer == 42;
}

int main() {
	int value = 42;
	if (!guarded(nullptr)) return 1;
	if (!guarded(&value)) return 2;
	return 0;
}
