// Test: function returning T&& — bind to T&& var and read/write through it
// Exercises the call-return path that registers indirect storage info

int&& pass_through_rref(int&& x) {
	return static_cast<int&&>(x);
}

struct Pair {
	int a, b;
};

static Pair g_pair{10, 20};

Pair&& get_pair_rref() {
	return static_cast<Pair&&>(g_pair);
}

int main() {
	int base = 77;

// T&& return → T&& reference variable binding
	int&& r = pass_through_rref(static_cast<int&&>(base));
	if (r != 77)
		return 1;

// Write through the rvalue reference
	r = 99;
	if (base != 99)
		return 2;

// Struct T&& return — bind reference, read member
	Pair&& rp = get_pair_rref();
	if (rp.a != 10)
		return 3;
	if (rp.b != 20)
		return 4;

// Mutate via reference
	rp.a = 55;
	if (g_pair.a != 55)
		return 5;

	return 0;
}
