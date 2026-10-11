// The callable takes int but the pointer-to-member target takes double. A
// differing parameter list is a different function type, so the conversion is
// ill-formed.
struct C {
	int f(int) const;
};

int (C::*p)(double) const = &C::f;

int main() {
	return 0;
}
