// C++20 [expr.bit.xor]: bitwise operators require integral operands.
// Global compound assignment must use the structured operator diagnostic.
double value = 7.0;

int main() {
	value ^= 3.0;
	return 0;
}
