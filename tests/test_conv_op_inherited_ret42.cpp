// Phase 21 regression: inherited conversion operator in return and variable-init contexts.

struct Base {
	int value;
	Base(int v) : value(v) {}
	operator int() const { return value; }
};

struct Derived : Base {
	Derived(int v) : Base(v) {}
 // inherits operator int() from Base
};

int get_value() {
	Derived d(21);
	return d;  // inherited operator int() — return path
}

int main() {
	Derived d(21);

	int i = d;			   // inherited operator int() — variable-init
	int from_return = get_value();

	if (i != 21)
		return 1;
	if (from_return != 21)
		return 2;

	return i + from_return;	// 42
}
