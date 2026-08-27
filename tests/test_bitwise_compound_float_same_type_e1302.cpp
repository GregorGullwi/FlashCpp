// C++20 [expr.bit.and]: bitwise operators require integral operands.
// Same-type floating-point operands must not reach the integer IR opcodes.
int main() {
	float value = 7.0f;
	float mask = 3.0f;
	value &= mask;
	return 0;
}
