struct Inner {
	int value;
};

struct Outer {
	Inner inner;
};

int eval_count = 0;

bool choose_right() {
	++eval_count;
	return false;
}

int main() {
	Outer left;
	Outer right;
	left.inner.value = 10;
	right.inner.value = 42;

	int* ptr = &((choose_right() ? left : right).inner.value);
	if (eval_count != 1)
		return 1;
	if (*ptr != 42)
		return 2;
	*ptr = 99;
	if (right.inner.value != 99)
		return 3;
	if (left.inner.value != 10)
		return 4;
	return 0;
}
