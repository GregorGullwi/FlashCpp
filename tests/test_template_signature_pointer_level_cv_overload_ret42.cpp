// Two function templates whose parameter types differ only in pointer-level cv
// are distinct overloads. The signature-shape compare must keep them apart, and
// its answer must agree with the canonical identity trait: `int* const*` and
// `int**` are different types.
static_assert(!__is_same(int* const*, int**), "per-level pointer cv is part of the type");
static_assert(!__is_same(int* const* volatile*, int**), "every pointer level's cv counts");

template <class T>
int choose(T tag, int* const* value) {
	return 11;
}

template <class T>
int choose(T tag, int** value) {
	return 22;
}

int main() {
	int value = 7;
	int* inner = &value;
	int* const* qualified = &inner;
	int** plain = &inner;
	if (choose(0, qualified) != 11) {
		return 1;
	}
	if (choose(0, plain) != 22) {
		return 2;
	}
	return 42;
}
