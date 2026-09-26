int main() {
	if (!__is_same(int (*)[3], int (*)[3])) {
		return 1;
	}
	if (__is_same(int (*)[3], int*[3])) {
		return 2;
	}
	return 42;
}
