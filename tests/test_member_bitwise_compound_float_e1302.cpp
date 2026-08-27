// C++20 [expr.bit.or]: bitwise operators require integral operands.
// Member lvalues must use the same diagnostic as local variables.
struct Holder {
	double value;
};

int main() {
	Holder holder{7.0};
	holder.value |= 3.0;
	return 0;
}
