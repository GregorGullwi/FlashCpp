// Phase 21 regression: user-defined conversion operator (operator int / operator double)
// in both the return-statement and variable-initialisation contexts, using the
// sema-owned UserDefined annotation path.

struct BoxedInt {
	int value;
	BoxedInt(int v) : value(v) {}
	operator int() const { return value; }
	operator double() const { return static_cast<double>(value); }
};

int get_int() {
	BoxedInt b(21);
	return b;  // return path: sema annotates UserDefined, codegen calls operator int()
}

int main() {
	BoxedInt b(21);

	// variable-init paths
	int i = b;          // operator int()
	double d = b;       // operator double()

	int from_return = get_int();

	if (i != 21) return 1;
	if (static_cast<int>(d) != 21) return 2;
	if (from_return != 21) return 3;

	return i + from_return;  // 42
}
