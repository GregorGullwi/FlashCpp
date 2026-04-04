int plus1(int x) {
	return x + 1;
}

int (*get())(int) {
	return plus1;
}

int main() {
	int (*fp)(int) = get();
	return fp(41) == 42 ? 0 : 1;
}
