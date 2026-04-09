struct Inner {
	int values[3];
};

struct Outer {
	Inner inner;
};

int main() {
	Outer left;
	Outer right;
	left.inner.values[0] = 1;
	left.inner.values[1] = 2;
	left.inner.values[2] = 3;
	right.inner.values[0] = 4;
	right.inner.values[1] = 5;
	right.inner.values[2] = 6;

	bool pick_left = false;
	int* ptr = &(pick_left ? left.inner.values[2] : right.inner.values[2]);
	if (*ptr != 6)
		return 1;
	*ptr = 77;
	if (right.inner.values[2] != 77)
		return 2;
	if (left.inner.values[2] != 3)
		return 3;
	return 0;
}
