int pick(int* p) {
	return *p - 42;
}

int pick(void* p) {
	return p ? 1 : 2;
}

int main() {
	int value = 42;
	return pick(&value);
}
