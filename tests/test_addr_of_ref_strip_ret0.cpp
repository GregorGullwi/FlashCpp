int read(int* p) {
	return *p;
}

int main() {
	int value = 42;
	int& ref = value;
	return read(&ref) - 42;
}
