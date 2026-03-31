// Test: initialize T (value) from T&& function return
// Exercises the data-copy path when source has ContainsAddress storage

int&& forward_rref(int&& x) {
	return static_cast<int&&>(x);
}

struct Pair {
	int a, b;
};

static Pair g_pair{30, 40};

Pair&& get_pair_rref() {
	return static_cast<Pair&&>(g_pair);
}

int main() {
	int base = 77;

	// Copy int from T&& return (not reference binding — this is a value copy)
	int val = forward_rref(static_cast<int&&>(base));
	if (val != 77)
		return 1;

	// Modifying base does NOT affect val (it's a true copy)
	base = 99;
	if (val != 77)
		return 2;

	// Copy Pair from T&& return
	Pair copy = get_pair_rref();
	if (copy.a != 30)
		return 3;
	if (copy.b != 40)
		return 4;

	// Modifying copy does NOT affect g_pair
	copy.a = 55;
	if (g_pair.a != 30)
		return 5;

	return 0;
}
