// Regression: sema-owned overload-resolution argument typing should cover
// literal, unary, and comparison/assignment/comma expression forms so codegen
// does not reconstruct them in normalized bodies.

struct Sink {
	int kind;

	Sink(int) : kind(1) {}
	Sink(bool) : kind(2) {}
};

int main() {
	Sink from_numeric(42);
	if (from_numeric.kind != 1)
		return 1;

	Sink from_bool(true);
	if (from_bool.kind != 2)
		return 2;

	Sink from_compare(40 + 2 == 42);
	if (from_compare.kind != 2)
		return 3;

	Sink from_not(!0);
	if (from_not.kind != 2)
		return 4;

	Sink from_comma((0, 7));
	if (from_comma.kind != 1)
		return 5;

	int value = 0;
	Sink from_assign(value = 9);
	if (from_assign.kind != 1)
		return 6;
	if (value != 9)
		return 7;

	return 0;
}
