// Expected to fail: compound assignment reads the indeterminate local value before writing it.
constexpr int bumpLocalScalar() {
	int value;
	value += 1;
	return value;
}

static_assert(bumpLocalScalar() == 1);

int main() {
	return 0;
}
