// C++20 [expr.shift]/1 requires integral or unscoped enum operands.
// Indirect lvalues must use the same built-in shift validation.
int main() {
	int value = 8;
	int* pointer = &value;
	double shift = 3.0;
	*pointer >>= shift;
	return value;
}
