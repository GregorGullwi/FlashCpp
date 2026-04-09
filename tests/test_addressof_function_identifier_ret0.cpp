int add_one(int value) {
	return value + 1;
}

int main() {
	int (*fn)(int) = &add_one;
	return fn(41) == 42 ? 0 : 1;
}
