template <typename T>
int f(T x) {
	(void)x;
	return 7;
}

template <typename T>
int f(T* x) {
	(void)x;
	return 42;
}

int main() {
	int i = 0;
	if (f(&i) != 42) return 1;
	return 0;
}
