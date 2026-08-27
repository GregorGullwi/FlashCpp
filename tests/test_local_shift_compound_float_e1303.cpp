// C++20 [expr.shift]/1 requires integral or unscoped enum operands.
// The general local lowering path must reject a floating-point shift count.
int main() {
	int value = 8;
	double shift = 3.0;
	value >>= shift;
	return value;
}
