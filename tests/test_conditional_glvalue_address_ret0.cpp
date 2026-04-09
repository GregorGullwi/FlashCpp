struct Inner {
	int value;
};

struct Outer {
	Inner inner;
};

int main() {
	Outer left;
	Outer right;
	left.inner.value = 10;
	right.inner.value = 42;

	bool pick_left = false;
	int* ptr = &((pick_left ? left : right).inner.value);
	if (*ptr != 42)
		return 1;
	*ptr = 99;
	if (right.inner.value != 99)
		return 2;
	if (left.inner.value != 10)
		return 3;
	return 0;
}
