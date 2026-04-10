int pick(int* p) {
	return *p - 42;
}

int pick(int value) {
	return value;
}

int main() {
	int value = 42;
	return pick(&value);
}
