int fortyTwo() {
	return 42;
}

int main() {
	int (*fp)() = fortyTwo;
	return fp();
}
